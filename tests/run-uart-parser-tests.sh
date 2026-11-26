#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
TEMP_DIR=$(mktemp -d)
trap 'rm -rf "$TEMP_DIR"' EXIT
LINK_FLAGS=(-Wl,--gc-sections)
if [[ $(uname -s) == Darwin ]]; then LINK_FLAGS=(-Wl,-dead_strip); fi
for REMOTE in 0 1; do
    DEFINES=(-DENABLE_UART)
    if [[ $REMOTE == 1 ]]; then DEFINES+=(-DENABLE_REMOTE_CONTROL); fi
    "${CC:-cc}" -O2 -std=c2x -ffunction-sections -fdata-sections "${DEFINES[@]}" \
        -I "$ROOT" -I "$ROOT/external/CMSIS_5/CMSIS/Core/Include" \
        -I "$ROOT/external/CMSIS_5/Device/ARM/ARMCM0/Include" \
        "$ROOT/tests/uart_parser_test.c" "$ROOT/app/uart_at.c" \
        "${LINK_FLAGS[@]}" -o "$TEMP_DIR/uart-test"
    "$TEMP_DIR/uart-test"
done
