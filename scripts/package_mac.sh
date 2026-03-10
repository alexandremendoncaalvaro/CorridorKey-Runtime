#!/bin/bash
set -euo pipefail

VERSION="${CORRIDORKEY_VERSION:-0.1.0}"
DIST_DIR="dist/CorridorKey_Mac_v${VERSION}"
BIN_NAME="corridorkey"
BUILD_DIR="${CORRIDORKEY_BUILD_DIR:-build/release}"
SIGN_IDENTITY="${CORRIDORKEY_SIGN_IDENTITY:-}"
NOTARY_PROFILE="${CORRIDORKEY_NOTARY_PROFILE:-}"
RUNTIME_DIR="vendor/onnxruntime/lib"

if [ -f "$RUNTIME_DIR/libonnxruntime.dylib" ]; then
    RUNTIME_LIB="$RUNTIME_DIR/libonnxruntime.dylib"
else
    RUNTIME_LIB="$(find "$RUNTIME_DIR" -maxdepth 1 -type f -name 'libonnxruntime*.dylib' | head -n 1)"
fi

if [ -z "${RUNTIME_LIB:-}" ] || [ ! -f "$RUNTIME_LIB" ]; then
    echo "ERROR: Unable to locate vendored ONNX Runtime dylib in $RUNTIME_DIR"
    exit 1
fi

RUNTIME_LIB_NAME="$(basename "$RUNTIME_LIB")"

echo "[1/5] Cleaning and creating dist directory..."
rm -rf "$DIST_DIR"
mkdir -p "$DIST_DIR/bin"
mkdir -p "$DIST_DIR/models"
mkdir -p "$DIST_DIR/outputs"

echo "[2/5] Copying binaries and libraries..."
cp "${BUILD_DIR}/src/cli/corridorkey" "$DIST_DIR/bin/"
cp "$RUNTIME_LIB" "$DIST_DIR/bin/"
if [ "$RUNTIME_LIB_NAME" != "libonnxruntime.dylib" ]; then
    ln -sf "$RUNTIME_LIB_NAME" "$DIST_DIR/bin/libonnxruntime.dylib"
fi

for model in corridorkey_int8_512.onnx corridorkey_int8_768.onnx; do
    if [ -f "models/$model" ]; then
        cp "models/$model" "$DIST_DIR/models/"
    fi
done

echo "[3/5] Fixing library paths for portability..."
# Make the binary look for the dylib in the same directory
install_name_tool -change "@rpath/$RUNTIME_LIB_NAME" "@executable_path/$RUNTIME_LIB_NAME" "$DIST_DIR/bin/$BIN_NAME"

echo "[4/5] Adding documentation..."
cat << 'README_EOF' > "$DIST_DIR/README.txt"
CorridorKey Runtime v${VERSION} - Mac Portable Release
==================================================

This is a standalone, zero-python version of CorridorKey.

QUICK START:
1. Open Terminal.
2. Navigate to this folder: cd "$(dirname "$0")"
3. Run the smoke test:
   ./smoke_test.sh
4. Benchmark the bundle:
   ./bin/corridorkey benchmark --json -m models/corridorkey_int8_512.onnx
5. Process your video:
   ./bin/corridorkey process -i input.mp4 -o outputs/result.mp4 -m models/corridorkey_int8_512.onnx

If the validated models are not bundled, place them in ./models before running
process or benchmark. When the bundle is signed and notarized, Gatekeeper should
accept it without the manual "Open" workaround.
README_EOF

cat << 'SMOKE_EOF' > "$DIST_DIR/smoke_test.sh"
#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

./bin/corridorkey info
./bin/corridorkey doctor --json > /dev/null
./bin/corridorkey models --json > /dev/null
./bin/corridorkey presets --json > /dev/null
if [ -f "./models/corridorkey_int8_512.onnx" ]; then
    ./bin/corridorkey benchmark --json -m ./models/corridorkey_int8_512.onnx > /dev/null
fi
SMOKE_EOF
chmod +x "$DIST_DIR/smoke_test.sh"

if [ -n "$SIGN_IDENTITY" ]; then
    echo "[4.5/5] Signing bundle..."
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
