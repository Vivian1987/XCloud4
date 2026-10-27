#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
set -Eeuo pipefail
cd "$(dirname "$0")/.."

echo "Compiling and running test_ca_bundle..."
gcc -std=c11 -Wall -Wextra -O2 -I. tests/test_ca_bundle.c src/auth/ca_bundle.c -o /tmp/test_ca_bundle
/tmp/test_ca_bundle
rm -f /tmp/test_ca_bundle
echo "PASS: test_ca_bundle.sh"
