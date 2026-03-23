#!/bin/bash
set -euo pipefail

PRESET="${1:-release-macos-portable}"
REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_ROOT"

echo "[build] Configuring preset: $PRESET"
cmake --preset "$PRESET"
if [ $? -ne 0 ]; then
    echo "[build] CMake configure failed for preset '$PRESET'." >&2
    exit 1
fi

echo "[build] Building preset: $PRESET"
cmake --build --preset "$PRESET"
if [ $? -ne 0 ]; then
    echo "[build] CMake build failed for preset '$PRESET'." >&2
    exit 1
fi

echo "[build] Build completed successfully."
