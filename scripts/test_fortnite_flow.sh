#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
set -euo pipefail
cd "$(dirname "$0")/.."
build_dir=$(mktemp -d)
trap 'rm -rf "$build_dir"' EXIT

"${CC:-clang}" -std=c11 -D_POSIX_C_SOURCE=200809L -O1 -Wall -Wextra -Werror -DX4_PUBLIC=1 \
    -isystem "$OO_PS4_TOOLCHAIN/include" -I. -Isrc -Ithird_party -Itests/host_stubs \
    tests/test_fortnite_flow.c \
    src/ui/screen.c src/ui/apple_ui.c src/ui/canvas.c src/ui/font.c src/ui/lang.c src/ui/net_hint.c \
    src/ui/server_settings.c src/ui/picture_settings.c src/streaming/stream_profile.c \
    src/auth/release_check.c src/auth/json.c third_party/qrcodegen/qrcodegen.c \
    -lpthread -lm -o "$build_dir/test_fortnite_flow_pub"
"$build_dir/test_fortnite_flow_pub"

"${CC:-clang}" -std=c11 -D_POSIX_C_SOURCE=200809L -O1 -Wall -Wextra -Werror -DX4_PUBLIC=0 \
    -isystem "$OO_PS4_TOOLCHAIN/include" -I. -Isrc -Ithird_party -Itests/host_stubs \
    tests/test_fortnite_flow.c \
    src/ui/screen.c src/ui/apple_ui.c src/ui/canvas.c src/ui/font.c src/ui/lang.c src/ui/net_hint.c \
    src/ui/server_settings.c src/ui/picture_settings.c src/streaming/stream_profile.c \
    src/auth/release_check.c src/auth/json.c third_party/qrcodegen/qrcodegen.c \
    -lpthread -lm -o "$build_dir/test_fortnite_flow_lab"
"$build_dir/test_fortnite_flow_lab"
