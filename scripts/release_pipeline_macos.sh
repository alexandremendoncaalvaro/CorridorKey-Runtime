#!/bin/bash
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_ROOT"

# Resolve and export VCPKG_ROOT so direct `cmake --preset` calls in this
# script (not just child scripts that source the helper themselves) can
# locate the vcpkg toolchain.
source "$REPO_ROOT/scripts/model_artifact_checks.sh"
require_vcpkg_root

SKIP_TESTS=0
CLEAN_ONLY=0
DISPLAY_LABEL=""
PUBLISH_GITHUB=0
NOTES_FILE=""
GITHUB_REPO="alexandremendoncaalvaro/CorridorKey-Runtime"

usage() {
    cat <<USAGE
Usage: $0 [options]

Options:
  --skip-tests               Skip the unit and regression test gate.
  --clean-only               Sanitize the environment and exit.
  --display-label LABEL      Bake LABEL into version.hpp, CLI --version, OFX
                             UI, and artifact filenames. Must be of the form
                             X.Y.Z-mac.N. Required when --publish-github is
                             set so the resulting DMG is named
                             CorridorKey_OFX_v<LABEL>_macOS_AppleSilicon.dmg.
  --publish-github           After a successful build and package step, hand
                             the DMG to scripts/publish_github_release.sh.
                             Requires --display-label and --notes-file.
  --notes-file PATH          Path to the release notes file (default:
                             build/release_notes/v<label>.md). See
                             docs/RELEASE_GUIDELINES.md section 5.
  --github-repo OWNER/REPO   Override the default publish target.
  -h, --help                 Show this help.
USAGE
}

while [ $# -gt 0 ]; do
    case "$1" in
        --skip-tests) SKIP_TESTS=1; shift ;;
        --clean-only) CLEAN_ONLY=1; shift ;;
        --display-label) DISPLAY_LABEL="$2"; shift 2 ;;
        --publish-github) PUBLISH_GITHUB=1; shift ;;
        --notes-file) NOTES_FILE="$2"; shift 2 ;;
        --github-repo) GITHUB_REPO="$2"; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *) echo "Unknown argument: $1" >&2; usage >&2; exit 2 ;;
    esac
done

step() {
    echo ""
    echo "=== [STEP] $1 ==="
}

success() {
    echo "[SUCCESS] $1"
}

# Read the CMake project version (X.Y.Z) as the single source of truth
# for publish validation. The display label, when set, must start with
# this core.
read_cmake_version() {
    awk '/^project\(/, /\)/' "${REPO_ROOT}/CMakeLists.txt" \
        | grep -E 'VERSION[[:space:]]+[0-9]+\.[0-9]+\.[0-9]+' \
        | head -n 1 \
        | awk '{print $2}'
}

CMAKE_VERSION="$(read_cmake_version)"

if [ -n "$DISPLAY_LABEL" ]; then
    if ! [[ "$DISPLAY_LABEL" =~ ^([0-9]+\.[0-9]+\.[0-9]+)-mac\.([0-9]+)$ ]]; then
        echo "ERROR: --display-label '$DISPLAY_LABEL' is not a valid macOS prerelease label." >&2
        echo "       Expected form: X.Y.Z-mac.N (see docs/RELEASE_GUIDELINES.md section 1)." >&2
        exit 2
    fi
    LABEL_CORE="${BASH_REMATCH[1]}"
    if [ "$LABEL_CORE" != "$CMAKE_VERSION" ]; then
        echo "ERROR: --display-label core '$LABEL_CORE' does not match CMakeLists.txt VERSION '$CMAKE_VERSION'." >&2
        echo "       Bump CMakeLists.txt first or pick a matching counter." >&2
        exit 2
    fi
fi

if [ "$PUBLISH_GITHUB" = "1" ]; then
    if [ -z "$DISPLAY_LABEL" ]; then
        echo "ERROR: --publish-github requires --display-label X.Y.Z-mac.N." >&2
        exit 2
    fi
    if [ -z "$NOTES_FILE" ]; then
        NOTES_FILE="${REPO_ROOT}/build/release_notes/v${DISPLAY_LABEL}.md"
    fi
    if [ ! -f "$NOTES_FILE" ]; then
        echo "ERROR: release notes file not found at '$NOTES_FILE'." >&2
        echo "       Write it per docs/RELEASE_GUIDELINES.md section 5 before rerunning." >&2
        exit 2
    fi
fi

# Carry the label through the child scripts so version.hpp, CLI --version,
# OFX UI, bundle Info.plist, and the DMG filename all agree.
if [ -n "$DISPLAY_LABEL" ]; then
    export CORRIDORKEY_DISPLAY_VERSION_LABEL="$DISPLAY_LABEL"
    export CORRIDORKEY_VERSION="$DISPLAY_LABEL"
fi

# 1. Sanitize Environment
step "Sanitizing Environment"
for dir in build/release-macos-portable build/debug-macos-portable dist; do
    if [ -d "$dir" ]; then
        echo "Cleaning $dir..."
        rm -rf "$dir"
    fi
done

LOG_DIR="$HOME/Library/Logs/CorridorKey"
if [ -d "$LOG_DIR" ]; then
    echo "Clearing system logs at $LOG_DIR..."
    rm -f "$LOG_DIR"/*.log 2>/dev/null || true
fi

if [ "$CLEAN_ONLY" = "1" ]; then
    success "Environment cleaned."
    exit 0
fi

# 2. Build
step "Building Project (Release Mode)"
bash "$REPO_ROOT/scripts/build.sh" release-macos-portable
success "Build completed successfully."

# 2.5. Validate Debug Configure
step "Validating Debug Configure (ASAN Preset)"
cmake --preset debug-macos-portable
success "Debug preset configured successfully."

# 3. Quality Gate 1: Tests
if [ "$SKIP_TESTS" = "0" ]; then
    step "Quality Gate: Running Automated Tests"
    cmake --build --preset release-macos-portable --target test_unit test_regression
    (cd build/release-macos-portable && ctest --output-on-failure)
    success "All tests passed."
fi

# 4. Quality Gate 2: Package CLI
step "Quality Gate: Packaging CLI Bundle"
bash "$REPO_ROOT/scripts/package_mac.sh"
success "CLI bundle packaged and validated."

# 5. Quality Gate 3: Validate CLI Release
step "Quality Gate: Validating CLI Release Bundle"
bash "$REPO_ROOT/scripts/validate_mac_release.sh"
success "CLI release bundle validation completed."

# 6. Quality Gate 4: Package OFX
step "Quality Gate: Packaging OFX Installer"
bash "$REPO_ROOT/scripts/package_ofx_mac.sh"
success "OFX installer packaged and validated."

# 7. Final Summary
step "Release is READY"
echo ""
echo "Artifacts:"
if [ -d dist ]; then
    find dist -type f \( -name '*.dmg' -o -name '*.zip' -o -name '*.pkg' \) -exec ls -lh {} \; | \
        awk '{ printf "  %-12s %s\n", $5, $NF }'
fi

# 8. Optional Publish
if [ "$PUBLISH_GITHUB" = "1" ]; then
    step "Publishing GitHub Release"
    DMG_PATH="${REPO_ROOT}/dist/CorridorKey_OFX_v${DISPLAY_LABEL}_macOS_AppleSilicon.dmg"
    bash "$REPO_ROOT/scripts/publish_github_release.sh" \
        --platform mac \
        --version "$CMAKE_VERSION" \
        --display-label "$DISPLAY_LABEL" \
        --notes-file "$NOTES_FILE" \
        --repo "$GITHUB_REPO" \
        --asset "$DMG_PATH"
    success "GitHub release published."
else
    echo ""
    echo "[INFO] --publish-github not set; skipping GitHub release publish."
    echo "       To publish: rerun with --publish-github --display-label X.Y.Z-mac.N"
    echo "       after writing build/release_notes/v<label>.md."
fi
