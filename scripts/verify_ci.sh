#!/bin/bash
# verify_ci.sh - run local quality gates before pushing.
#
# Format checking uses the repository .clang-format file and the same
# clang-format 18.x toolchain version documented for contributors. Build and
# unit-test checks use the debug preset for the current platform.
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

to_unix_path() {
    local path="$1"
    if command -v wslpath >/dev/null 2>&1 && [[ "$path" =~ ^[A-Za-z]:\\ ]]; then
        wslpath -u "$path"
    elif command -v cygpath >/dev/null 2>&1 && [[ "$path" =~ ^[A-Za-z]:\\ ]]; then
        cygpath -u "$path"
    else
        printf '%s\n' "$path"
    fi
}

is_supported_clang_format() {
    local candidate="$1"
    local version_text
    local major
    version_text="$("$candidate" --version 2>/dev/null || true)"
    major="$(printf '%s\n' "$version_text" | sed -n 's/.*version \([0-9][0-9]*\).*/\1/p')"
    [ -n "$major" ] && [ "$major" -ge 18 ]
}

find_clang_format() {
    local candidate
    local python
    local user_base
    local user_base_unix
    local vs_install
    local vs_install_unix
    local candidates=()

    if [ -n "${CORRIDORKEY_CLANG_FORMAT:-}" ]; then
        candidates+=("$(to_unix_path "$CORRIDORKEY_CLANG_FORMAT")")
    fi

    for candidate in clang-format clang-format-18; do
        if command -v "$candidate" >/dev/null 2>&1; then
            candidates+=("$(command -v "$candidate")")
        fi
    done

    for python in python3 python; do
        if command -v "$python" >/dev/null 2>&1; then
            user_base="$("$python" -m site --user-base 2>/dev/null || true)"
            if [ -n "$user_base" ]; then
                user_base_unix="$(to_unix_path "$user_base")"
                candidates+=("${user_base_unix}/Scripts/clang-format.exe")
                candidates+=("${user_base_unix}/bin/clang-format")
            fi
        fi
    done

    if command -v py >/dev/null 2>&1; then
        user_base="$(py -3 -m site --user-base 2>/dev/null || true)"
        if [ -n "$user_base" ]; then
            user_base_unix="$(to_unix_path "$user_base")"
            candidates+=("${user_base_unix}/Scripts/clang-format.exe")
        fi
    fi

    shopt -s nullglob
    candidates+=("$HOME"/AppData/Local/Programs/Python/Python*/Scripts/clang-format.exe)
    candidates+=("$HOME"/AppData/Roaming/Python/Python*/Scripts/clang-format.exe)
    candidates+=("$HOME"/AppData/Local/Packages/PythonSoftwareFoundation.Python.*/LocalCache/local-packages/Python*/Scripts/clang-format.exe)
    candidates+=(/mnt/c/Users/*/AppData/Local/Programs/Python/Python*/Scripts/clang-format.exe)
    candidates+=(/mnt/c/Users/*/AppData/Roaming/Python/Python*/Scripts/clang-format.exe)
    candidates+=(/mnt/c/Users/*/AppData/Local/Packages/PythonSoftwareFoundation.Python.*/LocalCache/local-packages/Python*/Scripts/clang-format.exe)
    shopt -u nullglob

    candidates+=("/c/Program Files/LLVM/bin/clang-format.exe")
    candidates+=("/c/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/Llvm/x64/bin/clang-format.exe")
    candidates+=("/c/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/Llvm/bin/clang-format.exe")
    candidates+=("/mnt/c/Program Files/LLVM/bin/clang-format.exe")
    candidates+=("/mnt/c/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/Llvm/x64/bin/clang-format.exe")
    candidates+=("/mnt/c/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/Llvm/bin/clang-format.exe")

    if [ -x "/c/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe" ]; then
        while IFS= read -r vs_install; do
            [ -z "$vs_install" ] && continue
            vs_install_unix="$(to_unix_path "$vs_install")"
            candidates+=("${vs_install_unix}/VC/Tools/Llvm/x64/bin/clang-format.exe")
            candidates+=("${vs_install_unix}/VC/Tools/Llvm/bin/clang-format.exe")
        done < <("/c/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe" -products '*' -property installationPath 2>/dev/null || true)
    fi

    for candidate in "${candidates[@]}"; do
        if [ -x "$candidate" ] && is_supported_clang_format "$candidate"; then
            printf '%s\n' "$candidate"
            return 0
        fi
    done

    return 1
}

echo "==> [1/3] clang-format --dry-run --Werror"
CLANG_FORMAT="$(find_clang_format || true)"
if [ -z "$CLANG_FORMAT" ]; then
    echo "error: clang-format 18+ not found. Install via 'pip install clang-format==18.1.8' or set CORRIDORKEY_CLANG_FORMAT." >&2
    exit 1
fi
echo "    using: ${CLANG_FORMAT}"
"$CLANG_FORMAT" --version | sed 's/^/    version: /'
STYLE_PATH="${REPO_ROOT}/.clang-format"
if [[ "$CLANG_FORMAT" == *.exe ]] && command -v wslpath >/dev/null 2>&1; then
    STYLE_PATH="$(wslpath -w "$STYLE_PATH")"
fi
STYLE_ARG="--style=file:${STYLE_PATH}"
find src include tests \( -name "*.cpp" -o -name "*.hpp" \) -print0 \
    | xargs -0 "$CLANG_FORMAT" "$STYLE_ARG" --dry-run --Werror
echo "    OK"

if [ "$MODE" = "--format" ]; then
    echo "==> Format-only run; skipping build and tests."
    exit 0
fi

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

echo "==> [3/3] ctest --label-regex unit"
ctest --test-dir "${BUILD_DIR}" --output-on-failure --label-regex unit
echo "    OK"

echo "==> All local quality checks passed."
