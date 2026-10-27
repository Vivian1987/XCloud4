/* SPDX-License-Identifier: GPL-3.0-only */
#include "../src/streaming/stream_startup.h"
#include <assert.h>
#include <stdio.h>

static void test_mode_0_retries_every_300ms(void)
{
    X4StreamStartup s;
    bool request = false;
    x4_stream_startup_init_mode(&s, true, 0);
    assert(x4_stream_startup_request_mode(&s) == 0);
    x4_stream_startup_note_alias(&s, 0, 1000000);
    assert(x4_stream_startup_alias_sends(&s) == 1);
    assert(x4_stream_startup_last_alias_send_us(&s) == 1000000);

    /* First poll at 1000000 requests */
    assert(!x4_stream_startup_poll(&s, 1000000, &request) && request);
    /* 299 ms later: no request */
    assert(!x4_stream_startup_poll(&s, 1299999, &request) && !request);
    /* 300 ms later: requests */
    assert(!x4_stream_startup_poll(&s, 1300000, &request) && request);
    assert(x4_stream_startup_alias_retries(&s) == 1);
    /* 600 ms later: requests */
    assert(!x4_stream_startup_poll(&s, 1600000, &request) && request);
    assert(x4_stream_startup_alias_retries(&s) == 2);
    /* 30s expiry from 1000000 is 31000000 */
    assert(!x4_stream_startup_poll(&s, 30999999, &request));
    assert(x4_stream_startup_poll(&s, 31000000, &request) == X4_STREAM_INITIAL_RESOLUTION_TIMEOUT && !request);
    assert(s.timed_out && !x4_stream_startup_waiting(&s));
}

static void test_mode_1_sends_once_no_resend(void)
{
    X4StreamStartup s;
    bool request = false;
    x4_stream_startup_init_mode(&s, true, 1);
    assert(x4_stream_startup_request_mode(&s) == 1);
    x4_stream_startup_note_alias(&s, 0, 1000000);
    assert(x4_stream_startup_alias_sends(&s) == 1);
    assert(x4_stream_startup_last_alias_send_us(&s) == 1000000);

    /* Mode 1 sent successfully once: must NEVER request again */
    for (uint64_t t = 1000000; t < 31000000; t += 300000) {
        assert(!x4_stream_startup_poll(&s, t, &request) && !request);
    }
    assert(x4_stream_startup_alias_retries(&s) == 0);
    /* Expires at 30 s */
    assert(x4_stream_startup_poll(&s, 31000000, &request) == X4_STREAM_INITIAL_RESOLUTION_TIMEOUT && !request);
    assert(s.timed_out);
}

static void test_mode_1_retries_on_failure(void)
{
    X4StreamStartup s;
    bool request = false;
    x4_stream_startup_init_mode(&s, true, 1);
    assert(x4_stream_startup_request_mode(&s) == 1);
    /* Failed send attempt at 1000000 (rc = -1) */
    x4_stream_startup_note_alias(&s, -1, 1000000);
    assert(x4_stream_startup_alias_sends(&s) == 1);
    assert(!x4_stream_startup_alias_sent_us(&s));

    /* Retries every 300 ms until success */
    assert(!x4_stream_startup_poll(&s, 1000000, &request) && request);
    assert(!x4_stream_startup_poll(&s, 1200000, &request) && !request);
    assert(!x4_stream_startup_poll(&s, 1300000, &request) && request);

    /* Second attempt fails too */
    x4_stream_startup_note_alias(&s, -1, 1300000);
    assert(x4_stream_startup_alias_sends(&s) == 2);
    assert(!x4_stream_startup_poll(&s, 1500000, &request) && !request);
    assert(!x4_stream_startup_poll(&s, 1600000, &request) && request);

    /* Third attempt succeeds at 1600000 */
    x4_stream_startup_note_alias(&s, 0, 1600000);
    assert(x4_stream_startup_alias_sends(&s) == 3);
    assert(x4_stream_startup_alias_sent_us(&s) == 1600000);

    /* Once successful, never requests again */
    assert(!x4_stream_startup_poll(&s, 1600000, &request) && !request);
    assert(!x4_stream_startup_poll(&s, 1900000, &request) && !request);
    assert(!x4_stream_startup_poll(&s, 5000000, &request) && !request);
    assert(!x4_stream_startup_poll(&s, 31599999, &request) && !request);
    assert(x4_stream_startup_poll(&s, 31600000, &request) == X4_STREAM_INITIAL_RESOLUTION_TIMEOUT && !request);
}

static void test_mode_2_repeats_every_5s_until_video(void)
{
    X4StreamStartup s;
    bool request = false;
    x4_stream_startup_init_mode(&s, true, 2);
    assert(x4_stream_startup_request_mode(&s) == 2);
    /* Successful send at 1000000 */
    x4_stream_startup_note_alias(&s, 0, 1000000);
    assert(x4_stream_startup_alias_sends(&s) == 1);

    /* No video yet. Between 1s and 5.999s: no request */
    assert(!x4_stream_startup_poll(&s, 1000000, &request) && !request);
    assert(!x4_stream_startup_poll(&s, 5999999, &request) && !request);
    /* At 6000000 (5 s after 1000000): requests repeat */
    assert(!x4_stream_startup_poll(&s, 6000000, &request) && request);
    x4_stream_startup_note_alias(&s, 0, 6000000);
    assert(x4_stream_startup_alias_sends(&s) == 2);

    /* Another 5 s without video */
    assert(!x4_stream_startup_poll(&s, 10999999, &request) && !request);
    assert(!x4_stream_startup_poll(&s, 11000000, &request) && request);
    x4_stream_startup_note_alias(&s, 0, 11000000);
    assert(x4_stream_startup_alias_sends(&s) == 3);

    /* Video packet arrives at 12000000! */
    x4_stream_startup_note_video_packet(&s);

    /* After video arrives: no further repeats at 16s, 21s, etc. */
    assert(!x4_stream_startup_poll(&s, 16000000, &request) && !request);
    assert(!x4_stream_startup_poll(&s, 21000000, &request) && !request);

    /* Sps admission completes startup */
    x4_stream_startup_admit(&s, 22000000);
    assert(s.admitted && !x4_stream_startup_waiting(&s));
    assert(!x4_stream_startup_poll(&s, 25000000, &request) && !request);
}

int main(void)
{
    X4StreamStartup s;bool request=true,attempted=true;
    x4_stream_startup_init(&s,false);
    x4_stream_startup_note_alias(&s,0,1000000);
    assert(!x4_stream_startup_waiting(&s)&&!x4_stream_startup_alias_sent_us(&s));
    assert(!x4_stream_startup_hold_size(&s,2560,1440));
    assert(!x4_stream_startup_poll(&s,20000000,&request)&&!request);
    x4_stream_startup_init(&s,true);
    assert(x4_stream_startup_waiting(&s)&&!x4_stream_startup_alias_result(&s,&attempted)&&!attempted);
    assert(x4_stream_startup_hold_size(&s,2560,1440));
    assert(!x4_stream_startup_poll(&s,20000000,&request)&&!request&&!s.timed_out);
    x4_stream_startup_note_alias(&s,-3,20000000);
    assert(x4_stream_startup_alias_result(&s,&attempted)==-3&&attempted&&!x4_stream_startup_alias_sent_us(&s));
    assert(!x4_stream_startup_poll(&s,40000000,&request)&&!request);
    x4_stream_startup_note_alias(&s,0,50000000);
    assert(!x4_stream_startup_poll(&s,50000000,&request)&&request);
    assert(!x4_stream_startup_poll(&s,50299999,&request)&&!request);
    assert(!x4_stream_startup_poll(&s,50300000,&request)&&request);
    x4_stream_startup_note_alias(&s,0,51000000); /* A duplicate cannot renew the clock. */
    assert(x4_stream_startup_alias_sent_us(&s)==50000000);
    assert(x4_stream_startup_hold_size(&s,2560,1440)&&s.dropped_aus==2);
    x4_stream_startup_admit(&s,53000000);
    assert(s.admitted&&s.elapsed_available&&s.alias_to_sps_us==3000000&&!x4_stream_startup_waiting(&s));
    assert(!x4_stream_startup_hold_size(&s,2560,1440)); /* Reconfiguration gets no exception. */
    assert(!x4_stream_startup_poll(&s,90000000,&request)&&!request);
    x4_stream_startup_init(&s,true);x4_stream_startup_note_alias(&s,0,1000000);
    assert(!x4_stream_startup_poll(&s,30999999,&request));
    assert(x4_stream_startup_poll(&s,31000000,&request)==X4_STREAM_INITIAL_RESOLUTION_TIMEOUT&&!request);
    assert(s.timed_out&&!x4_stream_startup_waiting(&s));
    x4_stream_startup_admit(&s,31000001);assert(!s.admitted);
    x4_stream_startup_init(&s,true);x4_stream_startup_note_alias(&s,0,1000000);
    x4_stream_startup_cancel(&s);assert(!x4_stream_startup_waiting(&s));
    assert(!x4_stream_startup_poll(&s,20000000,&request)&&!request);
    assert(!x4_stream_startup_hold_size(&s,2560,1440));
    x4_stream_startup_init(&s,true);x4_stream_startup_admit(&s,42);
    assert(s.admitted&&!s.elapsed_available&&!s.alias_to_sps_us); /* No causal timing is invented. */

    test_mode_0_retries_every_300ms();
    test_mode_1_sends_once_no_resend();
    test_mode_1_retries_on_failure();
    test_mode_2_repeats_every_5s_until_video();

    puts("PASS per-session initial-resolution clock, 30s expiry, modes 0/1/2 simulated clocks, admission and cancellation");
    return 0;
}
