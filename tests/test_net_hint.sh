#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${REPO_ROOT}/build/tests"
mkdir -p "${BUILD_DIR}"

[ -f "$HOME/.config/xcloud4/env.sh" ] && source "$HOME/.config/xcloud4/env.sh"
OO_PS4_TOOLCHAIN="${OO_PS4_TOOLCHAIN:-$HOME/.local/share/xcloud4/OpenOrbis/PS4Toolchain}"

"${CC:-clang}" -std=c11 -O1 -Wall -Wextra \
    -isystem "${OO_PS4_TOOLCHAIN}/include" \
    -I"${REPO_ROOT}" \
    -I"${REPO_ROOT}/src" \
    -I"${REPO_ROOT}/tests/host_stubs" \
    "${REPO_ROOT}/tests/test_net_hint.c" \
    "${REPO_ROOT}/src/ui/net_hint.c" \
    "${REPO_ROOT}/src/ui/lang.c" \
    -o "${BUILD_DIR}/test_net_hint"

"${BUILD_DIR}/test_net_hint"
echo "PASS: test_net_hint.sh"
