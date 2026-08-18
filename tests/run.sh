#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
#
# Run every automated check. No toolchain or hardware required.
#
# Set NOCFREE_BUILD_DIR to a directory containing left/ and right/ build trees
# to additionally check the built artifacts.

set -euo pipefail

TESTS="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CC="${CC:-cc}"
OUT="$(mktemp -d)"
trap 'rm -rf "${OUT}"' EXIT

# The unittest run below changes directory, so resolve NOCFREE_BUILD_DIR
# against the caller's directory first -- and refuse to run if it was set but
# does not hold both build trees, so the artifact checks can never be skipped
# silently by a mistyped path.
if [ -n "${NOCFREE_BUILD_DIR:-}" ]; then
    for role in left right; do
        if [ ! -f "${NOCFREE_BUILD_DIR}/${role}/zephyr/.config" ]; then
            echo "NOCFREE_BUILD_DIR=${NOCFREE_BUILD_DIR} has no ${role}/zephyr/.config" >&2
            exit 1
        fi
    done
    NOCFREE_BUILD_DIR="$(cd "${NOCFREE_BUILD_DIR}" && pwd)"
    export NOCFREE_BUILD_DIR
fi

echo "== scanner logic (C) =="
"${CC}" -std=c11 -Wall -Wextra -Werror -O2 \
    -o "${OUT}/test_kscan_scan" "${TESTS}/test_kscan_scan.c"
"${OUT}/test_kscan_scan"

echo
echo "== board, keymap, metadata and hygiene (Python) =="
cd "${TESTS}"
python3 -m unittest discover -s "${TESTS}" -p "test_*.py" -v
