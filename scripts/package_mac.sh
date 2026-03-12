#!/bin/bash
set -euo pipefail

VERSION="${CORRIDORKEY_VERSION:-0.1.0}"
DIST_DIR="dist/CorridorKey_Mac_v${VERSION}"
BIN_NAME="corridorkey"
BUILD_DIR="${CORRIDORKEY_BUILD_DIR:-build/release}"
SIGN_IDENTITY="${CORRIDORKEY_SIGN_IDENTITY:-}"
NOTARY_PROFILE="${CORRIDORKEY_NOTARY_PROFILE:-}"
RUNTIME_DIR="vendor/onnxruntime/lib"
CORE_LIB="${BUILD_DIR}/src/libcorridorkey_core.dylib"
MLX_LIB="${CORRIDORKEY_MLX_LIB:-}"
MLX_METALLIB="${CORRIDORKEY_MLX_METALLIB:-}"
MLX_REQUIRED_PACK="models/corridorkey_mlx.safetensors"
MLX_REQUIRED_BRIDGE="models/corridorkey_mlx_bridge_512.mlxfn"
MLX_HIGH_RES_BRIDGE="models/corridorkey_mlx_bridge_1024.mlxfn"
CPU_BASELINE_MODEL="models/corridorkey_int8_512.onnx"

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
    MLX_LIB="$(find .venv-macos-mlx -type f -name 'libmlx.dylib' | head -n 1)"
fi

if [ -z "${MLX_LIB:-}" ] || [ ! -f "$MLX_LIB" ]; then
    echo "ERROR: Unable to locate libmlx.dylib. Set CORRIDORKEY_MLX_LIB if MLX is installed elsewhere."
    exit 1
fi

if [ -z "$MLX_METALLIB" ]; then
    MLX_METALLIB="$(find .venv-macos-mlx -type f -name 'mlx.metallib' | head -n 1)"
fi

if [ -z "${MLX_METALLIB:-}" ] || [ ! -f "$MLX_METALLIB" ]; then
    echo "ERROR: Unable to locate mlx.metallib. Set CORRIDORKEY_MLX_METALLIB if MLX is installed elsewhere."
    exit 1
fi

RUNTIME_LIB_NAME="$(basename "$RUNTIME_LIB")"
CORE_LIB_NAME="$(basename "$CORE_LIB")"
MLX_LIB_NAME="$(basename "$MLX_LIB")"
MLX_METALLIB_NAME="$(basename "$MLX_METALLIB")"

echo "[1/5] Cleaning and creating dist directory..."
rm -rf "$DIST_DIR"
mkdir -p "$DIST_DIR/bin"
mkdir -p "$DIST_DIR/models"
mkdir -p "$DIST_DIR/outputs"

echo "[2/5] Copying binaries and libraries..."
cp "${BUILD_DIR}/src/cli/corridorkey" "$DIST_DIR/bin/"
cp "$CORE_LIB" "$DIST_DIR/bin/"
cp "$RUNTIME_LIB" "$DIST_DIR/bin/"
cp "$MLX_LIB" "$DIST_DIR/bin/"
cp "$MLX_METALLIB" "$DIST_DIR/bin/"
if [ "$RUNTIME_LIB_NAME" != "libonnxruntime.dylib" ]; then
    ln -sf "$RUNTIME_LIB_NAME" "$DIST_DIR/bin/libonnxruntime.dylib"
fi

for model in corridorkey_int8_512.onnx corridorkey_mlx.safetensors corridorkey_mlx_bridge_512.mlxfn corridorkey_mlx_bridge_1024.mlxfn; do
    cp "models/$model" "$DIST_DIR/models/"
done

echo "[3/5] Fixing library paths for portability..."
install_name_tool -change "@rpath/libcorridorkey_core.dylib" "@executable_path/$CORE_LIB_NAME" \
    "$DIST_DIR/bin/$BIN_NAME"
install_name_tool -change "@rpath/$RUNTIME_LIB_NAME" "@loader_path/$RUNTIME_LIB_NAME" \
    "$DIST_DIR/bin/$CORE_LIB_NAME"
install_name_tool -change "@rpath/libmlx.dylib" "@loader_path/$MLX_LIB_NAME" \
    "$DIST_DIR/bin/$CORE_LIB_NAME"

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
3. Check the bundle:
   ./corridorkey doctor
4. Run the smoke test:
   ./smoke_test.sh
5. Process a regular video:
   ./corridorkey process input.mp4 output.mp4
6. Process a 4K input with the stronger preset:
   ./corridorkey process input_4k.mp4 output_4k.mp4 --preset max
7. Run a synthetic benchmark:
   ./corridorkey benchmark

When the bundle is signed and notarized, Gatekeeper should accept it without
the manual "Open" workaround.
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

echo "[4.25/5] Validating bundle outside the build tree..."
(cd "$DIST_DIR" && ./smoke_test.sh)

if [ -n "$SIGN_IDENTITY" ]; then
    echo "[4.5/5] Signing bundle..."
    codesign --force --options runtime --sign "$SIGN_IDENTITY" "$DIST_DIR/bin/$CORE_LIB_NAME"
    codesign --force --options runtime --sign "$SIGN_IDENTITY" "$DIST_DIR/bin/$MLX_LIB_NAME"
    codesign --force --options runtime --sign "$SIGN_IDENTITY" "$DIST_DIR/bin/$RUNTIME_LIB_NAME"
    codesign --force --options runtime --sign "$SIGN_IDENTITY" "$DIST_DIR/bin/$BIN_NAME"
fi

echo "[5/5] Creating ZIP archive..."
cd dist
zip -r "CorridorKey_Mac_v${VERSION}.zip" "CorridorKey_Mac_v${VERSION}"
cd ..

if [ -n "$NOTARY_PROFILE" ]; then
    echo "[5.5/5] Submitting for notarization..."
    xcrun notarytool submit "dist/CorridorKey_Mac_v${VERSION}.zip" --keychain-profile "$NOTARY_PROFILE" --wait
fi

echo "=================================================="
echo "SUCCESS: Bundle created at dist/CorridorKey_Mac_v${VERSION}.zip"
echo "=================================================="
