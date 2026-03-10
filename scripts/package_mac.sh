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
3. Download the model:
   ./bin/corridorkey download --variant int8
4. Process your video:
   ./bin/corridorkey process -i input.mp4 -o outputs/result.mp4 -m models/corridorkey_int8_512.onnx

NOTE: If you get a "Developer cannot be verified" error,
right-click the 'bin/corridorkey' file and select 'Open' once.
README_EOF

echo "[5/5] Creating ZIP archive..."
cd dist
zip -r "CorridorKey_Mac_v${VERSION}.zip" "CorridorKey_Mac_v${VERSION}"
cd ..

echo "=================================================="
echo "SUCCESS: Bundle created at dist/CorridorKey_Mac_v${VERSION}.zip"
echo "=================================================="
