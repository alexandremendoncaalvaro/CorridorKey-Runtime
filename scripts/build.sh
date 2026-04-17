#!/bin/bash
set -euo pipefail

PRESET="${1:-release-macos-portable}"
REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_ROOT"

source "$REPO_ROOT/scripts/model_artifact_checks.sh"
require_vcpkg_root

# CORRIDORKEY_DISPLAY_VERSION_LABEL is the optimization-checkpoint label (e.g.
# "0.7.5-2"). Passing it through CMake configure bakes the label into
# version.hpp so the CLI and the runtime-server log file both report the slice
# under test. When unset, PROJECT_VERSION is used (same behavior as the
# Windows build wrapper).
configure_args=("--preset" "$PRESET")
if [ -n "${CORRIDORKEY_DISPLAY_VERSION_LABEL:-}" ]; then
    configure_args+=("-DCORRIDORKEY_DISPLAY_VERSION_LABEL=${CORRIDORKEY_DISPLAY_VERSION_LABEL}")
    echo "[build] Using display version label: ${CORRIDORKEY_DISPLAY_VERSION_LABEL}"
fi

echo "[build] Configuring preset: $PRESET"
cmake "${configure_args[@]}"

echo "[build] Building preset: $PRESET"
cmake --build --preset "$PRESET"

echo "[build] Build completed successfully."
