#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
set -euo pipefail
cd "$(dirname "$0")/.."
build_dir=$(mktemp -d)
trap 'rm -rf -- "$build_dir"' EXIT
compatibility_flags=()
case "$("${CC:-clang}" --version)" in *[Cc]lang*) compatibility_flags+=(-Wno-c23-extensions);;esac
# Baseline catalog uses strcasecmp; baseline HTTP has a label-declaration.
# Host compatibility only: do not change either product path for this gate.
for profile in 0 1;do
 "${CC:-clang}" -std=gnu11 -D_POSIX_C_SOURCE=200809L -O2 -g -Wall -Wextra -Werror -include strings.h "${compatibility_flags[@]}" ${X4_TEST_CFLAGS:-} \
  -DX4_STREAM_PROFILE="$profile" -DX4_STREAM_WAIT_HOST=1 -ffunction-sections -fdata-sections -Itests/host_stubs \
  tests/test_tarea20c_xbox_queue.c src/auth/http_client.c src/auth/ca_bundle.c src/auth/json.c \
  src/auth/xbox_recent.c src/streaming/stream_profile.c src/ui/lang.c src/ui/server_settings.c -Wl,--gc-sections \
  -o "$build_dir/queue-$profile"
 "$build_dir/queue-$profile"
done
