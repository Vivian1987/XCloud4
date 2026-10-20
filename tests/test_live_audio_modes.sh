#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Focused audio only. No video runtime, VM setup or complete project suite.
set -euo pipefail
cd "$(dirname "$0")/.."
audio_tmp=$(mktemp -d)
trap 'rm -f -- "$audio_tmp/test" "$audio_tmp/result.log"; rmdir -- "$audio_tmp"' EXIT
"${CC:-cc}" -std=c11 -O1 -g -Wall -Wextra -Werror -pthread \
    -DX4_VIDEO_PROBE=0 -DX4_STREAM_PROFILE=1 -Itests/audio_stubs \
    tests/live_audio_modes_test.c -o "$audio_tmp/test"
"$audio_tmp/test" | tee "$audio_tmp/result.log"
python3 -B tests/live_audio_mode_source_test.py "$audio_tmp/result.log"
