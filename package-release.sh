#!/usr/bin/env bash
# Modified for the Radio Remote fork: clean-source releases and verified metadata.
set -euo pipefail

usage() {
  echo "Usage: $0 <version> <release-notes-file>" >&2
}

if [[ $# -ne 2 ]]; then
  usage
  exit 2
fi

VERSION=$1
NOTES_FILE=$2

if [[ ! "$VERSION" =~ ^[A-Za-z0-9][A-Za-z0-9._-]*$ ]] || (( ${#VERSION} > 7 )); then
  echo "Invalid version '$VERSION': use 1-7 characters from A-Z, a-z, 0-9, '.', '_' or '-'." >&2
  exit 2
fi

if [[ ! -f "$NOTES_FILE" ]]; then
  echo "Release notes file not found: $NOTES_FILE" >&2
  exit 2
fi

ROOT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
RELEASES_DIR="$ROOT_DIR/releases"
PACKAGE_NAME="uvk5-v1-$VERSION"
FINAL_DIR="$RELEASES_DIR/$PACKAGE_NAME"
FINAL_ZIP="$RELEASES_DIR/$PACKAGE_NAME.zip"

if [[ -e "$FINAL_DIR" || -e "$FINAL_ZIP" ]]; then
  echo "Release already exists: $PACKAGE_NAME" >&2
  exit 3
fi

SOURCE_COMMIT=$(git -C "$ROOT_DIR" rev-parse HEAD)
if [[ -n "$(git -C "$ROOT_DIR" status --porcelain --untracked-files=normal)" ]]; then
  echo "Release builds require a clean working tree; commit or move local changes first." >&2
  exit 4
fi
SOURCE_STATE="clean working tree"

mkdir -p "$RELEASES_DIR"
TEMP_ROOT=$(mktemp -d "$RELEASES_DIR/.${PACKAGE_NAME}.XXXXXX")
PACKAGE_DIR="$TEMP_ROOT/$PACKAGE_NAME"
mkdir -p "$PACKAGE_DIR/THIRD_PARTY_LICENSES"

cleanup() {
  rm -rf "$TEMP_ROOT"
}
trap cleanup EXIT

BUILD_TIME=$(TZ=Asia/Shanghai date '+%Y-%m-%d %H:%M:%S %Z (Asia/Shanghai)')

docker build -t uvk5 "$ROOT_DIR"
BUILD_IMAGE=$(docker image inspect --format '{{.Id}}' uvk5)
docker run --rm \
  -v "$PACKAGE_DIR:/output" \
  uvk5 /bin/bash -c \
  "cd /app && make AUTHOR_STRING=EGZUMER VERSION_STRING='$VERSION' clean && make AUTHOR_STRING=EGZUMER VERSION_STRING='$VERSION' && cp firmware.bin firmware.packed.bin /output/ && arm-none-eabi-gcc --version | sed -n '1p' > /output/COMPILER.txt && arm-none-eabi-size firmware > /output/SIZE.txt"

RAW_SIZE=$(wc -c < "$PACKAGE_DIR/firmware.bin" | tr -d ' ')
PACKED_SIZE=$(wc -c < "$PACKAGE_DIR/firmware.packed.bin" | tr -d ' ')
if (( RAW_SIZE > 61440 )); then
  echo "Firmware is too large: $RAW_SIZE / 61440 bytes" >&2
  exit 4
fi

if ! grep -aFq "$VERSION" "$PACKAGE_DIR/firmware.bin"; then
  echo "Embedded version check failed: $VERSION" >&2
  exit 4
fi

COMPILER=$(sed -n '1p' "$PACKAGE_DIR/COMPILER.txt")
rm "$PACKAGE_DIR/COMPILER.txt" "$PACKAGE_DIR/SIZE.txt"

docker run --rm "uvk5" cat /opt/toolchain/license.txt > "$PACKAGE_DIR/THIRD_PARTY_LICENSES/ARM-GNU-Toolchain.txt"
cp "$NOTES_FILE" "$PACKAGE_DIR/RELEASE_NOTES.md"
cp "$ROOT_DIR/LICENSE" "$PACKAGE_DIR/LICENSE"
cp "$ROOT_DIR/NOTICE" "$PACKAGE_DIR/NOTICE"
cp "$ROOT_DIR/external/CMSIS_5/LICENSE.txt" "$PACKAGE_DIR/THIRD_PARTY_LICENSES/CMSIS-Apache-2.0.txt"
cp "$ROOT_DIR/external/printf/LICENSE" "$PACKAGE_DIR/THIRD_PARTY_LICENSES/printf-MIT.txt"

cat > "$PACKAGE_DIR/BUILDINFO.txt" <<EOF
Package: $PACKAGE_NAME
Target: UV-K5 V1 / DP32G030 / BK4819
Version: $VERSION
Source commit: $SOURCE_COMMIT
Source state: $SOURCE_STATE
Build time: $BUILD_TIME
Build environment: isolated Docker image built from the repository Dockerfile
Build image: $BUILD_IMAGE
Build command: make AUTHOR_STRING=EGZUMER VERSION_STRING=$VERSION clean && make AUTHOR_STRING=EGZUMER VERSION_STRING=$VERSION
Compiler: $COMPILER
Raw image: firmware.bin, $RAW_SIZE bytes
Packed image: firmware.packed.bin, $PACKED_SIZE bytes
Image limit check: $RAW_SIZE / 61440 bytes (60 KiB), PASS
Embedded version check: EGZUMER $VERSION, PASS
License package check: ARM GNU toolchain license, Apache-2.0, NOTICE, CMSIS and printf license texts included, PASS
EOF

(
  cd "$PACKAGE_DIR"
  find . -type f ! -name SHA256SUMS -print | LC_ALL=C sort | while IFS= read -r file; do
    shasum -a 256 "$file"
  done > SHA256SUMS
  shasum -a 256 -c SHA256SUMS
)

(
  cd "$TEMP_ROOT"
  zip -qr "$PACKAGE_NAME.zip" "$PACKAGE_NAME"
  unzip -t "$PACKAGE_NAME.zip" >/dev/null
)

mv "$PACKAGE_DIR" "$FINAL_DIR"
mv "$TEMP_ROOT/$PACKAGE_NAME.zip" "$FINAL_ZIP"

echo "Release created:"
echo "  $FINAL_DIR"
echo "  $FINAL_ZIP"
