#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
set -euo pipefail
cd "$(dirname "$0")/.."
d=$(mktemp -d)
trap 'rm -rf -- "$d"' EXIT
for mode in plain sanitized;do
 flags=(-std=c11 -O2 -g -Wall -Wextra -Werror -DX4_PUBLIC=0)
 if [[ $mode == sanitized ]];then flags+=(-fsanitize=address,undefined -fno-omit-frame-pointer -no-pie);fi
 "${CC:-clang}" "${flags[@]}" tests/test_tarea20c_metrics.c src/ui/ui_draw_metrics.c -o "$d/metrics-$mode"
 ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 "$d/metrics-$mode"
done
