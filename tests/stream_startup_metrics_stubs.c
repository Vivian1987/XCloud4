/* SPDX-License-Identifier: GPL-3.0-only */
/* HOST-ONLY startup harness dependency completion. These owners are not
 * present in a parser/scheduler/native-double test. Empty observations are
 * explicit fixtures, not measured RTC/server/input data or coverage of those
 * exporters. This file must never be linked into a native application. */
#include <stdio.h>
#include <string.h>
#include "../src/streaming/stream_profile.h"
#include "../src/streaming/rtc_receive_diag.h"
#include "../src/ui/server_settings.h"

void x4_live_interaction_metrics_write(FILE *f)
{
    fputs("{\"available\":false,\"fixture_only\":true}", f);
}
void x4_rtc_receive_diag_summary(X4InboundRtpSummary *out)
{
    if (out) memset(out, 0, sizeof(*out));
}
void x4_server_settings_write_json(FILE *f)
{
    fputs("\"server\":{\"selected\":\"auto\",\"is_automatic\":true,\"default\":\"\",\"has_saved_choice\":false,\"regions\":[],\"fixture_only\":true},", f);
}
void x4_rtc_diag_write_ice_json(FILE *f)
{
    fputs("\"ice\":{\"remote_candidates\":[],\"has_selected_pair\":false,\"fixture_only\":true},", f);
}
void x4_rtc_diag_write_rtt_json(FILE *f)
{
    fputs("\"rtt_ms\":{\"p50\":-1,\"p95\":-1,\"samples\":0,\"series\":[],\"fixture_only\":true},", f);
}
