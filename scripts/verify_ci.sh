#!/bin/bash
# verify_ci.sh — run the same checks CI runs, locally, before pushing.
#
# Mirrors .github/workflows/ci.yml: format check, configure with the same
# preset CI uses, build, and run unit tests. Intended to give developers a
# single "will CI pass?" command so format/build/test regressions do not
# ship to the runner.
#
# Usage:
#   scripts/verify_ci.sh              # all checks
#   scripts/verify_ci.sh --format     # format check only
#   scripts/verify_ci.sh --skip-tests # format + build, skip ctest
set -euo pipefail

REPO_ROOT="$(git rev-parse --show-toplevel)"
cd "$REPO_ROOT"

MODE="${1:-all}"

# VCPKG_ROOT is required by CMakePresets.json's toolchain file; fail early
# rather than letting CMake emit a confusing "Could not find" error later.
if [ "$MODE" != "--format" ] && [ -z "${VCPKG_ROOT:-}" ]; then
    echo "error: VCPKG_ROOT is not set. Export it to your vcpkg checkout before running this script." >&2
    exit 1
fi

# 1. Format check — matches CI's format job exactly.
echo "==> [1/3] clang-format --dry-run --Werror"
if ! command -v clang-format >/dev/null 2>&1; then
    echo "error: clang-format not on PATH. Install via 'pip install clang-format==18.1.8'" >&2
    exit 1
fi
find src include tests \( -name "*.cpp" -o -name "*.hpp" \) -print0 \
    | xargs -0 clang-format --dry-run --Werror
echo "    OK"

if [ "$MODE" = "--format" ]; then
    echo "==> Format-only run; skipping build and tests."
    exit 0
fi

# 2. Configure + build with the same preset CI uses for this platform.
if [ "$(uname -s)" = "Darwin" ]; then
    PRESET="debug-macos-portable"
else
    PRESET="debug"
fi
BUILD_DIR="build/${PRESET}"

echo "==> [2/3] cmake --preset ${PRESET} && cmake --build"
cmake --preset "${PRESET}"
cmake --build --preset "${PRESET}"
echo "    OK"

if [ "$MODE" = "--skip-tests" ]; then
    echo "==> Skipping tests (--skip-tests)."
    exit 0
fi

# 3. Run the same ctest selector CI uses.
echo "==> [3/3] ctest --label-regex unit"
ctest --test-dir "${BUILD_DIR}" --output-on-failure --label-regex unit
echo "    OK"

echo "==> All CI-equivalent checks passed."
