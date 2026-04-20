#!/bin/bash
# Canonical Linux entrypoint for CorridorKey builds and experimental releases.
#
# Mirrors scripts/windows.ps1 in shape: one wrapper for build, prepare-cuda,
# package-ofx, release, and sync-version. Lower-level scripts are internal
# delegates for debugging this wrapper and should not be treated as alternate
# release procedures.
#
# Usage:
#   scripts/linux.sh --task build [--preset release-linux-portable]
#   scripts/linux.sh --task prepare-cuda
#   scripts/linux.sh --task package-ofx --version X.Y.Z
#   scripts/linux.sh --task release --version X.Y.Z
#   scripts/linux.sh --task sync-version

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"

TASK="build"
PRESET="release-linux-portable"
VERSION=""

while [ $# -gt 0 ]; do
    case "$1" in
        --task|-Task)
            TASK="$2"
            shift 2
            ;;
        --preset|-Preset)
            PRESET="$2"
            shift 2
            ;;
        --version|-Version)
            VERSION="$2"
            shift 2
            ;;
        -h|--help)
            cat <<USAGE
Usage: $0 --task TASK [--preset NAME] [--version X.Y.Z]

Tasks:
  build          Configure and compile with --preset (default release-linux-portable).
  prepare-cuda   Stage the curated Linux ONNX Runtime GPU drop at
                 vendor/onnxruntime-linux-cuda/.
  package-ofx    Build the OFX bundle and assemble the .tar.gz, .deb, and .rpm
                 artifacts in dist/. Requires --version.
  release        Full pipeline: prepare-cuda (if needed) -> build -> tests ->
                 package-ofx -> validate. Requires --version.
  sync-version   Read CMakeLists.txt VERSION and update embedded GUI version
                 metadata. Safe to run any time.

Artifacts emitted by package-ofx and release:
  dist/CorridorKey_Resolve_vX.Y.Z_Linux_RTX.tar.gz
  dist/CorridorKey_Resolve_vX.Y.Z_Linux_RTX.deb
  dist/CorridorKey_Resolve_vX.Y.Z_Linux_RTX.rpm
USAGE
            exit 0
            ;;
        *)
            echo "Unknown argument: $1" >&2
            exit 2
            ;;
    esac
done

say() { echo "[linux] $*"; }

require_version() {
    if [ -z "${VERSION}" ]; then
        echo "ERROR: --version is required for task '${TASK}'" >&2
        exit 2
    fi
}

read_cmake_version() {
    awk '/^project\(/, /\)/' "${REPO_ROOT}/CMakeLists.txt" \
        | grep -E 'VERSION[[:space:]]+[0-9]+\.[0-9]+\.[0-9]+' \
        | head -n 1 \
        | awk '{print $2}'
}

task_prepare_cuda() {
    say "Staging curated Linux ONNX Runtime GPU drop..."
    bash "${REPO_ROOT}/scripts/sync_onnxruntime_linux_cuda.sh"
}

task_build() {
    if [ ! -d "${REPO_ROOT}/vendor/onnxruntime-linux-cuda" ]; then
        say "No curated Linux ONNX Runtime found; running prepare-cuda first."
        task_prepare_cuda
    fi
    say "Configuring preset ${PRESET}..."
    local configure_args=(--preset "${PRESET}")
    if [ -n "${VERSION}" ]; then
        configure_args+=(-DCORRIDORKEY_DISPLAY_VERSION_LABEL="${VERSION}")
    fi
    cmake "${configure_args[@]}"
    say "Building preset ${PRESET}..."
    cmake --build --preset "${PRESET}"
}

task_package_ofx() {
    require_version
    say "Packaging OFX artifacts for v${VERSION}..."
    bash "${REPO_ROOT}/scripts/package_ofx_installer_linux.sh" --version "${VERSION}"
}

task_release() {
    require_version
    task_prepare_cuda
    task_build
    say "Running unit tests..."
    ctest --test-dir "${REPO_ROOT}/build/${PRESET}" -L unit --output-on-failure
    task_package_ofx
    say "Validating packaged bundle..."
    bash "${REPO_ROOT}/scripts/validate_ofx_linux.sh" \
        "${REPO_ROOT}/dist/CorridorKey_Resolve_v${VERSION}_Linux_RTX/CorridorKey.ofx.bundle"
    say "Release pipeline complete."
    say "Artifacts:"
    (cd "${REPO_ROOT}" && ls -lh dist/CorridorKey_Resolve_v${VERSION}_Linux_RTX.{tar.gz,deb,rpm} 2>/dev/null || true)
}

task_sync_version() {
    local resolved="${VERSION}"
    if [ -z "${resolved}" ]; then
        resolved="$(read_cmake_version)"
    fi
    say "CMakeLists.txt VERSION = ${resolved}"
    # GUI metadata sync is shared with the macOS pipeline and lives in the
    # CMake build step itself via the generated version.hpp; no extra action
    # needed here for the Linux track until a Tauri Linux bundle is added.
}

case "${TASK}" in
    build) task_build ;;
    prepare-cuda) task_prepare_cuda ;;
    package-ofx) task_package_ofx ;;
    release) task_release ;;
    sync-version) task_sync_version ;;
    *)
        echo "Unknown task: ${TASK}" >&2
        exit 2
        ;;
esac
