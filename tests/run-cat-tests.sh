#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
TEMP_DIR=$(mktemp -d)
trap 'rm -rf "$TEMP_DIR"' EXIT
LINK_FLAGS=(-Wl,--gc-sections)
if [[ $(uname -s) == Darwin ]]; then LINK_FLAGS=(-Wl,-dead_strip); fi
"${CC:-cc}" -O2 -std=c2x -ffunction-sections -fdata-sections -DENABLE_UART -DENABLE_REMOTE_CONTROL \
    -I "$ROOT" -I "$ROOT/external/CMSIS_5/CMSIS/Core/Include" \
    -I "$ROOT/external/CMSIS_5/Device/ARM/ARMCM0/Include" \
    "$ROOT/tests/cat_test.c" "$ROOT/radio.c" "$ROOT/frequencies.c" "$ROOT/dcs.c" \
    "${LINK_FLAGS[@]}" -o "$TEMP_DIR/cat-test"
"$TEMP_DIR/cat-test"
