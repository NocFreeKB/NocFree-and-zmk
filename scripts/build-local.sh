#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
#
# Build both firmware images from a clean checkout using only public,
# pinned dependencies.
#
#   ./scripts/build-local.sh [workspace-dir]
#
# The west workspace is created outside this repository, exactly as ZMK's
# GitHub Actions build does, so nothing in the repo is modified. Requires
# Docker.

set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WS="${1:-${REPO}/../nocfree-and-zmk-build}"
IMAGE="${ZMK_BUILD_IMAGE:-zmkfirmware/zmk-build-arm:stable}"

mkdir -p "${WS}/config"
cp -R "${REPO}/config/." "${WS}/config/"
WS="$(cd "${WS}" && pwd)"

docker run --rm ${ZMK_BUILD_PLATFORM:+--platform "${ZMK_BUILD_PLATFORM}"} \
    -v "${WS}:/ws" \
    -v "${REPO}:/module:ro" \
    -w /ws \
    -e HOME=/tmp \
    "${IMAGE}" \
    bash -euo pipefail -c '
        # Some ZMK images set this, others ship the SDK without exporting it.
        if [ -z "${ZEPHYR_SDK_INSTALL_DIR:-}" ]; then
            sdk=$(ls -d /opt/zephyr-sdk-* 2>/dev/null | head -1)
            if [ -n "${sdk}" ]; then
                export ZEPHYR_SDK_INSTALL_DIR="${sdk}"
                export ZEPHYR_TOOLCHAIN_VARIANT=zephyr
            fi
        fi

        if [ ! -d /ws/.west ]; then
            west init -l /ws/config
        fi
        west update --fetch-opt=--filter=tree:0
        west zephyr-export

        for role in left right; do
            west build -p -s zmk/app -d "/ws/build/${role}" \
                -b "nocfree_and_${role}/nrf52833/zmk" \
                -- -DZMK_CONFIG=/ws/config -DZMK_EXTRA_MODULES=/module
        done
    '

echo
echo "Artifacts:"
for role in left right; do
    ls -l "${WS}/build/${role}/zephyr/zmk.uf2" 2>/dev/null || true
done
