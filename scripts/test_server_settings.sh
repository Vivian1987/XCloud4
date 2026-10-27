#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
set -euo pipefail
cd "$(dirname "$0")/.."
build_dir=$(mktemp -d)
trap 'rm -rf "$build_dir"' EXIT

"${CC:-clang}" -std=c11 -O1 -Wall -Wextra -Werror ${X4_TEST_CFLAGS:-} \
  -Itests/host_stubs \
  tests/test_server_settings.c \
  src/ui/server_settings.c \
  src/ui/picture_settings.c src/ui/apple_ui.c third_party/qrcodegen/qrcodegen.c \
  src/ui/canvas.c \
  src/ui/lang.c \
  src/ui/font.c \
  src/streaming/stream_profile.c \
  -lpthread -lm \
  -o "$build_dir/test_server_settings"

"$build_dir/test_server_settings"
