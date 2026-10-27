#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
set -euo pipefail
cd "$(dirname "$0")/.."
temp=$(mktemp -d)
trap 'rm -rf -- "$temp"' EXIT
cc=${CC:-clang}
flags=(-std=c11 -D_DEFAULT_SOURCE -D_POSIX_C_SOURCE=200809L -O2 -g -Wall -Wextra -Werror)
for hooks in 0 1;do
    # Only stores are substituted in trap mode. The actual damage algorithm,
    # source loads and pointer arithmetic remain the production code.
    # shellcheck disable=SC2086
    "$cc" "${flags[@]}" ${X4_UI_TEST_FLAGS:-} -DX4_UI_SURFACE_TEST_HOOKS="$hooks" \
        tests/test_tarea20c_surface.c tests/ui_surface_test_trap.c src/ui/ui_surface.c \
        -o "$temp/surface-$hooks"
    ASAN_OPTIONS=detect_leaks=1:halt_on_error=1:handle_segv=0 UBSAN_OPTIONS=halt_on_error=1 \
        "$temp/surface-$hooks"
done
objdump -d --disassemble=x4_ui_surface_copy_row_nt "$temp/surface-0" > "$temp/nt-copy.asm"
objdump -d --disassemble=x4_ui_surface_finish_writes "$temp/surface-0" > "$temp/nt-fence.asm"
if [[ $(uname -m) == x86_64 ]];then
    # LLVM may preserve _mm_stream_si128 as MOVNTDQ or select the bitwise
    # equivalent MOVNTPS/MOVNTPD. Require an actual 128-bit XMM NT store;
    # regular MOVDQU/MOVUPS or unrelated scalar stores cannot satisfy it.
    grep -Eq '[[:space:]](v?movntdq|v?movntps|v?movntpd)[[:space:]]+%xmm[0-9]+,' "$temp/nt-copy.asm"
    grep -q sfence "$temp/nt-fence.asm"
fi
if [[ -n ${1:-} ]];then mkdir -p "$1";cp "$temp/nt-copy.asm" "$temp/nt-fence.asm" "$1/";fi
printf 'PASS NT-store/fence disassembly and real/trap presentation paths\n'
