#!/bin/bash
set -euo pipefail

VERSION="${CORRIDORKEY_VERSION:-0.1.3}"
DIST_DIR="dist/CorridorKey_Mac_v${VERSION}"
BIN_NAME="corridorkey"
BUILD_DIR="${CORRIDORKEY_BUILD_DIR:-build/release-macos-portable}"
SIGN_IDENTITY="${CORRIDORKEY_SIGN_IDENTITY:-}"
NOTARY_PROFILE="${CORRIDORKEY_NOTARY_PROFILE:-}"
ARCHIVE_FORMAT="${CORRIDORKEY_ARCHIVE_FORMAT:-zip}"
PUBLIC_RELEASE="${CORRIDORKEY_PUBLIC_RELEASE:-0}"
RUNTIME_DIR="vendor/onnxruntime/lib"
CORE_LIB="${BUILD_DIR}/src/libcorridorkey_core.dylib"
MLX_LIB="${CORRIDORKEY_MLX_LIB:-}"
MLX_METALLIB="${CORRIDORKEY_MLX_METALLIB:-}"
MLX_REQUIRED_PACK="models/corridorkey_mlx.safetensors"
MLX_REQUIRED_BRIDGE="models/corridorkey_mlx_bridge_512.mlxfn"
MLX_HIGH_RES_BRIDGE="models/corridorkey_mlx_bridge_1024.mlxfn"
CPU_BASELINE_MODEL="models/corridorkey_int8_512.onnx"
ZIP_PATH="dist/CorridorKey_Mac_v${VERSION}.zip"
DMG_PATH="dist/CorridorKey_Mac_v${VERSION}.dmg"
REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
IDENTITY_HELPER="${REPO_ROOT}/scripts/codesign-identity.sh"

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

require_public_release_prereqs() {
    if [ "$PUBLIC_RELEASE" != "1" ]; then
        return 0
    fi

    if [ "$ARCHIVE_FORMAT" != "dmg" ]; then
        echo "ERROR: Public macOS releases must use CORRIDORKEY_ARCHIVE_FORMAT=dmg."
        exit 1
    fi

    if [ -z "$SIGN_IDENTITY" ]; then
        echo "ERROR: Public macOS releases require a Developer ID Application signing identity."
        exit 1
    fi

    if [[ "$SIGN_IDENTITY" != Developer\ ID\ Application:* ]]; then
        echo "ERROR: Public macOS releases require a 'Developer ID Application' identity."
        echo "Current identity: $SIGN_IDENTITY"
        exit 1
    fi

    if [ -z "$NOTARY_PROFILE" ]; then
        echo "ERROR: Public macOS releases require CORRIDORKEY_NOTARY_PROFILE for notarization."
        exit 1
    fi
}

create_zip_archive() {
    rm -f "$ZIP_PATH"
    COPYFILE_DISABLE=1 ditto -c -k --norsrc --keepParent "$DIST_DIR" "$ZIP_PATH"
}

create_dmg_archive() {
    rm -f "$DMG_PATH"
    hdiutil create -volname "CorridorKey Runtime v${VERSION}" \
        -srcfolder "$DIST_DIR" \
        -ov -format UDZO \
        "$DMG_PATH" >/tmp/corridorkey_package_dmg.log

    if [ -n "$SIGN_IDENTITY" ]; then
        codesign --force --sign "$SIGN_IDENTITY" "$DMG_PATH"
    fi
}

notarize_archive() {
    local archive_path="$1"

    xcrun notarytool submit "$archive_path" --keychain-profile "$NOTARY_PROFILE" --wait

    if [[ "$archive_path" == *.dmg ]]; then
        xcrun stapler staple "$archive_path"
        xcrun stapler validate "$archive_path"
    fi
}

find_portable_mlx_artifact() {
    local relative_path="$1"
    local bottle_name
    local artifact

    for bottle_name in sonoma sequoia tahoe; do
        artifact="$(find "build/mlx-${bottle_name}-bottle" -type f -path "*/${relative_path}" | head -n 1)"
        if [ -n "${artifact:-}" ]; then
            echo "$artifact"
            return 0
        fi
    done

    return 1
}

if [ -f "$RUNTIME_DIR/libonnxruntime.dylib" ]; then
    RUNTIME_LIB="$RUNTIME_DIR/libonnxruntime.dylib"
else
    RUNTIME_LIB="$(find "$RUNTIME_DIR" -maxdepth 1 -type f -name 'libonnxruntime*.dylib' | head -n 1)"
fi

if [ -z "${RUNTIME_LIB:-}" ] || [ ! -f "$RUNTIME_LIB" ]; then
    echo "ERROR: Unable to locate vendored ONNX Runtime dylib in $RUNTIME_DIR"
    exit 1
fi

if [ ! -f "$CORE_LIB" ]; then
    echo "ERROR: Unable to locate libcorridorkey_core.dylib at $CORE_LIB"
    exit 1
fi

if [ ! -f "$CPU_BASELINE_MODEL" ]; then
    echo "ERROR: Missing required CPU baseline model: $CPU_BASELINE_MODEL"
    exit 1
fi

if [ ! -f "$MLX_REQUIRED_PACK" ]; then
    echo "ERROR: Missing required MLX pack: $MLX_REQUIRED_PACK"
    exit 1
fi

if [ ! -f "$MLX_REQUIRED_BRIDGE" ]; then
    echo "ERROR: Missing required MLX bridge: $MLX_REQUIRED_BRIDGE"
    exit 1
fi

if [ ! -f "$MLX_HIGH_RES_BRIDGE" ]; then
    echo "ERROR: Missing required high-resolution MLX bridge: $MLX_HIGH_RES_BRIDGE"
    exit 1
fi

if [ -z "$MLX_LIB" ]; then
    MLX_LIB="$(find_portable_mlx_artifact 'mlx/*/lib/libmlx.dylib' || true)"
fi

if [ -z "$MLX_LIB" ]; then
    MLX_LIB="$(find .venv-macos-mlx -type f -name 'libmlx.dylib' | head -n 1)"
fi

if [ -z "${MLX_LIB:-}" ] || [ ! -f "$MLX_LIB" ]; then
    echo "ERROR: Unable to locate libmlx.dylib. Set CORRIDORKEY_MLX_LIB if MLX is installed elsewhere."
    exit 1
fi

if [ -z "$MLX_METALLIB" ]; then
    MLX_METALLIB="$(find_portable_mlx_artifact 'mlx/*/lib/mlx.metallib' || true)"
fi

if [ -z "$MLX_METALLIB" ]; then
    MLX_METALLIB="$(find .venv-macos-mlx -type f -name 'mlx.metallib' | head -n 1)"
fi

if [ -z "${MLX_METALLIB:-}" ] || [ ! -f "$MLX_METALLIB" ]; then
    echo "ERROR: Unable to locate mlx.metallib. Set CORRIDORKEY_MLX_METALLIB if MLX is installed elsewhere."
    exit 1
fi

RUNTIME_LIB_REAL="$(resolve_real_path "$RUNTIME_LIB")"
RUNTIME_LIB_NAME="$(basename "$RUNTIME_LIB_REAL")"
RUNTIME_LINK_NAME="libonnxruntime.dylib"
CORE_LIB_NAME="$(basename "$CORE_LIB")"
MLX_LIB_NAME="$(basename "$MLX_LIB")"
MLX_METALLIB_NAME="$(basename "$MLX_METALLIB")"

if [ -z "$SIGN_IDENTITY" ] && [ -x "$IDENTITY_HELPER" ]; then
    if [ "$PUBLIC_RELEASE" = "1" ]; then
        SIGN_IDENTITY="$(bash "$IDENTITY_HELPER" public)"
    else
        SIGN_IDENTITY="$(bash "$IDENTITY_HELPER" release)"
    fi
    if [ "$SIGN_IDENTITY" = "-" ]; then
        SIGN_IDENTITY=""
    fi
fi

require_public_release_prereqs

echo "[1/5] Cleaning and creating dist directory..."
rm -rf "$DIST_DIR"
mkdir -p "$DIST_DIR/bin"
mkdir -p "$DIST_DIR/models"
mkdir -p "$DIST_DIR/outputs"
rm -f "$ZIP_PATH" "$DMG_PATH"

echo "[2/5] Copying binaries and libraries..."
cp "${BUILD_DIR}/src/cli/corridorkey" "$DIST_DIR/bin/"
cp "$CORE_LIB" "$DIST_DIR/bin/"
cp "$RUNTIME_LIB_REAL" "$DIST_DIR/bin/"
cp "$MLX_LIB" "$DIST_DIR/bin/"
cp "$MLX_METALLIB" "$DIST_DIR/bin/"
if [ "$RUNTIME_LIB_NAME" != "$RUNTIME_LINK_NAME" ]; then
    ln -sf "$RUNTIME_LIB_NAME" "$DIST_DIR/bin/$RUNTIME_LINK_NAME"
fi

for model in corridorkey_int8_512.onnx corridorkey_mlx.safetensors corridorkey_mlx_bridge_512.mlxfn corridorkey_mlx_bridge_1024.mlxfn; do
    cp "models/$model" "$DIST_DIR/models/"
done

echo "[3/5] Fixing library paths for portability..."
install_name_tool -change "@rpath/libcorridorkey_core.dylib" "@executable_path/$CORE_LIB_NAME" \
    "$DIST_DIR/bin/$BIN_NAME"
CORE_RUNTIME_DEPENDENCY="$(runtime_dependency_name "$DIST_DIR/bin/$CORE_LIB_NAME")"
if [ -n "${CORE_RUNTIME_DEPENDENCY:-}" ]; then
    rewrite_dependency "$DIST_DIR/bin/$CORE_LIB_NAME" "@rpath/$CORE_RUNTIME_DEPENDENCY" \
        "@loader_path/$RUNTIME_LIB_NAME"
fi
rewrite_dependency "$DIST_DIR/bin/$CORE_LIB_NAME" "@rpath/$RUNTIME_LINK_NAME" "@loader_path/$RUNTIME_LIB_NAME"
CORE_MLX_DEPENDENCY="$(mlx_dependency_path "$DIST_DIR/bin/$CORE_LIB_NAME")"
if [ -n "${CORE_MLX_DEPENDENCY:-}" ]; then
    rewrite_dependency "$DIST_DIR/bin/$CORE_LIB_NAME" "$CORE_MLX_DEPENDENCY" "@loader_path/$MLX_LIB_NAME"
fi
rewrite_dependency "$DIST_DIR/bin/$CORE_LIB_NAME" "@rpath/libmlx.dylib" "@loader_path/$MLX_LIB_NAME"
delete_absolute_rpaths "$DIST_DIR/bin/$BIN_NAME"
delete_absolute_rpaths "$DIST_DIR/bin/$CORE_LIB_NAME"
delete_absolute_rpaths "$DIST_DIR/bin/$RUNTIME_LIB_NAME"
delete_absolute_rpaths "$DIST_DIR/bin/$MLX_LIB_NAME"

echo "[4/5] Adding documentation..."
cat << 'LAUNCHER_EOF' > "$DIST_DIR/corridorkey"
#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"
exec "./bin/corridorkey" "$@"
LAUNCHER_EOF
chmod +x "$DIST_DIR/corridorkey"

cat <<README_EOF > "$DIST_DIR/README.txt"
CorridorKey Runtime v${VERSION} - Mac Portable Release
==================================================

This is a standalone, zero-python version of CorridorKey.

QUICK START:
1. Open Terminal.
2. Navigate to this folder: cd "\$(dirname "\$0")"
3. If macOS blocks the preview build after extraction, remove the quarantine
   attribute from this folder once:
   xattr -dr com.apple.quarantine CorridorKey_Mac_v${VERSION}
4. Check the bundle:
   ./corridorkey doctor
5. Run the smoke test:
   ./smoke_test.sh
6. Process a regular video:
   ./corridorkey process input.mp4 output.mp4
7. Process a 4K input with the stronger preset:
   ./corridorkey process input_4k.mp4 output_4k.mp4 --preset max
8. Run a synthetic benchmark:
   ./corridorkey benchmark

This preview is not notarized yet, so the quarantine-removal step above may be
required on first run.
README_EOF

cat << 'SMOKE_EOF' > "$DIST_DIR/smoke_test.sh"
#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

./corridorkey info
./corridorkey doctor --json > /dev/null
./corridorkey models --json > /dev/null
./corridorkey presets --json > /dev/null
./corridorkey benchmark --json > /dev/null
SMOKE_EOF
chmod +x "$DIST_DIR/smoke_test.sh"

echo "[4.5/5] Signing bundle..."
sign_binary "$DIST_DIR/bin/$RUNTIME_LIB_NAME"
sign_binary "$DIST_DIR/bin/$MLX_LIB_NAME"
sign_binary "$DIST_DIR/bin/$CORE_LIB_NAME"
sign_binary "$DIST_DIR/bin/$BIN_NAME"

echo "[4.75/5] Validating bundle outside the build tree..."
(cd "$DIST_DIR" && ./smoke_test.sh)

find "$DIST_DIR" \( -name '.DS_Store' -o -name '._*' \) -delete

case "$ARCHIVE_FORMAT" in
    zip)
        echo "[5/5] Creating ZIP archive..."
        create_zip_archive
        ARCHIVE_PATH="$ZIP_PATH"
        ;;
    dmg)
        echo "[5/5] Creating DMG archive..."
        create_dmg_archive
        ARCHIVE_PATH="$DMG_PATH"
        ;;
    *)
        echo "ERROR: Unsupported CORRIDORKEY_ARCHIVE_FORMAT '$ARCHIVE_FORMAT'. Use zip or dmg."
        exit 1
        ;;
esac

if [ -n "$NOTARY_PROFILE" ]; then
    echo "[5.5/5] Submitting for notarization..."
    notarize_archive "$ARCHIVE_PATH"
fi

echo "=================================================="
echo "SUCCESS: Bundle created at $ARCHIVE_PATH"
echo "=================================================="
