#!/bin/bash
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_ROOT"

SKIP_TESTS=0
CLEAN_ONLY=0

for arg in "$@"; do
    case "$arg" in
        --skip-tests) SKIP_TESTS=1 ;;
        --clean-only) CLEAN_ONLY=1 ;;
        *) echo "Unknown argument: $arg" >&2; exit 1 ;;
    esac
done

step() {
    echo ""
    echo "=== [STEP] $1 ==="
}

success() {
    echo "[SUCCESS] $1"
}

# 1. Sanitize Environment
step "Sanitizing Environment"
for dir in build/release-macos-portable dist; do
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

# 5. Quality Gate 3: Package OFX
step "Quality Gate: Packaging OFX Installer"
bash "$REPO_ROOT/scripts/package_ofx_mac.sh"
success "OFX installer packaged and validated."

# 6. Final Summary
step "Release is READY"
echo ""
echo "Artifacts:"
if [ -d dist ]; then
    find dist -type f \( -name '*.dmg' -o -name '*.zip' -o -name '*.pkg' \) -exec ls -lh {} \; | \
        awk '{ printf "  %-12s %s\n", $5, $NF }'
fi
