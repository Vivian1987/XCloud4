#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
set -euo pipefail
cd "$(dirname "$0")/.."
build_dir=$(mktemp -d)
trap 'rm -rf "$build_dir"' EXIT
"${CC:-clang}" -std=c11 -D_POSIX_C_SOURCE=200809L -O2 -Wall -Wextra -Werror ${X4_UI_TEST_FLAGS:-} \
    -Itests/host_stubs tests/test_tarea20_images.c src/ui/apple_ui.c \
    src/ui/font.c src/ui/lang.c third_party/qrcodegen/qrcodegen.c \
    -Wl,--wrap=x4_text_px -lm -o "$build_dir/images"
if [[ -n "${1:-}" ]]; then mkdir -p "$1"; fi
"$build_dir/images" "$@"
