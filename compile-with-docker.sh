#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)

echo "compile-with-docker.sh now creates a versioned release package." >&2
exec "$SCRIPT_DIR/package-release.sh" "$@"
