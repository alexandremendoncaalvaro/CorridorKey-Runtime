#!/bin/bash
set -e

VERSION="0.1.0"
DIST_DIR="dist/CorridorKey_Mac_v${VERSION}"
BIN_NAME="corridorkey"

echo "[1/5] Cleaning and creating dist directory..."
rm -rf "$DIST_DIR"
mkdir -p "$DIST_DIR/bin"
mkdir -p "$DIST_DIR/models"
mkdir -p "$DIST_DIR/outputs"

echo "[2/5] Copying binaries and libraries..."
cp build/release/src/cli/corridorkey "$DIST_DIR/bin/"
cp vendor/onnxruntime/lib/libonnxruntime.1.16.3.dylib "$DIST_DIR/bin/"
ln -sf "libonnxruntime.1.16.3.dylib" "$DIST_DIR/bin/libonnxruntime.dylib"

for model in corridorkey_int8_512.onnx corridorkey_int8_768.onnx; do
    if [ -f "models/$model" ]; then
        cp "models/$model" "$DIST_DIR/models/"
    fi
done

echo "[3/5] Fixing library paths for portability..."
# Make the binary look for the dylib in the same directory
install_name_tool -change "@rpath/libonnxruntime.1.16.3.dylib" "@executable_path/libonnxruntime.1.16.3.dylib" "$DIST_DIR/bin/$BIN_NAME"

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
4. If the validated models are not bundled, download them:
   ./bin/corridorkey download --variant int8
5. Process your video:
   ./bin/corridorkey process -i input.mp4 -o outputs/result.mp4 -m models/corridorkey_int8_512.onnx

NOTE: If you get a "Developer cannot be verified" error,
right-click the 'bin/corridorkey' file and select 'Open' once.
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
SMOKE_EOF
chmod +x "$DIST_DIR/smoke_test.sh"

echo "[5/5] Creating ZIP archive..."
cd dist
zip -r "CorridorKey_Mac_v${VERSION}.zip" "CorridorKey_Mac_v${VERSION}"
cd ..

echo "=================================================="
echo "SUCCESS: Bundle created at dist/CorridorKey_Mac_v${VERSION}.zip"
echo "=================================================="
