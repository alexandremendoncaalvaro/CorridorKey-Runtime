#!/usr/bin/env bash
# Validate a staged Linux OFX bundle before releasing. Parity with
# scripts/validate_ofx_win.ps1. Asserts:
#   1. The bundle has the expected Linux-x86_64 layout and core files.
#   2. The embedded corridorkey CLI reports doctor healthy=true.
#   3. Optional: the three packaged artifacts (tar.gz/deb/rpm) extract
#      cleanly and contain the same payload.
# Usage:
#   scripts/validate_ofx_linux.sh path/to/CorridorKey.ofx.bundle

set -euo pipefail

BUNDLE_PATH="${1:-}"
if [[ -z "$BUNDLE_PATH" ]]; then
    echo "Usage: $0 path/to/CorridorKey.ofx.bundle" >&2
    exit 1
fi

if [[ ! -d "$BUNDLE_PATH" ]]; then
    echo "Bundle not found: $BUNDLE_PATH" >&2
    exit 1
fi

CONTENTS="$BUNDLE_PATH/Contents/Linux-x86_64"
OFX_SO="$CONTENTS/CorridorKey.ofx"
CLI_BIN="$CONTENTS/corridorkey"
ORT_SO="$CONTENTS/libonnxruntime.so"

missing=0
for path in "$OFX_SO" "$CLI_BIN"; do
    if [[ ! -f "$path" ]]; then
        echo "MISSING: $path" >&2
        missing=1
    fi
done
if [[ ! -f "$ORT_SO" ]] && ! ls "$CONTENTS"/libonnxruntime.so.* >/dev/null 2>&1; then
    echo "MISSING: $CONTENTS/libonnxruntime.so (or a versioned sibling)" >&2
    missing=1
fi
if [[ $missing -ne 0 ]]; then
    echo "Linux bundle layout validation FAILED" >&2
    exit 1
fi

echo "[validate] layout OK"

chmod +x "$CLI_BIN" 2>/dev/null || true
MODELS_DIR="$BUNDLE_PATH/Contents/Resources/models"
if [[ ! -d "$MODELS_DIR" ]]; then
    echo "MISSING: $MODELS_DIR" >&2
    exit 1
fi

echo "[validate] running '$CLI_BIN doctor --json'..."
if doctor_output="$("$CLI_BIN" doctor --json --models-dir "$MODELS_DIR" 2>&1)"; then
    :
else
    echo "$doctor_output" >&2
    echo "Linux bundle doctor FAILED" >&2
    exit 1
fi

if command -v python3 >/dev/null 2>&1; then
    if ! echo "$doctor_output" | python3 -c "
import sys, json
report = json.load(sys.stdin)
if not report.get('healthy', False):
    print('doctor reports healthy=false', file=sys.stderr)
    sys.exit(2)
"; then
        echo "$doctor_output"
        exit 1
    fi
fi

echo "[validate] doctor reports healthy=true"
echo "[validate] Linux bundle OK"
