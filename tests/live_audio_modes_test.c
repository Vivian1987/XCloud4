/* SPDX-License-Identifier: GPL-3.0-only */
/* Real audio helpers/worker, deterministic Opus/AudioOut/RTP doubles. No video. */
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "../src/audio/live_audio.c"

static uint64_t clock_us;
static int decode_fail;
static void *(*created_worker)(void *);
static unsigned output_calls;
static X4LiveAudio *worker_audio;
static int join_result, close_result;
static bool underflow_on_join;
static unsigned destroyed_decoders;
OpusDecoder *opus_decoder_create(int rate, int channels, int *rc)
{ assert(rate == 48000 && channels == 2); *rc = 0; return calloc(1, sizeof(OpusDecoder)); }
void opus_decoder_destroy(OpusDecoder *d) { ++destroyed_decoders; free(d); }
int opus_decoder_ctl(OpusDecoder *d, int command, ...)
{ assert(command == OPUS_RESET_STATE); ++d->resets; return 0; }
int opus_packet_get_nb_samples(const unsigned char *p, opus_int32 n, int rate)
{ assert(rate == 48000); return n ? (int)p[0] * 120 : -1; }
int opus_decode(OpusDecoder *d, const unsigned char *p, opus_int32 size, int16_t *out, int capacity, int fec)
{
    assert(!fec); ++d->calls;
    if (decode_fail) return -4;
    int count = p ? opus_packet_get_nb_samples(p, size, 48000) : capacity;
    assert(count <= capacity);
    if (!p) ++d->plc_calls;
    for (int i = 0; i < count * 2; ++i) out[i] = p ? 12000 : 9000;
    return count;
}
uint64_t sceKernelGetProcessTime(void) { return clock_us; }
int sceKernelUsleep(unsigned us) { clock_us += us; atomic_store(&worker_audio->stop, true); return 0; }
int scePthreadCreate(OrbisPthread *t, const pthread_attr_t *at, void *(*fn)(void *), void *opaque, const char *name)
{ (void)t; (void)at; (void)opaque; assert(!strcmp(name, "x4-opus")); created_worker = fn; return 0; }
int scePthreadJoin(OrbisPthread t, void **result)
{
    (void)t; (void)result;
    if (!join_result && underflow_on_join) new_underflow(worker_audio, clock_us + 2000);
    return join_result;
}
int x4_live_ring_init(X4LiveRing *q, uint32_t cap) { (void)q; assert(cap == 96); return 0; }
void x4_live_ring_free(X4LiveRing *q) { (void)q; }
size_t x4_live_ring_pop(X4LiveRing *q, uint8_t *p) { (void)q; (void)p; return 0; }
bool x4_live_ring_push(X4LiveRing *q, const uint8_t *p, size_t n) { (void)q; (void)p; (void)n; return true; }
int x4_live_reorder_init(X4LiveReorder *q, uint16_t w, uint16_t d, uint64_t wait)
{ (void)q; assert(w == 64 && d == 8 && wait == 30000); return 0; }
void x4_live_reorder_free(X4LiveReorder *q) { (void)q; }
void x4_live_reorder_reset(X4LiveReorder *q) { (void)q; }
int x4_live_reorder_insert(X4LiveReorder *q, const uint8_t *p, size_t n, uint16_t seq, uint64_t now)
{ (void)q; (void)p; (void)n; (void)seq; (void)now; return 0; }
const uint8_t *x4_live_reorder_next(X4LiveReorder *q, uint64_t now, size_t *n, uint32_t *lost)
{ (void)q; (void)now; *n = 0; *lost = 0; return NULL; }
void x4_live_track_init(X4LiveTrack *t, int type) { memset(t, 0, sizeof(*t)); t->forced_type = type; }
int x4_live_track_accept(X4LiveTrack *t, const X4LiveRtp *r) { (void)t; (void)r; return 0; }
int x4_live_rtp_parse(const uint8_t *p, size_t n, X4LiveRtp *r) { (void)p; (void)n; (void)r; return -1; }
static int32_t out_init(void) { return 0; }
static int32_t out_open(OrbisUserServiceUserId u, enum OrbisAudioOutPort port, int32_t index, uint32_t samples, uint32_t rate, uint32_t format)
{ (void)u; (void)port; (void)index; (void)format; assert(samples == AUDIO_BLOCK && rate == 48000); return 7; }
static int32_t out_write(int32_t h, const void *p)
{ assert(h == 7); if (p) { ++output_calls; atomic_store(&worker_audio->stop, true); } return 0; }
static int32_t out_close(int32_t h) { assert(h == 7); return close_result; }
int x4_module_open(const char *name) { assert(!strcmp(name, "libSceAudioOut")); return 1; }
int x4_module_symbol(int h, const char *name, void **value)
{
    assert(h == 1);
    if (!strcmp(name, "sceAudioOutInit")) *value = (void *)out_init;
    else if (!strcmp(name, "sceAudioOutOpen")) *value = (void *)out_open;
    else if (!strcmp(name, "sceAudioOutOutput")) *value = (void *)out_write;
    else if (!strcmp(name, "sceAudioOutClose")) *value = (void *)out_close;
    else assert(0);
    return 0;
}
static X4LiveAudio *audio(bool previous)
{
    int rc = -1; X4LiveAudio *a = x4_live_audio_start_mode(&rc, 111, previous);
    assert(a && !rc && created_worker == (previous ? play : play_new));
    return a;
}
static void packet(X4LiveAudio *a, unsigned timestamp, unsigned duration, int16_t *decoded)
{
    uint8_t payload = (uint8_t)(duration / 120);
    X4LiveRtp r = { .payload = &payload, .size = 1, .timestamp = timestamp };
    new_decode_packet(a, &r, decoded);
}
static void test_threshold_and_legacy(void)
{
    int16_t samples[1920 * 2] = {0};
    X4LiveAudio *a = audio(false); worker_audio = a; output_calls = 0;
    new_append(a, samples, 1024); play_new(a);
    assert(!output_calls && !atomic_load(&a->playing)); x4_live_audio_stop(a);
    a = audio(false); worker_audio = a; output_calls = 0;
    new_append(a, samples, 1920); play_new(a);
    assert(output_calls == 1 && a->pcm_count == 896 && !atomic_load(&a->restarts)); x4_live_audio_stop(a);
    a = audio(false); worker_audio = a; output_calls = 0; a->have_started = true;
    new_append(a, samples, 1920); play_new(a);
    assert(output_calls == 1 && atomic_load(&a->restarts) == 1); x4_live_audio_stop(a);
    a = audio(true); worker_audio = a; output_calls = 0;
    append_pcm(a, samples, 1024); play(a);
    assert(output_calls == 1); X4LiveAudioSnapshot s; x4_live_audio_snapshot(a, &s);
    assert(s.previous && !s.detailed_metrics_available); x4_live_audio_stop(a);
    int rc; a = x4_live_audio_start(&rc, 111); assert(a && a->previous && created_worker == play); x4_live_audio_stop(a);
    puts("PASS audio target40ms, restart threshold and previous worker/wrapper");
}
static void test_jitter(void)
{
    X4LiveAudio *a = audio(false); int16_t decoded[5760 * 2];
    /* Alternate +/-6 ms arrival jitter at 20 ms packet cadence, 40 ms prebuffer. */
    unsigned next = 0; uint64_t next_output = 0; bool started = false;
    for (uint64_t now = 0; now < 3000000; now += 100) {
        uint64_t arrival = (uint64_t)next * 20000u + (next % 2 ? 6000u : 0u);
        if (now >= arrival) { packet(a, next * 960u, 960, decoded); ++next; new_level(a, now); }
        if (!started && a->pcm_count >= AUDIO_TARGET) { started = true; next_output = now; }
        if (started && now >= next_output) { new_render(a, decoded, now); next_output += 21333u; }
    }
    assert(started && !atomic_load(&a->underflows) && !atomic_load(&a->plc_samples));
    assert(!a->decoder->resets); x4_live_audio_stop(a);
    puts("PASS real new helpers: +/-6ms jitter, no underflow with40ms");
}
static void test_loss_plc_and_credit(void)
{
    X4LiveAudio *a = audio(false); int16_t decoded[5760 * 2];
    packet(a, 0, 960, decoded); packet(a, 1920, 960, decoded);
    assert(atomic_load(&a->plc_samples) == 960 && a->pcm_count == 2880 && !a->decoder->resets);
    a->pcm_count = 0; a->plc_run_samples = 0; a->concealed_ahead = 0;
    a->have_started = true; atomic_store(&a->playing, true);
    new_render(a, decoded, 100000);
    unsigned old = a->pcm_count, credit = a->concealed_ahead;
    packet(a, a->expected_timestamp, 960, decoded);
    assert(a->pcm_count == old && a->concealed_ahead == credit - 960);
    for (unsigned n = 1; n < 10 && atomic_load(&a->playing); ++n)
        new_render(a, decoded, 100000u + n * 21333u);
    assert(a->plc_run_samples == AUDIO_PLC_MAX && !atomic_load(&a->playing));
    assert(a->output_block[AUDIO_BLOCK * 2 - 1] == 0 && !a->decoder->resets);
    assert(atomic_load(&a->plc_samples) == 960 + AUDIO_PLC_MAX);
    decode_fail = 1; packet(a, a->expected_timestamp, 960, decoded); decode_fail = 0;
    assert(atomic_load(&a->decode_errors) == 1 && atomic_load(&a->bad_frames) == 1);
    x4_live_audio_stop(a);
    puts("PASS loss->PLC without reset,60ms cap/fade/rebuffer,late credit,decode error");
}
static void test_long_declared_gap(void)
{
    X4LiveAudio *a = audio(false); int16_t decoded[5760 * 2];
    packet(a, 0, 960, decoded); packet(a, 5760, 960, decoded);
    assert(atomic_load(&a->plc_samples) == 2880 && a->pcm_count == 6720);
    assert(!a->decoder->resets && !atomic_load(&a->pcm_overflows));
    /* 60 ms PLC, then5ms fade followed by silence for the rest of the100ms gap. */
    unsigned silence = 960 + 2880;
    assert(a->pcm[silence * 2] > 0 && a->pcm[silence * 2] < 9000);
    assert(a->pcm[(silence + 239) * 2] == 0 && a->pcm[(5760 - 1) * 2] == 0);
    x4_live_audio_stop(a);
    puts("PASS declared100ms loss:60ms PLC then fade/silence,without reset");
}
static void test_rebuffer_does_not_replay_old_silence(void)
{
    X4LiveAudio *a = audio(false); int16_t decoded[5760 * 2];
    packet(a, 0, 960, decoded); packet(a, 960, 960, decoded);
    a->have_started = true; atomic_store(&a->playing, true);
    for (unsigned n = 0; n < 10 && atomic_load(&a->playing); ++n)
        new_render(a, decoded, 100000u + n * 21333u);
    assert(!atomic_load(&a->playing) && !a->pcm_count);
    assert(a->plc_run_samples == AUDIO_PLC_MAX && atomic_load(&a->plc_samples) == AUDIO_PLC_MAX);
    assert(a->concealed_ahead && a->output_block[AUDIO_BLOCK * 2 - 1] == 0);
    /* 500ms of RTP time has elapsed while AudioOut was already silent. */
    packet(a, 24000, 960, decoded);
    assert(a->pcm_count == 960 && !a->concealed_ahead && !a->plc_run_samples);
    assert(atomic_load(&a->plc_samples) == AUDIO_PLC_MAX && !atomic_load(&a->pcm_overflows));
    worker_audio = a; output_calls = 0; play_new(a);
    assert(!output_calls && a->pcm_count == 960); /* 20ms real cannot restart. */
    atomic_store(&a->stop, false);
    packet(a, 24960, 960, decoded); assert(a->pcm_count == 1920);
    play_new(a); assert(output_calls == 1 && atomic_load(&a->restarts) == 1 && a->pcm_count == 896);
    for (unsigned i = 0; i < AUDIO_BLOCK * 2; ++i) assert(a->output_block[i] == 12000);
    assert(!atomic_load(&a->pcm_overflows) && !atomic_load(&a->trimmed_samples));
    x4_live_audio_stop(a);
    puts("PASS gap500ms after PLC exhaustion: no170ms old silence, restart with2real20ms packets");
}
static void test_joined_final_snapshot_and_close_retention(void)
{
    X4LiveAudio *a = audio(false); worker_audio = a; clock_us = 7000;
    new_underflow(a, 1000); X4LiveAudioSnapshot before, final;
    x4_live_audio_snapshot(a, &before); assert(before.underflows == 1 && !before.worker_joined);
    underflow_on_join = true;
    assert(x4_live_audio_stop_report(a, &final) == 0);
    underflow_on_join = false;
    assert(final.worker_joined && final.underflows == 2 && final.underflow_interval_count == 1);
    assert(final.underflow_interval_min_us == 8000 && final.underflow_interval_max_us == 8000);
    a = audio(false); worker_audio = a; join_result = 5; unsigned prior = destroyed_decoders;
    assert(x4_live_audio_stop_report(a, &final) == -5 && !final.worker_joined);
    assert(a->running == 1 && a->decoder && destroyed_decoders == prior);
    join_result = 0; close_result = -77;
    assert(x4_live_audio_stop_report(a, &final) == -77 && final.worker_joined);
    assert(!a->running && a->decoder && a->handle == 7 && destroyed_decoders == prior);
    close_result = 0; assert(!x4_live_audio_stop_report(a, &final));
    assert(final.worker_joined && destroyed_decoders == prior + 1);
    puts("PASS final snapshot after join includes final underflow; join/AudioOut-close failure retains ownership");
}
static void test_burst_and_metrics(void)
{
    X4LiveAudio *a = audio(false); int16_t decoded[5760 * 2] = {0};
    new_append(a, decoded, 4800); new_level(a, 1);
    new_level(a, 500001); assert(a->pcm_count == 4800);
    new_level(a, 500002); assert(a->pcm_count == 1920);
    assert(atomic_load(&a->trimmed_samples) == 2880 && !atomic_load(&a->bad_frames));
    new_append(a, decoded, 5760); new_append(a, decoded, 960);
    assert(atomic_load(&a->pcm_overflows) == 1 && !atomic_load(&a->bad_frames));
    new_underflow(a, 1000000); new_underflow(a, 1020000);
    X4LiveAudioSnapshot s; x4_live_audio_snapshot(a, &s);
    assert(s.underflows == 2 && s.underflow_episodes == 1 && s.underflow_interval_count == 1);
    assert(s.underflow_interval_min_us == 20000 && s.underflow_interval_max_us == 20000);
    assert(s.buffer_observations == 3 && s.buffer_max_us == 100000);
    atomic_store(&a->underflow_interval_min_us, UINT64_MAX);
    x4_live_audio_snapshot(a, &s); assert(s.underflow_interval_min_us == 0);
    atomic_store(&a->underflow_interval_min_us, 20000);
    x4_live_audio_snapshot(a, &s);
    FILE *f = tmpfile(); assert(f); x4_live_audio_metrics_write(f, &s); rewind(f);
    char json[2048] = {0}; assert(fread(json, 1, sizeof(json)-1, f)); fclose(f);
    assert(strstr(json, "\"fec_implemented\":false") && strstr(json, "\"pcm_overflows\":1"));
    printf("AUDIO_METRICS_JSON=%s\n", json);
    x4_live_audio_stop(a);
    puts("PASS burst sustained500ms->trim10ms to40ms,overflow separate,atomic snapshot/JSON");
}
int main(void)
{
    test_threshold_and_legacy(); test_jitter(); test_loss_plc_and_credit(); test_long_declared_gap();
    test_rebuffer_does_not_replay_old_silence(); test_joined_final_snapshot_and_close_retention(); test_burst_and_metrics();
    return 0;
}
