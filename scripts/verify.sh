#!/bin/bash
set -e

echo "--- Pre-flight Quality Check (SonarQube Local) ---"

# 1. Format Check
if command -v clang-format &> /dev/null; then
    echo "[1/3] Checking formatting..."
    find src include tests -name "*.cpp" -o -name "*.hpp" | xargs clang-format --dry-run --Werror
else
    echo "[1/3] Skipping format check (clang-format not found)"
fi

# 2. Static Analysis
if command -v clang-tidy &> /dev/null; then
    echo "[2/3] Running static analysis..."
    # Note: Requires compile_commands.json from CMake
    find src -name "*.cpp" | xargs clang-tidy -p build/debug --quiet
else
    echo "[2/3] Skipping static analysis (clang-tidy not found)"
fi

# 3. Unit Tests
if [ -d "build/debug" ]; then
    echo "[3/3] Running unit tests..."
    ctest --test-dir build/debug --label-regex unit --output-on-failure
else
    echo "[3/3] Skipping tests (build directory not found)"
fi

echo "--- Quality Check Passed ---"
