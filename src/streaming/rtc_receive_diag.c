/* SPDX-License-Identifier: GPL-3.0-only */
#include "rtc_receive_diag.h"
#include "../media/live_trace.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#ifndef X4_RECEIVE_DIAG_HOST_TEST
#include <orbis/libkernel.h>
/* Already used by rtc_pthread.c's native syscall(20) adapter. */
extern int32_t scePthreadGetthreadid(void);
#else
extern uint64_t x4_receive_test_clock(void);
extern void x4_receive_test_sleep(unsigned);
extern uint64_t x4_receive_test_threadid(void);
#endif

/* XCloud4 owns exactly one RTC session at a time. A process-lifetime bridge
 * observes only that session. Clearing the sink then draining entered readers
 * prevents diagnostic storage from being freed under an upstream producer.
 * New producers after detach see NULL. Producers never spin or sleep. */
static _Atomic(X4Trace *) sink;
static atomic_uint producers;
/* Compiler TLS is not assumed to work in an OpenOrbis SELF. These fixed,
 * process-lifetime cells use the existing native pthread identity API.
 * Each identity owns its timestamps; an exhausted lookup returns unknown.
 * No allocation, lock, wait or unbounded retry occurs on a receive path. */
enum { RECEIVE_THREAD_CAP = 32 };
typedef struct {
    atomic_uint_fast64_t key, socket_time, delivery_time;
    /* One native thread writes/reads its own ticket. Different threads own
     * disjoint cells; no ticket is borrowed across a callback boundary. */
    X4NackPacket nack;
} ReceiveThread;
static ReceiveThread thread_slots[RECEIVE_THREAD_CAP];
static atomic_uint_fast64_t thread_omissions;
static atomic_uint_fast64_t binding_count;
static atomic_uint_fast64_t nack_ticket_omissions;

enum { INBOUND_STREAM_CAP = 16 };
typedef struct {
    atomic_uint_fast64_t key; /* (1ull<<63) | ((uint64_t)ssrc << 32) | (pt & 0xffffffffu) */
    atomic_uint_fast64_t packets;
    atomic_uint_fast64_t first_us;
    atomic_uint_fast64_t last_us;
} InboundStreamSlot;

static InboundStreamSlot inbound_streams[INBOUND_STREAM_CAP];
static atomic_uint_fast64_t srtp_decrypt_failures;
static atomic_uint_fast64_t unknown_ssrc_packets;
static atomic_uint_fast64_t unknown_pt_packets;
static atomic_uint_fast64_t expected_video_ssrc;
static atomic_uint_fast32_t expected_video_pt;
static atomic_bool unexpected_video_logged;
static char expected_video_mid[32];

static atomic_flag ice_diag_lock = ATOMIC_FLAG_INIT;
static X4IceConnectionSummary s_ice_summary;
static unsigned s_rtt_count = 0;
static int s_rtt_samples[X4_RTT_MAX_SAMPLES];

static inline void lock_ice(void) {
    while (atomic_flag_test_and_set_explicit(&ice_diag_lock, memory_order_acquire)) {}
}
static inline void unlock_ice(void) {
    atomic_flag_clear_explicit(&ice_diag_lock, memory_order_release);
}
_Static_assert(ATOMIC_POINTER_LOCK_FREE == 2 && ATOMIC_INT_LOCK_FREE == 2 &&
    ATOMIC_LONG_LOCK_FREE == 2 && ATOMIC_LLONG_LOCK_FREE == 2,
    "Receive diagnostics require lock-free bridge atomics");
_Static_assert((int)X4_RD_APP == (int)X4_MON_T_APP_CALLBACK &&
    (int)X4_RD_RESERVED == (int)X4_MON_T_RESERVED,
    "Receive stage ABI must match the monitor");

static ReceiveThread *thread_slot(void)
{
    int saved = errno;
#ifndef X4_RECEIVE_DIAG_HOST_TEST
    int32_t native_id = scePthreadGetthreadid();
    uint64_t key = native_id > 0 ? (uint32_t)native_id : 0;
#else
    uint64_t key = x4_receive_test_threadid();
#endif
    errno = saved;
    if (key) for (unsigned i = 0; i < RECEIVE_THREAD_CAP; ++i) {
        uint64_t observed = atomic_load_explicit(&thread_slots[i].key, memory_order_relaxed);
        if (!observed) {
            uint64_t empty = 0;
            if (atomic_compare_exchange_strong_explicit(&thread_slots[i].key, &empty,
                key, memory_order_relaxed, memory_order_relaxed)) observed = key;
            else observed = empty;
        }
        if (observed == key) return &thread_slots[i];
    }
    atomic_fetch_add_explicit(&thread_omissions, 1, memory_order_relaxed);
    return NULL;
}

uint64_t x4_rtc_receive_clock(void)
{
    int saved = errno;
#ifndef X4_RECEIVE_DIAG_HOST_TEST
    uint64_t now = sceKernelGetProcessTime();
#else
    uint64_t now = x4_receive_test_clock();
#endif
    errno = saved;
    return now;
}

/* Only fixed RTP header fields are inspected. Header-extension and padding
 * bytes may still be encrypted in SRTP: no payload, extension, OSN or key is
 * read here. A pre-authentication tuple is explicitly untrusted metadata. */
static bool tuple(const void *packet, size_t size, uint32_t *ssrc,
    uint16_t *seq, unsigned *pt, uint32_t *flags)
{
    const uint8_t *p = packet;
    if (!p || size < 2 || (p[0] >> 6) != 2) return false;
    if (p[1] >= 192 && p[1] <= 223) {
        *flags |= X4_RD_RTCP_FLAG;
        return false;
    }
    if (size < 12 || size < 12u + (p[0] & 15u) * 4u) return false;
    *seq = ((uint16_t)p[2] << 8) | p[3];
    *ssrc = ((uint32_t)p[8] << 24) | ((uint32_t)p[9] << 16) |
        ((uint32_t)p[10] << 8) | p[11];
    *pt = p[1] & 127u;
    return true;
}

static X4Trace *enter(void)
{
    /* seq_cst is intentional: detach's null publication and count drain must
     * order against both the producer's count and its subsequent sink load. */
    atomic_fetch_add_explicit(&producers, 1, memory_order_seq_cst);
    return atomic_load_explicit(&sink, memory_order_seq_cst);
}
static void leave(void)
{ atomic_fetch_sub_explicit(&producers, 1, memory_order_seq_cst); }

uint64_t x4_rtc_receive_packet(unsigned stage, const void *packet, size_t bytes,
    uint64_t preceding_time, uint32_t flags)
{
    int saved = errno;
    uint64_t now = x4_rtc_receive_clock();
    if (stage == X4_RD_UDP || stage == X4_RD_TCP || stage == X4_RD_TRACK_POP) {
        ReceiveThread *slot = thread_slot();
        if (slot) atomic_store_explicit(stage == X4_RD_TRACK_POP ?
            &slot->delivery_time : &slot->socket_time, now, memory_order_relaxed);
    }
    uint32_t ssrc = 0; uint16_t seq = 0; unsigned pt = 0;
    bool valid = tuple(packet, bytes, &ssrc, &seq, &pt, &flags);
    if (stage == X4_RD_SRTP_VALID && valid) {
        uint64_t key = (1ull << 63) | ((uint64_t)ssrc << 32) | pt;
        for (unsigned i = 0; i < INBOUND_STREAM_CAP; ++i) {
            uint64_t observed = atomic_load_explicit(&inbound_streams[i].key, memory_order_relaxed);
            if (!observed) {
                uint64_t empty = 0;
                if (atomic_compare_exchange_strong_explicit(&inbound_streams[i].key, &empty,
                    key, memory_order_relaxed, memory_order_relaxed)) {
                    observed = key;
                } else {
                    observed = empty;
                }
            }
            if (observed == key) {
                atomic_fetch_add_explicit(&inbound_streams[i].packets, 1, memory_order_relaxed);
                uint64_t unset = 0;
                (void)atomic_compare_exchange_strong_explicit(&inbound_streams[i].first_us, &unset, now,
                    memory_order_relaxed, memory_order_relaxed);
                atomic_store_explicit(&inbound_streams[i].last_us, now, memory_order_relaxed);
                break;
            }
        }
        uint64_t exp_ssrc = atomic_load_explicit(&expected_video_ssrc, memory_order_relaxed);
        uint32_t exp_pt = atomic_load_explicit(&expected_video_pt, memory_order_relaxed);
        if (exp_ssrc && (pt == exp_pt || pt == 102) && ssrc != (uint32_t)exp_ssrc) {
            if (!atomic_exchange_explicit(&unexpected_video_logged, true, memory_order_relaxed)) {
                printf("XCloud4: RTP video recibido con SSRC no coincidente: recibido=0x%08x pt=%u (esperado=0x%08x pt=%u)\n",
                    ssrc, pt, (uint32_t)exp_ssrc, exp_pt);
            }
        }
    }
    X4Trace *trace = enter();
    if (trace) {
        x4_trace_monitor_transport_packet(trace, stage, valid, ssrc, seq, pt,
            bytes, now, preceding_time, flags);
    }
    leave();
    errno = saved;
    return now;
}

void x4_rtc_receive_reject(unsigned stage, unsigned reason, int result,
    const void *packet, size_t bytes)
{
    int saved = errno;
    uint64_t now = x4_rtc_receive_clock();
    if (stage == X4_RD_SRTP_INPUT && (reason == X4_RD_SRTP_ERROR || reason == X4_RD_SRTP_AUTH_FAIL || reason == X4_RD_SRTP_REPLAY_FAIL)) {
        atomic_fetch_add_explicit(&srtp_decrypt_failures, 1, memory_order_relaxed);
    } else if (stage == X4_RD_SRTP_INPUT && reason == X4_RD_DEMUX_UNKNOWN) {
        atomic_fetch_add_explicit(&unknown_ssrc_packets, 1, memory_order_relaxed);
    } else if ((stage == X4_RD_PEER && reason == X4_RD_NO_TRACK) || (stage == X4_RD_TRACK && reason == X4_RD_RTP_PAYLOAD)) {
        atomic_fetch_add_explicit(&unknown_pt_packets, 1, memory_order_relaxed);
    }
    X4Trace *trace = enter();
    if (trace) {
        uint32_t ssrc = 0, flags = 0; uint16_t seq = 0; unsigned pt = 0;
        bool valid = tuple(packet, bytes, &ssrc, &seq, &pt, &flags);
        x4_trace_monitor_transport_reject(trace, stage, reason, result,
            valid, ssrc, seq, now);
    }
    leave();
    errno = saved;
}

void x4_rtc_receive_diag_reset(void)
{
    atomic_store_explicit(&srtp_decrypt_failures, 0, memory_order_relaxed);
    atomic_store_explicit(&unknown_ssrc_packets, 0, memory_order_relaxed);
    atomic_store_explicit(&unknown_pt_packets, 0, memory_order_relaxed);
    atomic_store_explicit(&expected_video_ssrc, 0, memory_order_relaxed);
    atomic_store_explicit(&expected_video_pt, 0, memory_order_relaxed);
    atomic_store_explicit(&unexpected_video_logged, false, memory_order_relaxed);
    expected_video_mid[0] = '\0';
    for (unsigned i = 0; i < INBOUND_STREAM_CAP; ++i) {
        atomic_store_explicit(&inbound_streams[i].key, 0, memory_order_relaxed);
        atomic_store_explicit(&inbound_streams[i].packets, 0, memory_order_relaxed);
        atomic_store_explicit(&inbound_streams[i].first_us, 0, memory_order_relaxed);
        atomic_store_explicit(&inbound_streams[i].last_us, 0, memory_order_relaxed);
    }
    lock_ice();
    memset(&s_ice_summary, 0, sizeof(s_ice_summary));
    s_rtt_count = 0;
    memset(s_rtt_samples, 0, sizeof(s_rtt_samples));
    unlock_ice();
}

void x4_rtc_receive_diag_set_expected_video(uint32_t ssrc, uint32_t pt, const char *mid)
{
    atomic_store_explicit(&expected_video_ssrc, ssrc, memory_order_relaxed);
    atomic_store_explicit(&expected_video_pt, pt, memory_order_relaxed);
    if (mid) {
        size_t len = strlen(mid);
        if (len >= sizeof(expected_video_mid)) len = sizeof(expected_video_mid) - 1;
        memcpy(expected_video_mid, mid, len);
        expected_video_mid[len] = '\0';
    } else {
        expected_video_mid[0] = '\0';
    }
}

void x4_rtc_receive_diag_summary(X4InboundRtpSummary *out)
{
    if (!out) return;
    memset(out, 0, sizeof(*out));
    out->srtp_decrypt_failures = atomic_load_explicit(&srtp_decrypt_failures, memory_order_relaxed);
    out->unknown_ssrc_packets = atomic_load_explicit(&unknown_ssrc_packets, memory_order_relaxed);
    out->unknown_pt_packets = atomic_load_explicit(&unknown_pt_packets, memory_order_relaxed);
    out->sdp_video_ssrc = (uint32_t)atomic_load_explicit(&expected_video_ssrc, memory_order_relaxed);
    out->sdp_video_pt = atomic_load_explicit(&expected_video_pt, memory_order_relaxed);
    memcpy(out->sdp_video_mid, expected_video_mid, sizeof(out->sdp_video_mid));
    unsigned count = 0;
    for (unsigned i = 0; i < INBOUND_STREAM_CAP; ++i) {
        uint64_t key = atomic_load_explicit(&inbound_streams[i].key, memory_order_relaxed);
        if (key & (1ull << 63)) {
            out->streams[count].ssrc = (uint32_t)(key >> 32);
            out->streams[count].payload_type = (uint32_t)(key & 0xffffffffu);
            out->streams[count].packets = atomic_load_explicit(&inbound_streams[i].packets, memory_order_relaxed);
            out->streams[count].first_us = atomic_load_explicit(&inbound_streams[i].first_us, memory_order_relaxed);
            out->streams[count].last_us = atomic_load_explicit(&inbound_streams[i].last_us, memory_order_relaxed);
            ++count;
        }
    }
    out->stream_count = count;
}
uint64_t x4_rtc_receive_socket_time(void)
{
    ReceiveThread *slot = thread_slot();
    return slot ? atomic_load_explicit(&slot->socket_time,memory_order_relaxed) : 0;
}
uint64_t x4_rtc_receive_delivery_time(void)
{
    ReceiveThread *slot = thread_slot();
    return slot ? atomic_load_explicit(&slot->delivery_time,memory_order_relaxed) : 0;
}
void x4_rtc_receive_setting(unsigned setting, uint64_t a, uint64_t b, uint64_t c)
{
    int saved = errno;
    uint64_t now = x4_rtc_receive_clock();
    X4Trace *trace = enter();
    if (trace) {
#if X4_STREAM_PROFILE
        if(setting==14 && (a>>32)<2 && (a&0xffffu)<128 && b<=0xffffffu)
            x4_trace_sdp_profile_record(trace,(unsigned)(a>>32),(unsigned)(a&0xffffu),(uint32_t)b);
#endif
        x4_trace_monitor_critical(trace, now, X4_MON_CR_RTC_SETTING,
            0xffffu, 0, setting, 0, a, b, c);
    }
    leave();
    errno = saved;
}
void x4_rtc_nack_event(unsigned event, unsigned flags, uint64_t identity,
    uint64_t source, uint64_t a, uint64_t b, uint64_t c)
{
    int saved = errno;
    uint64_t now = x4_rtc_receive_clock();
    X4Trace *trace = enter();
    /* The newly used vendor sequence records share critical31 with worker
     * grace records. Low byte selects subkind13..16, remaining bits are its
     * flags. No extra critical-counter slot or record capacity is allocated. */
    unsigned stage = event == X4_MON_CR_NACK_GRACE ? flags & 255u : 0xffffu;
    unsigned record_flags = event == X4_MON_CR_NACK_GRACE ? flags >> 8 : flags;
    if (trace) x4_trace_monitor_critical(trace, now, event, stage, record_flags,
        identity, source, a, b, c);
    leave(); errno = saved;
}
void x4_rtc_nack_delivery(uint64_t identity, uint64_t source,
    uint64_t extended_sequence, uint32_t rtp_timestamp, uint64_t authenticated_us)
{
    ReceiveThread *slot = thread_slot();
    if (slot) slot->nack = (X4NackPacket){.identity=identity, .source=source,
        .extended_sequence=extended_sequence, .rtp_timestamp=rtp_timestamp,
        .authenticated_us=authenticated_us};
    else atomic_fetch_add_explicit(&nack_ticket_omissions,1,memory_order_relaxed);
}
void x4_rtc_nack_sequence_delivery(const X4NackSequenceTag *sequence)
{
    ReceiveThread *slot = thread_slot();
    if (slot) slot->nack.sequence = sequence ? *sequence : (X4NackSequenceTag){0};
    else atomic_fetch_add_explicit(&nack_ticket_omissions,1,memory_order_relaxed);
}
X4NackPacket x4_rtc_nack_delivery_ticket(void)
{
    ReceiveThread *slot = thread_slot();
    if (!slot) atomic_fetch_add_explicit(&nack_ticket_omissions,1,memory_order_relaxed);
    return slot ? slot->nack : (X4NackPacket){0};
}
void x4_rtc_receive_attach(X4Trace *trace)
{
    uint64_t bindings = atomic_fetch_add_explicit(&binding_count,1,memory_order_relaxed)+1;
    atomic_store_explicit(&sink, trace, memory_order_seq_cst);
    unsigned used=0;
    for(unsigned i=0;i<RECEIVE_THREAD_CAP;++i)
        if(atomic_load_explicit(&thread_slots[i].key,memory_order_relaxed))++used;
    x4_rtc_receive_setting(22,atomic_load_explicit(&thread_omissions,memory_order_relaxed),used,0);
    x4_rtc_receive_setting(23,bindings,1,0);
}
void x4_rtc_receive_detach(X4Trace *trace)
{
    X4Trace *expected = trace;
    if (!atomic_compare_exchange_strong_explicit(&sink, &expected, NULL,
        memory_order_seq_cst, memory_order_seq_cst)) return;
    while (atomic_load_explicit(&producers, memory_order_seq_cst)) {
#ifndef X4_RECEIVE_DIAG_HOST_TEST
        sceKernelUsleep(1000);
#else
        x4_receive_test_sleep(1000);
#endif
    }
    /* Owner still owns trace after draining entered hooks. Record final
     * coverage directly; never temporarily republish a detached sink.
     * The bridge has global scope, not an old/new transport epoch binding. */
    if (trace) {
        unsigned used=0;
        for(unsigned i=0;i<RECEIVE_THREAD_CAP;++i)
            if(atomic_load_explicit(&thread_slots[i].key,memory_order_relaxed))++used;
        uint64_t now=x4_rtc_receive_clock();
        x4_trace_monitor_critical(trace,now,X4_MON_CR_RTC_SETTING,0xffffu,0,22,0,
            atomic_load_explicit(&thread_omissions,memory_order_relaxed),used,1);
        x4_trace_monitor_critical(trace,now,X4_MON_CR_RTC_SETTING,0xffffu,0,23,0,
            atomic_load_explicit(&binding_count,memory_order_relaxed),1,0);
        x4_trace_monitor_critical(trace,now,X4_MON_CR_NACK_COUNTER,0xffffu,0,0,0,
            109,atomic_load_explicit(&nack_ticket_omissions,memory_order_relaxed),0);
    }
}


static bool parse_candidate_line(const char *cand, char *ip_out, size_t ip_cap, uint16_t *port_out,
                                char *typ_out, size_t typ_cap, char *trans_out, size_t trans_cap)
{
    if (!cand || !cand[0]) return false;
    if (ip_out && ip_cap) ip_out[0] = 0;
    if (port_out) *port_out = 0;
    if (typ_out && typ_cap) typ_out[0] = 0;
    if (trans_out && trans_cap) trans_out[0] = 0;

    const char *p = cand;
    if (!strncmp(p, "a=", 2)) p += 2;
    while (*p == ' ') p++;
    if (!strncmp(p, "candidate:", 10)) p += 10;
    while (*p == ' ') p++;

    /* Tokenize whitespace-separated words */
    char tokens[12][64];
    unsigned num_tokens = 0;
    while (*p && num_tokens < 12) {
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
        if (!*p) break;
        const char *tstart = p;
        while (*p && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n') p++;
        size_t tlen = (size_t)(p - tstart);
        if (tlen >= sizeof(tokens[num_tokens])) tlen = sizeof(tokens[num_tokens]) - 1;
        memcpy(tokens[num_tokens], tstart, tlen);
        tokens[num_tokens][tlen] = '\0';
        num_tokens++;
    }

    if (num_tokens >= 8) {
        /* Standard SDP candidate:
         * tokens[0]=foundation, tokens[1]=component, tokens[2]=transport (UDP),
         * tokens[3]=priority, tokens[4]=ip, tokens[5]=port, tokens[6]="typ", tokens[7]=type */
        if (trans_out && trans_cap) {
            size_t l = strlen(tokens[2]);
            if (l >= trans_cap) l = trans_cap - 1;
            memcpy(trans_out, tokens[2], l);
            trans_out[l] = 0;
        }
        if (ip_out && ip_cap) {
            size_t l = strlen(tokens[4]);
            if (l >= ip_cap) l = ip_cap - 1;
            memcpy(ip_out, tokens[4], l);
            ip_out[l] = 0;
        }
        if (port_out) {
            *port_out = (uint16_t)atoi(tokens[5]);
        }
        if (typ_out && typ_cap) {
            size_t l = strlen(tokens[7]);
            if (l >= typ_cap) l = typ_cap - 1;
            memcpy(typ_out, tokens[7], l);
            typ_out[l] = 0;
        }
        return true;
    }

    /* Fallback: try parsing IP:port */
    const char *colon = strrchr(cand, ':');
    if (colon && colon != cand) {
        size_t ip_len = (size_t)(colon - cand);
        if (ip_out && ip_cap) {
            if (ip_len >= ip_cap) ip_len = ip_cap - 1;
            memcpy(ip_out, cand, ip_len);
            ip_out[ip_len] = 0;
        }
        if (port_out) *port_out = (uint16_t)atoi(colon + 1);
        if (typ_out && typ_cap) snprintf(typ_out, typ_cap, "host");
        if (trans_out && trans_cap) snprintf(trans_out, trans_cap, "UDP");
        return true;
    }

    return false;
}

void x4_rtc_diag_record_remote_candidate(const char *cand_str, const char *mid)
{
    if (!cand_str || !cand_str[0]) return;
    char ip[48] = {0}, typ[16] = {0}, trans[8] = {0};
    uint16_t port = 0;
    if (!parse_candidate_line(cand_str, ip, sizeof(ip), &port, typ, sizeof(typ), trans, sizeof(trans))) return;
    if (!ip[0] || port == 0) return;

    lock_ice();
    for (unsigned i = 0; i < s_ice_summary.remote_candidate_count; ++i) {
        if (!strcmp(s_ice_summary.remote_candidates[i].ip, ip) &&
            s_ice_summary.remote_candidates[i].port == port) {
            unlock_ice();
            return; /* already recorded */
        }
    }

    if (s_ice_summary.remote_candidate_count < 32) {
        unsigned idx = s_ice_summary.remote_candidate_count++;
        snprintf(s_ice_summary.remote_candidates[idx].ip, sizeof(s_ice_summary.remote_candidates[idx].ip), "%s", ip);
        s_ice_summary.remote_candidates[idx].port = port;
        snprintf(s_ice_summary.remote_candidates[idx].type, sizeof(s_ice_summary.remote_candidates[idx].type), "%s", typ[0] ? typ : "host");
        snprintf(s_ice_summary.remote_candidates[idx].mid, sizeof(s_ice_summary.remote_candidates[idx].mid), "%s", mid ? mid : "0");
        snprintf(s_ice_summary.remote_candidates[idx].transport, sizeof(s_ice_summary.remote_candidates[idx].transport), "%s", trans[0] ? trans : "UDP");
    }
    unlock_ice();

    printf("XCloud4: ICE candidato remoto: %s:%u (tipo=%s mid=%s)\n", ip, port, typ[0] ? typ : "host", mid ? mid : "");
}

void x4_rtc_diag_record_selected_pair(const char *local, const char *remote)
{
    lock_ice();
    if (local) {
        snprintf(s_ice_summary.selected_local, sizeof(s_ice_summary.selected_local), "%s", local);
    }
    if (remote) {
        snprintf(s_ice_summary.selected_remote, sizeof(s_ice_summary.selected_remote), "%s", remote);
        char ip[48] = {0}, typ[16] = {0}, trans[8] = {0};
        uint16_t port = 0;
        if (parse_candidate_line(remote, ip, sizeof(ip), &port, typ, sizeof(typ), trans, sizeof(trans))) {
            snprintf(s_ice_summary.game_server_ip, sizeof(s_ice_summary.game_server_ip), "%s", ip);
            s_ice_summary.game_server_port = port;
        }
    }
    s_ice_summary.has_selected_pair = true;
    unlock_ice();

    printf("XCloud4: ICE par seleccionado: local=%s remote=%s\n", local ? local : "", remote ? remote : "");
    if (s_ice_summary.game_server_ip[0]) {
        printf("XCloud4: Servidor de juego (envia video): %s:%u\n",
            s_ice_summary.game_server_ip, s_ice_summary.game_server_port);
    }
}

void x4_rtc_diag_get_ice_summary(X4IceConnectionSummary *out)
{
    if (!out) return;
    lock_ice();
    *out = s_ice_summary;
    unlock_ice();
}

void x4_rtc_diag_write_ice_json(FILE *f)
{
    if (!f) return;
    lock_ice();
    fprintf(f, "\"ice\":{\"remote_candidates\":[");
    for (unsigned i = 0; i < s_ice_summary.remote_candidate_count; ++i) {
        if (i) fputc(',', f);
        fprintf(f, "{\"ip\":\"%s\",\"port\":%u,\"type\":\"%s\",\"mid\":\"%s\",\"transport\":\"%s\"}",
            s_ice_summary.remote_candidates[i].ip,
            (unsigned)s_ice_summary.remote_candidates[i].port,
            s_ice_summary.remote_candidates[i].type,
            s_ice_summary.remote_candidates[i].mid,
            s_ice_summary.remote_candidates[i].transport);
    }
    fprintf(f, "],\"selected_candidate_pair\":");
    if (s_ice_summary.has_selected_pair) {
        fprintf(f, "{\"local\":\"%s\",\"remote\":\"%s\",\"game_server_ip\":\"%s\",\"game_server_port\":%u}",
            s_ice_summary.selected_local,
            s_ice_summary.selected_remote,
            s_ice_summary.game_server_ip,
            (unsigned)s_ice_summary.game_server_port);
    } else {
        fprintf(f, "null");
    }
    fprintf(f, "},");
    unlock_ice();
}


void x4_rtc_diag_record_rtt_sample(int rtt_ms)
{
    if (rtt_ms < 0) return;
    lock_ice();
    if (s_rtt_count < X4_RTT_MAX_SAMPLES) {
        s_rtt_samples[s_rtt_count++] = rtt_ms;
    }
    unlock_ice();
}

static void sort_rtt_samples(int *arr, unsigned n)
{
    for (unsigned i = 0; i < n - 1; ++i) {
        for (unsigned j = i + 1; j < n; ++j) {
            if (arr[j] < arr[i]) {
                int tmp = arr[i];
                arr[i] = arr[j];
                arr[j] = tmp;
            }
        }
    }
}

void x4_rtc_diag_get_rtt_summary(X4RttSummary *out)
{
    if (!out) return;
    memset(out, 0, sizeof(*out));
    lock_ice();
    unsigned n = s_rtt_count;
    out->sample_count = n;
    if (n == 0) {
        out->p50 = -1;
        out->p95 = -1;
        unlock_ice();
        return;
    }
    for (unsigned i = 0; i < n; ++i) {
        out->samples[i] = s_rtt_samples[i];
    }
    unlock_ice();

    int temp[X4_RTT_MAX_SAMPLES];
    memcpy(temp, out->samples, n * sizeof(int));
    sort_rtt_samples(temp, n);

    out->p50 = temp[n / 2];
    unsigned p95_idx = (n * 95) / 100;
    if (p95_idx >= n) p95_idx = n - 1;
    out->p95 = temp[p95_idx];
}

void x4_rtc_diag_write_rtt_json(FILE *f)
{
    if (!f) return;
    X4RttSummary rtt;
    x4_rtc_diag_get_rtt_summary(&rtt);
    fprintf(f, "\"rtt_ms\":{\"p50\":%d,\"p95\":%d,\"samples\":%u,\"series\":[",
        rtt.p50, rtt.p95, rtt.sample_count);
    for (unsigned i = 0; i < rtt.sample_count; ++i) {
        if (i) fputc(',', f);
        fprintf(f, "%d", rtt.samples[i]);
    }
    fprintf(f, "]},");
}
