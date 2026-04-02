#!/bin/bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
source "${ROOT_DIR}/scripts/model_artifact_checks.sh"
BUILD_DIR="${CORRIDORKEY_BUILD_DIR:-build/release-macos-portable}"
VERSION_HEADER="${ROOT_DIR}/${BUILD_DIR}/generated/include/corridorkey/version.hpp"
if [ ! -f "$VERSION_HEADER" ]; then
    VERSION_HEADER="${ROOT_DIR}/include/corridorkey/version.hpp"
fi
DEFAULT_VERSION="$(grep 'CORRIDORKEY_VERSION_STRING' "$VERSION_HEADER" | sed 's/.*"\(.*\)".*/\1/')"
VERSION="${CORRIDORKEY_VERSION:-${DEFAULT_VERSION}}"
REQUIRE_MLX_2048="${CORRIDORKEY_REQUIRE_MLX_2048:-1}"
BUNDLE_PATH="${CORRIDORKEY_OFX_BUNDLE:-${ROOT_DIR}/build/ofx_mac_pkg/CorridorKey.ofx.bundle}"

require_file() {
    local path="$1"
    if [ ! -f "$path" ]; then
        echo "Missing required file: $path" >&2
        exit 1
    fi
}

require_min_size() {
    local path="$1"
    local min_bytes="$2"
    local actual_bytes
    actual_bytes="$(stat -f%z "$path")"
    if [ "$actual_bytes" -lt "$min_bytes" ]; then
        echo "File too small: $path ($actual_bytes bytes, expected >= $min_bytes)" >&2
        exit 1
    fi
}

require_no_absolute_rpaths() {
    local binary="$1"
    local leaked_rpaths

    leaked_rpaths="$(otool -l "$binary" | awk '
        $1 == "cmd" && $2 == "LC_RPATH" { in_rpath = 1; next }
        in_rpath && $1 == "path" && $2 ~ /^\// { print $2; in_rpath = 0; next }
        in_rpath && $1 == "path" { in_rpath = 0 }
    ')"

    if [ -n "$leaked_rpaths" ]; then
        echo "Absolute LC_RPATH entries found in $binary" >&2
        echo "$leaked_rpaths" >&2
        exit 1
    fi
}

require_dependency() {
    local binary="$1"
    local expected="$2"

    if ! otool -L "$binary" | awk 'NR > 1 { print $1 }' | grep -Fxq "$expected"; then
        echo "Missing dependency $expected in $binary" >&2
        exit 1
    fi
}

require_no_appledouble_entries() {
    local root="$1"
    local leaked_entry

    leaked_entry="$(find "$root" \( -name '._*' -o -name '.__*' \) -print -quit)"
    if [ -n "$leaked_entry" ]; then
        echo "Unexpected AppleDouble entry found in bundle: $leaked_entry" >&2
        exit 1
    fi
}

if [ ! -d "$BUNDLE_PATH" ]; then
    echo "Bundle not found at $BUNDLE_PATH" >&2
    exit 1
fi

MACOS_DIR="${BUNDLE_PATH}/Contents/MacOS"
MODELS_DIR="${BUNDLE_PATH}/Contents/Resources/models"
BIN_DIR="${BUNDLE_PATH}/Contents/Resources/bin"
INFO_PLIST="${BUNDLE_PATH}/Contents/Info.plist"

PLUGIN_BINARY="${MACOS_DIR}/CorridorKey.ofx"
CLI_BINARY="${BIN_DIR}/corridorkey"
CORE_LIB="${MACOS_DIR}/libcorridorkey_core.dylib"
RUNTIME_LIB="${MACOS_DIR}/corridorkey_onnxruntime.dylib"
MLX_LIB="${MACOS_DIR}/libmlx.dylib"
MLX_METALLIB="${MACOS_DIR}/mlx.metallib"

require_file "$INFO_PLIST"
require_file "$PLUGIN_BINARY"
require_file "$CLI_BINARY"
require_file "$CORE_LIB"
require_file "$RUNTIME_LIB"
require_file "$MLX_LIB"
require_file "$MLX_METALLIB"
require_real_model_artifact "${MODELS_DIR}/corridorkey_mlx.safetensors" 300000000 "packaged MLX model pack"
require_real_model_artifact "${MODELS_DIR}/corridorkey_mlx_bridge_512.mlxfn" 200000000 "packaged MLX bridge 512"
require_real_model_artifact "${MODELS_DIR}/corridorkey_mlx_bridge_768.mlxfn" 200000000 "packaged MLX bridge 768"
require_real_model_artifact "${MODELS_DIR}/corridorkey_mlx_bridge_1024.mlxfn" 200000000 "packaged MLX bridge 1024"
require_real_model_artifact "${MODELS_DIR}/corridorkey_mlx_bridge_1536.mlxfn" 200000000 "packaged MLX bridge 1536"
if [ "$REQUIRE_MLX_2048" = "1" ]; then
    require_real_model_artifact "${MODELS_DIR}/corridorkey_mlx_bridge_2048.mlxfn" 200000000 "packaged MLX bridge 2048"
fi
require_real_model_artifact "${MODELS_DIR}/corridorkey_int8_512.onnx" 50000000 "packaged CPU baseline model"

require_min_size "$PLUGIN_BINARY" 100000
require_min_size "$CLI_BINARY" 100000
require_min_size "$CORE_LIB" 5000000
require_min_size "$RUNTIME_LIB" 5000000
require_min_size "$MLX_LIB" 5000000
require_min_size "$MLX_METALLIB" 50000000
require_no_appledouble_entries "$BUNDLE_PATH"

require_no_absolute_rpaths "$PLUGIN_BINARY"
require_no_absolute_rpaths "$CLI_BINARY"
require_no_absolute_rpaths "$CORE_LIB"
require_no_absolute_rpaths "$RUNTIME_LIB"
require_no_absolute_rpaths "$MLX_LIB"

require_dependency "$PLUGIN_BINARY" "@loader_path/libcorridorkey_core.dylib"
require_dependency "$CLI_BINARY" "@executable_path/../../MacOS/libcorridorkey_core.dylib"
require_dependency "$CORE_LIB" "@loader_path/corridorkey_onnxruntime.dylib"
require_dependency "$CORE_LIB" "@loader_path/libmlx.dylib"

codesign --verify --verbose=2 "$PLUGIN_BINARY"
codesign --verify --verbose=2 "$CLI_BINARY"
codesign --verify --verbose=2 "$CORE_LIB"
codesign --verify --verbose=2 "$RUNTIME_LIB"
codesign --verify --verbose=2 "$MLX_LIB"

echo "OFX bundle validation passed for version ${VERSION}"
