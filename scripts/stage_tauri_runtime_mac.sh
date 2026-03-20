#!/bin/bash
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEFAULT_VERSION="$(grep 'CORRIDORKEY_VERSION_STRING' "${REPO_ROOT}/include/corridorkey/version.hpp" | sed 's/.*"\(.*\)".*/\1/')"
VERSION="${CORRIDORKEY_VERSION:-${DEFAULT_VERSION}}"
TAURI_RUNTIME_DIR="${REPO_ROOT}/src/gui/src-tauri/resources/runtime"

echo "[1/3] Building the portable macOS runtime bundle..."
"${REPO_ROOT}/scripts/package_mac.sh"

PORTABLE_BUNDLE_DIR="${REPO_ROOT}/dist/CorridorKey_Runtime_v${VERSION}_macOS_AppleSilicon"
if [ ! -d "$PORTABLE_BUNDLE_DIR" ]; then
    echo "ERROR: Expected portable runtime bundle at $PORTABLE_BUNDLE_DIR" >&2
    exit 1
fi

echo "[2/3] Staging runtime payload for the Tauri installer..."
rm -rf "$TAURI_RUNTIME_DIR"
mkdir -p "$TAURI_RUNTIME_DIR"

for item in corridorkey bin models; do
    if [ -e "${PORTABLE_BUNDLE_DIR}/${item}" ]; then
        cp -R "${PORTABLE_BUNDLE_DIR}/${item}" "${TAURI_RUNTIME_DIR}/"
    fi
done

echo "[3/3] Runtime payload staged for Tauri at: ${TAURI_RUNTIME_DIR}"
