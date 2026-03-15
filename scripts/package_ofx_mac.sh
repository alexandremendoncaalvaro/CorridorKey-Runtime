#!/bin/bash
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"

VERSION="${CORRIDORKEY_VERSION:-0.1.7}"
BUILD_DIR="${CORRIDORKEY_BUILD_DIR:-${REPO_ROOT}/build/release-macos-portable}"
DIST_DIR="${REPO_ROOT}/dist/CorridorKey_Resolve_Mac_v${VERSION}"
WORK_DIR="${REPO_ROOT}/build/ofx_mac_pkg"
BUNDLE_NAME="CorridorKey.ofx.bundle"
BUNDLE_DIR="${WORK_DIR}/${BUNDLE_NAME}"
MACOS_DIR="${BUNDLE_DIR}/Contents/MacOS"
RESOURCES_DIR="${BUNDLE_DIR}/Contents/Resources"
MODELS_DIR="${RESOURCES_DIR}/models"
PKG_DIR="${WORK_DIR}/pkgs"
PKG_PATH="${DIST_DIR}/CorridorKey_Resolve_Mac_v${VERSION}.pkg"
DMG_PATH="${REPO_ROOT}/dist/CorridorKey_Resolve_Mac_v${VERSION}.dmg"
SIGN_IDENTITY="${CORRIDORKEY_SIGN_IDENTITY:-}"
INSTALLER_IDENTITY="${CORRIDORKEY_INSTALLER_IDENTITY:-}"
NOTARY_PROFILE="${CORRIDORKEY_NOTARY_PROFILE:-}"
ARCHIVE_FORMAT="${CORRIDORKEY_ARCHIVE_FORMAT:-dmg}"
RUNTIME_DIR="${REPO_ROOT}/vendor/onnxruntime/lib"
MLX_LIB="${CORRIDORKEY_MLX_LIB:-}"
MLX_METALLIB="${CORRIDORKEY_MLX_METALLIB:-}"
PLUGINS_SCRIPTS_DIR="${REPO_ROOT}/scripts/ofx_pkg"

PLUGIN_BINARY="${BUILD_DIR}/src/plugins/ofx/CorridorKey.ofx"
CORE_LIB="${BUILD_DIR}/src/libcorridorkey_core.dylib"
CPU_BASELINE_MODEL="${REPO_ROOT}/models/corridorkey_int8_512.onnx"
MLX_REQUIRED_PACK="${REPO_ROOT}/models/corridorkey_mlx.safetensors"
MLX_REQUIRED_BRIDGE="${REPO_ROOT}/models/corridorkey_mlx_bridge_512.mlxfn"
MLX_HIGH_RES_BRIDGE="${REPO_ROOT}/models/corridorkey_mlx_bridge_1024.mlxfn"

resolve_real_path() {
    python3 - "$1" <<'PY'
from pathlib import Path
import sys

print(Path(sys.argv[1]).resolve())
PY
}

runtime_dependency_name() {
    otool -L "$1" | awk '
        NR > 1 && $1 ~ /^@rpath\/libonnxruntime/ {
            sub("^@rpath/", "", $1)
            print $1
            exit
        }
    '
}

mlx_dependency_path() {
    otool -L "$1" | awk '
        NR > 1 && $1 ~ /(^@rpath\/libmlx\.dylib$|\/libmlx\.dylib$|^@@HOMEBREW_PREFIX@@\/opt\/mlx\/lib\/libmlx\.dylib$)/ {
            print $1
            exit
        }
    '
}

rewrite_dependency() {
    local binary="$1"
    local old_path="$2"
    local new_path="$3"

    if otool -L "$binary" | awk 'NR > 1 { print $1 }' | grep -Fxq "$old_path"; then
        install_name_tool -change "$old_path" "$new_path" "$binary"
    fi
}

delete_absolute_rpaths() {
    local binary="$1"
    local leaked_rpath

    while IFS= read -r leaked_rpath; do
        [ -n "$leaked_rpath" ] || continue
        install_name_tool -delete_rpath "$leaked_rpath" "$binary"
    done < <(otool -l "$binary" | awk '
        $1 == "cmd" && $2 == "LC_RPATH" { in_rpath = 1; next }
        in_rpath && $1 == "path" && $2 ~ /^\// { print $2; in_rpath = 0; next }
        in_rpath && $1 == "path" { in_rpath = 0 }
    ')
}

sign_binary() {
    local binary="$1"

    if [ -n "$SIGN_IDENTITY" ]; then
        codesign --force --options runtime --sign "$SIGN_IDENTITY" "$binary"
    else
        codesign --force --sign - "$binary"
    fi
}

sign_resource() {
    local resource="$1"

    if [ -n "$SIGN_IDENTITY" ]; then
        codesign --force --sign "$SIGN_IDENTITY" "$resource"
    else
        codesign --force --sign - "$resource"
    fi
}

resolve_installer_identity() {
    if [ -n "$INSTALLER_IDENTITY" ]; then
        return 0
    fi

    local identities
    identities="$(security find-identity -v -p basic 2>/dev/null || true)"
    INSTALLER_IDENTITY="$(printf "%s\n" "$identities" | awk -F'\"' '
        $2 ~ /^Developer ID Installer:/ { print $2; exit }
    ')"
    if [ -z "$INSTALLER_IDENTITY" ]; then
        INSTALLER_IDENTITY="$(printf "%s\n" "$identities" | awk -F'\"' '
            $2 ~ /^Apple Distribution:/ { print $2; exit }
        ')"
    fi
}

find_portable_mlx_artifact() {
    local relative_path="$1"
    local bottle_name
    local artifact

    for bottle_name in sonoma sequoia tahoe; do
        artifact="$(find "${REPO_ROOT}/build/mlx-${bottle_name}-bottle" -type f -path "*/${relative_path}" | head -n 1)"
        if [ -n "${artifact:-}" ]; then
            echo "$artifact"
            return 0
        fi
    done

    return 1
}

require_file() {
    local path="$1"
    if [ ! -f "$path" ]; then
        echo "ERROR: Missing required file: $path" >&2
        exit 1
    fi
}

if [ -f "$RUNTIME_DIR/libonnxruntime.dylib" ]; then
    RUNTIME_LIB="$RUNTIME_DIR/libonnxruntime.dylib"
else
    RUNTIME_LIB="$(find "$RUNTIME_DIR" -maxdepth 1 -type f -name 'libonnxruntime*.dylib' | head -n 1)"
fi

if [ -z "${RUNTIME_LIB:-}" ] || [ ! -f "$RUNTIME_LIB" ]; then
    echo "ERROR: Unable to locate vendored ONNX Runtime dylib in $RUNTIME_DIR" >&2
    exit 1
fi

if [ -z "$MLX_LIB" ]; then
    MLX_LIB="$(find_portable_mlx_artifact 'mlx/*/lib/libmlx.dylib' || true)"
fi

if [ -z "$MLX_LIB" ]; then
    MLX_LIB="$(find "${REPO_ROOT}/.venv-macos-mlx" -type f -name 'libmlx.dylib' | head -n 1)"
fi

if [ -z "${MLX_LIB:-}" ] || [ ! -f "$MLX_LIB" ]; then
    echo "ERROR: Unable to locate libmlx.dylib. Set CORRIDORKEY_MLX_LIB if MLX is installed elsewhere." >&2
    exit 1
fi

if [ -z "$MLX_METALLIB" ]; then
    MLX_METALLIB="$(find_portable_mlx_artifact 'mlx/*/lib/mlx.metallib' || true)"
fi

if [ -z "$MLX_METALLIB" ]; then
    MLX_METALLIB="$(find "${REPO_ROOT}/.venv-macos-mlx" -type f -name 'mlx.metallib' | head -n 1)"
fi

if [ -z "${MLX_METALLIB:-}" ] || [ ! -f "$MLX_METALLIB" ]; then
    echo "ERROR: Unable to locate mlx.metallib. Set CORRIDORKEY_MLX_METALLIB if MLX is installed elsewhere." >&2
    exit 1
fi

require_file "$PLUGIN_BINARY"
require_file "$CORE_LIB"
require_file "$CPU_BASELINE_MODEL"
require_file "$MLX_REQUIRED_PACK"
require_file "$MLX_REQUIRED_BRIDGE"
require_file "$MLX_HIGH_RES_BRIDGE"
require_file "${PLUGINS_SCRIPTS_DIR}/Distribution.xml"
require_file "${PLUGINS_SCRIPTS_DIR}/user/postinstall"
require_file "${PLUGINS_SCRIPTS_DIR}/clear_cache/postinstall"

RUNTIME_LIB_REAL="$(resolve_real_path "$RUNTIME_LIB")"
RUNTIME_LIB_NAME="$(basename "$RUNTIME_LIB_REAL")"
RUNTIME_LINK_NAME="corridorkey_onnxruntime.dylib"
CORE_LIB_NAME="$(basename "$CORE_LIB")"
MLX_LIB_NAME="$(basename "$MLX_LIB")"
MLX_METALLIB_NAME="$(basename "$MLX_METALLIB")"
PLUGIN_NAME="$(basename "$PLUGIN_BINARY")"

if [ -z "$SIGN_IDENTITY" ] && [ -x "${REPO_ROOT}/scripts/codesign-identity.sh" ]; then
    SIGN_IDENTITY="$(bash "${REPO_ROOT}/scripts/codesign-identity.sh" public)"
    if [ "$SIGN_IDENTITY" = "-" ]; then
        SIGN_IDENTITY=""
    fi
fi

resolve_installer_identity

echo "[1/6] Cleaning packaging workspace..."
rm -rf "$WORK_DIR"
rm -rf "$DIST_DIR"
mkdir -p "$MACOS_DIR" "$MODELS_DIR" "$PKG_DIR" "$DIST_DIR"

echo "[2/6] Copying bundle payload..."
cp "$PLUGIN_BINARY" "$MACOS_DIR/$PLUGIN_NAME"
cp "$CORE_LIB" "$MACOS_DIR/$CORE_LIB_NAME"
cp "$MLX_LIB" "$MACOS_DIR/$MLX_LIB_NAME"
cp "$MLX_METALLIB" "$MACOS_DIR/$MLX_METALLIB_NAME"
cp "$RUNTIME_LIB_REAL" "$MACOS_DIR/$RUNTIME_LINK_NAME"

chmod u+w "$MACOS_DIR/$MLX_METALLIB_NAME"

cp "$CPU_BASELINE_MODEL" "$MODELS_DIR/"
cp "$MLX_REQUIRED_PACK" "$MODELS_DIR/"
cp "$MLX_REQUIRED_BRIDGE" "$MODELS_DIR/"
cp "$MLX_HIGH_RES_BRIDGE" "$MODELS_DIR/"

cat <<PLIST_EOF > "${BUNDLE_DIR}/Contents/Info.plist"
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleDevelopmentRegion</key>
    <string>en</string>
    <key>CFBundleInfoDictionaryVersion</key>
    <string>6.0</string>
    <key>CFBundleExecutable</key>
    <string>${PLUGIN_NAME}</string>
    <key>CFBundleIdentifier</key>
    <string>com.corridorkey.resolve</string>
    <key>CFBundlePackageType</key>
    <string>BNDL</string>
    <key>CFBundleShortVersionString</key>
    <string>${VERSION}</string>
    <key>CFBundleVersion</key>
    <string>${VERSION}</string>
</dict>
</plist>
PLIST_EOF

echo "[3/6] Fixing library paths for portability..."
install_name_tool -id "@rpath/${RUNTIME_LINK_NAME}" "$MACOS_DIR/$RUNTIME_LINK_NAME"

rewrite_dependency "$MACOS_DIR/$PLUGIN_NAME" "@rpath/libcorridorkey_core.dylib" \
    "@loader_path/$CORE_LIB_NAME"

CORE_RUNTIME_DEPENDENCY="$(runtime_dependency_name "$MACOS_DIR/$CORE_LIB_NAME")"
if [ -n "${CORE_RUNTIME_DEPENDENCY:-}" ]; then
    rewrite_dependency "$MACOS_DIR/$CORE_LIB_NAME" "@rpath/$CORE_RUNTIME_DEPENDENCY" \
        "@loader_path/$RUNTIME_LINK_NAME"
fi
rewrite_dependency "$MACOS_DIR/$CORE_LIB_NAME" "@rpath/libonnxruntime.dylib" \
    "@loader_path/$RUNTIME_LINK_NAME"

CORE_MLX_DEPENDENCY="$(mlx_dependency_path "$MACOS_DIR/$CORE_LIB_NAME")"
if [ -n "${CORE_MLX_DEPENDENCY:-}" ]; then
    rewrite_dependency "$MACOS_DIR/$CORE_LIB_NAME" "$CORE_MLX_DEPENDENCY" \
        "@loader_path/$MLX_LIB_NAME"
fi
rewrite_dependency "$MACOS_DIR/$CORE_LIB_NAME" "@rpath/libmlx.dylib" "@loader_path/$MLX_LIB_NAME"

delete_absolute_rpaths "$MACOS_DIR/$PLUGIN_NAME"
delete_absolute_rpaths "$MACOS_DIR/$CORE_LIB_NAME"
delete_absolute_rpaths "$MACOS_DIR/$RUNTIME_LINK_NAME"
delete_absolute_rpaths "$MACOS_DIR/$MLX_LIB_NAME"

echo "[4/6] Signing bundle binaries..."
sign_binary "$MACOS_DIR/$RUNTIME_LINK_NAME"
sign_binary "$MACOS_DIR/$MLX_LIB_NAME"
sign_resource "$MACOS_DIR/$MLX_METALLIB_NAME"
sign_binary "$MACOS_DIR/$CORE_LIB_NAME"
sign_binary "$MACOS_DIR/$PLUGIN_NAME"

echo "[5/6] Building installer package..."
SYSTEM_ROOT="${WORK_DIR}/system_root"
USER_ROOT="${WORK_DIR}/user_root"
mkdir -p "$SYSTEM_ROOT" "$USER_ROOT"
ditto "$BUNDLE_DIR" "$SYSTEM_ROOT/$BUNDLE_NAME"
ditto "$BUNDLE_DIR" "$USER_ROOT/$BUNDLE_NAME"

pkgbuild --root "$SYSTEM_ROOT" \
    --identifier "com.corridorkey.ofx.system" \
    --version "$VERSION" \
    --install-location "/Library/OFX/Plugins" \
    "$PKG_DIR/corridorkey_ofx_system.pkg"

pkgbuild --root "$USER_ROOT" \
    --identifier "com.corridorkey.ofx.user" \
    --version "$VERSION" \
    --install-location "/Users/Shared/CorridorKey" \
    --scripts "${PLUGINS_SCRIPTS_DIR}/user" \
    "$PKG_DIR/corridorkey_ofx_user.pkg"

pkgbuild --nopayload \
    --identifier "com.corridorkey.ofx.clear_cache" \
    --version "$VERSION" \
    --scripts "${PLUGINS_SCRIPTS_DIR}/clear_cache" \
    "$PKG_DIR/corridorkey_ofx_clear_cache.pkg"

DIST_XML="${WORK_DIR}/Distribution.xml"
sed "s/@VERSION@/${VERSION}/g" "${PLUGINS_SCRIPTS_DIR}/Distribution.xml" > "$DIST_XML"

if [ -n "$INSTALLER_IDENTITY" ]; then
    productbuild --distribution "$DIST_XML" --package-path "$PKG_DIR" \
        --sign "$INSTALLER_IDENTITY" "$PKG_PATH"
else
    productbuild --distribution "$DIST_XML" --package-path "$PKG_DIR" "$PKG_PATH"
fi

echo "[6/6] Creating archive..."
case "$ARCHIVE_FORMAT" in
    dmg)
        DMG_ROOT="${WORK_DIR}/dmg_root"
        rm -rf "$DMG_ROOT"
        mkdir -p "$DMG_ROOT"
        cp "$PKG_PATH" "$DMG_ROOT/"
        hdiutil create -volname "CorridorKey Resolve OFX" -srcfolder "$DMG_ROOT" \
            -ov -format UDZO "$DMG_PATH"
        if [ -n "$SIGN_IDENTITY" ]; then
            codesign --force --sign "$SIGN_IDENTITY" "$DMG_PATH"
        fi
        if [ -n "$NOTARY_PROFILE" ]; then
            xcrun notarytool submit "$DMG_PATH" --keychain-profile "$NOTARY_PROFILE" --wait
            xcrun stapler staple "$DMG_PATH"
            xcrun stapler validate "$DMG_PATH"
        fi
        echo "SUCCESS: DMG created at $DMG_PATH"
        ;;
    zip)
        ZIP_PATH="${REPO_ROOT}/dist/CorridorKey_Resolve_Mac_v${VERSION}.zip"
        rm -f "$ZIP_PATH"
        ditto -c -k --norsrc --keepParent "$DIST_DIR" "$ZIP_PATH"
        echo "SUCCESS: ZIP created at $ZIP_PATH"
        ;;
    *)
        echo "ERROR: Unsupported CORRIDORKEY_ARCHIVE_FORMAT '$ARCHIVE_FORMAT'. Use dmg or zip." >&2
        exit 1
        ;;
esac
