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
#   scripts/linux.sh --task release --version X.Y.Z \
#       [--display-label X.Y.Z-linux.N] \
#       [--publish-github [--notes-file PATH] [--github-repo OWNER/REPO]]
#   scripts/linux.sh --task sync-version

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"

TASK="build"
PRESET="release-linux-portable"
VERSION=""
DISPLAY_LABEL=""
PUBLISH_GITHUB=0
NOTES_FILE=""
GITHUB_REPO="alexandremendoncaalvaro/CorridorKey-Runtime"

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
        --display-label)
            DISPLAY_LABEL="$2"
            shift 2
            ;;
        --publish-github)
            PUBLISH_GITHUB=1
            shift
            ;;
        --notes-file)
            NOTES_FILE="$2"
            shift 2
            ;;
        --github-repo)
            GITHUB_REPO="$2"
            shift 2
            ;;
        -h|--help)
            cat <<USAGE
Usage: $0 --task TASK [--preset NAME] [--version X.Y.Z]
       [--display-label X.Y.Z-linux.N]
       [--publish-github [--notes-file PATH] [--github-repo OWNER/REPO]]

Tasks:
  build          Configure and compile with --preset (default release-linux-portable).
  prepare-cuda   Stage the curated Linux ONNX Runtime GPU drop at
                 vendor/onnxruntime-linux-cuda/.
  package-ofx    Build the OFX bundle and assemble the .deb and .rpm
                 installers in dist/. Requires --version.
  release        Full pipeline: prepare-cuda (if needed) -> build -> tests ->
                 package-ofx -> validate. Requires --version.
                 When --display-label X.Y.Z-linux.N is set, that label is
                 baked into version.hpp and the artifact filenames. When
                 --publish-github is also set the release is handed to
                 scripts/publish_github_release.sh, which requires a notes
                 file at build/release_notes/v<label>.md by default.
  sync-version   Read CMakeLists.txt VERSION and update embedded GUI version
                 metadata. Safe to run any time.

Artifacts emitted by package-ofx and release:
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
    cmake --preset "${PRESET}"
    say "Building preset ${PRESET}..."
    cmake --build --preset "${PRESET}"
}

task_package_ofx() {
    require_version
    local pkg_version="${VERSION}"
    if [ -n "${DISPLAY_LABEL}" ]; then
        pkg_version="${DISPLAY_LABEL}"
    fi
    say "Packaging OFX artifacts for v${pkg_version}..."
    bash "${REPO_ROOT}/scripts/package_ofx_installer_linux.sh" --version "${pkg_version}"
}

validate_linux_display_label() {
    if [ -z "${DISPLAY_LABEL}" ]; then
        return 0
    fi
    if ! [[ "${DISPLAY_LABEL}" =~ ^([0-9]+\.[0-9]+\.[0-9]+)-linux\.([0-9]+)$ ]]; then
        echo "ERROR: --display-label '${DISPLAY_LABEL}' is not a valid Linux prerelease label." >&2
        echo "       Expected form: X.Y.Z-linux.N (see docs/RELEASE_GUIDELINES.md section 1)." >&2
        exit 2
    fi
    local label_core="${BASH_REMATCH[1]}"
    if [ "${label_core}" != "${VERSION}" ]; then
        echo "ERROR: --display-label core '${label_core}' does not match --version '${VERSION}'." >&2
        echo "       The label must be '${VERSION}-linux.<counter>'." >&2
        exit 2
    fi
}

task_release() {
    require_version
    validate_linux_display_label
    local resolved_notes_file="${NOTES_FILE}"
    if [ "${PUBLISH_GITHUB}" = "1" ]; then
        if [ -z "${DISPLAY_LABEL}" ]; then
            echo "ERROR: --publish-github requires --display-label X.Y.Z-linux.N." >&2
            exit 2
        fi
        if [ -z "${resolved_notes_file}" ]; then
            resolved_notes_file="${REPO_ROOT}/build/release_notes/v${DISPLAY_LABEL}.md"
        fi
        if [ ! -f "${resolved_notes_file}" ]; then
            echo "ERROR: release notes file not found at '${resolved_notes_file}'." >&2
            echo "       Write it per docs/RELEASE_GUIDELINES.md section 5 before rerunning." >&2
            exit 2
        fi
    fi

    # Carry the label through the packaging step so version.hpp, CLI
    # --version, and the .deb / .rpm filenames all agree.
    if [ -n "${DISPLAY_LABEL}" ]; then
        export CORRIDORKEY_DISPLAY_VERSION_LABEL="${DISPLAY_LABEL}"
    fi

    local artifact_tag="${VERSION}"
    if [ -n "${DISPLAY_LABEL}" ]; then
        artifact_tag="${DISPLAY_LABEL}"
    fi

    task_prepare_cuda
    task_build
    say "Running unit tests..."
    ctest --test-dir "${REPO_ROOT}/build/${PRESET}" -L unit --output-on-failure
    task_package_ofx
    say "Validating packaged bundle..."
    bash "${REPO_ROOT}/scripts/validate_ofx_linux.sh" \
        "${REPO_ROOT}/dist/CorridorKey_Resolve_v${artifact_tag}_Linux_RTX/CorridorKey.ofx.bundle"
    say "Release pipeline complete."
    say "Artifacts:"
    (cd "${REPO_ROOT}" && ls -lh dist/CorridorKey_Resolve_v${artifact_tag}_Linux_RTX.{deb,rpm} 2>/dev/null || true)

    if [ "${PUBLISH_GITHUB}" = "1" ]; then
        say "Publishing GitHub release..."
        bash "${REPO_ROOT}/scripts/publish_github_release.sh" \
            --platform linux \
            --version "${VERSION}" \
            --display-label "${DISPLAY_LABEL}" \
            --notes-file "${resolved_notes_file}" \
            --repo "${GITHUB_REPO}" \
            --asset "${REPO_ROOT}/dist/CorridorKey_Resolve_v${artifact_tag}_Linux_RTX.deb" \
            --asset "${REPO_ROOT}/dist/CorridorKey_Resolve_v${artifact_tag}_Linux_RTX.rpm"
        say "GitHub release published."
    fi
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
