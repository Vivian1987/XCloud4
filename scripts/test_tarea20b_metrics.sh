#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
set -euo pipefail
cd "$(dirname "$0")/.."
cc=${CC:-gcc}
base=${X4_MENU_METRICS_OUTPUT:-$(mktemp -d)}
if [[ -z ${X4_MENU_METRICS_OUTPUT:-} ]]; then trap 'rm -rf -- "$base"' EXIT; fi
mkdir -p "$base"
for profile in 1 2; do
 for mode in plain sanitized; do
    out="$base/profile-$profile-$mode";mkdir -p "$out"
    flags=(-std=gnu2x -O1 -g -Wall -Wextra -Werror -pthread -DX4_OPENORBIS=1 -DX4_STREAM_WAIT_HOST=1 -DX4_STREAM_PIPELINE_TEST_HOOKS=1 -DX4_STREAM_PROFILE="$profile" -DX4_VIDEO_PROBE=0 -DX4_PUBLIC=0 -Itests/video_capacity_stubs -Itests/host_stubs)
    if [[ $mode == sanitized ]];then flags+=(-fno-omit-frame-pointer -fsanitize=address,undefined -no-pie);fi
    "$cc" "${flags[@]}" tests/test_tarea20b_metrics.c tests/stream_startup_metrics_stubs.c \
        src/video/video_pipeline.c src/video/stream_cpu.c src/video/stream_wait.c \
        src/video/stream_au_queue.c src/video/stream_diagnostics.c src/video/stream_sps.c \
        src/video/stream_color_policy.c src/streaming/stream_profile.c \
        src/streaming/stream_startup.c src/streaming/keyframe_limiter.c -o "$out/metrics"
    ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 timeout 30s "$out/metrics" "$out"
    python3 - "$out" <<'PY'
import json,pathlib,sys
reports=list(pathlib.Path(sys.argv[1]).glob('xcloud4-stream-*-p*-220.json'))
assert len(reports)==1,reports
data=json.loads(reports[0].read_text())
menu=data['menu_pipeline']
assert menu['conversion_without_menu_us']['n']==2,menu
assert menu['conversion_with_menu_us']['n']==1,menu
assert menu['presented_with_menu']==1,menu
assert menu['discarded_latency_while_open']==3,menu
assert menu['discarded_ring_full_while_open']==2,menu
assert data['ring']['discarded_latency']==6,data['ring']
assert data['ring']['discarded_ring_full']==4,data['ring']
for key in ('conversion_without_menu_us','conversion_with_menu_us'):
    metric=menu[key]
    assert metric['bin_us']==50 and metric['total_us']>=metric['maximum_us']>=0,metric
    assert metric['p95_upper_us']>=metric['p50_upper_us']>=0,metric
print('PASS parsed real menu JSON:',json.dumps(menu,sort_keys=True))
PY
 done
done
printf 'PASS menu diagnostic hooks/export profiles1/2 plain+ASan/UBSan; decoder and VideoOut are host doubles.\n'
