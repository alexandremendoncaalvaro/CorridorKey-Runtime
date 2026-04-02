#!/bin/bash
set -euo pipefail

PRESET="${1:-release-macos-portable}"
REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_ROOT"

source "$REPO_ROOT/scripts/model_artifact_checks.sh"
require_vcpkg_root

echo "[build] Configuring preset: $PRESET"
cmake --preset "$PRESET"

echo "[build] Building preset: $PRESET"
cmake --build --preset "$PRESET"

echo "[build] Build completed successfully."
