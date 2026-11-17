#!/usr/bin/env bash
# Modified for the Radio Remote fork: preserve the fixed toolchain PATH.
set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
BUILD_IMAGE=uvk5-rrcat3-dev-build
OUTPUT_DIR="$SCRIPT_DIR/build/dev-radio-remote-v3"

rm -rf "$OUTPUT_DIR"
mkdir -p "$OUTPUT_DIR"

docker build -t "$BUILD_IMAGE" "$SCRIPT_DIR"
docker run --rm -v "$OUTPUT_DIR:/output" "$BUILD_IMAGE" \
    /bin/bash -c \
    "cd /app && make VERSION_STRING=RRCAT3 clean && make VERSION_STRING=RRCAT3 && cp firmware.bin firmware.packed.bin /output/"

FIRMWARE_SIZE=$(wc -c < "$OUTPUT_DIR/firmware.bin" | tr -d ' ')
if (( FIRMWARE_SIZE > 61440 )); then
    echo "firmware.bin exceeds the 60 KiB limit: $FIRMWARE_SIZE bytes" >&2
    exit 1
fi
if ! strings "$OUTPUT_DIR/firmware.bin" | grep -q RRCAT3; then
    echo "RRCAT3 is not embedded in firmware.bin" >&2
    exit 1
fi

{
    echo "Product: UV-K5 V1 Radio Remote CAT V3 development firmware"
    echo "Version: RRCAT3"
    echo "Target: DP32G030 + BK4819"
    echo "Source commit: $(git -C "$SCRIPT_DIR" rev-parse HEAD)"
    if [[ -n "$(git -C "$SCRIPT_DIR" status --porcelain)" ]]; then
        echo "Source worktree: dirty"
    else
        echo "Source worktree: clean"
    fi
    echo "Build method: clean Docker make build"
    echo "firmware.bin bytes: $FIRMWARE_SIZE"
    echo "Created UTC: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
} > "$OUTPUT_DIR/BUILDINFO.txt"

(
    cd "$OUTPUT_DIR"
    shasum -a 256 firmware.bin firmware.packed.bin BUILDINFO.txt > SHA256SUMS
    shasum -a 256 -c SHA256SUMS
)

echo "Development package created at $OUTPUT_DIR"
