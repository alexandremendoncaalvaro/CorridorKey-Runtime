#!/bin/bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${CORRIDORKEY_BUILD_DIR:-build/release}"
VERSION="${CORRIDORKEY_VERSION:-0.1.0}"
DIST_ZIP="${ROOT_DIR}/dist/CorridorKey_Mac_v${VERSION}.zip"
OUTPUT_ROOT="${CORRIDORKEY_VALIDATION_ROOT:-${ROOT_DIR}/build/macos_release_validation}"
UNPACK_DIR="${OUTPUT_ROOT}/bundle"
INPUT_4K_VIDEO="${CORRIDORKEY_VALIDATION_INPUT_4K_VIDEO:-${ROOT_DIR}/assets/video_samples/100745-video-2160.mp4}"
INPUT_4K_FRAME="${CORRIDORKEY_VALIDATION_INPUT_4K_FRAME:-${ROOT_DIR}/build/runtime_inputs/100745-video-2160-frame0.png}"
INPUT_SMOKE="${CORRIDORKEY_VALIDATION_INPUT_SMOKE:-${ROOT_DIR}/assets/corridor.png}"

require_file() {
    local path="$1"
    if [ ! -f "$path" ]; then
        echo "Missing required file: $path" >&2
        exit 1
    fi
}

require_file "${ROOT_DIR}/scripts/package_mac.sh"
require_file "$INPUT_SMOKE"

if [ ! -f "$INPUT_4K_FRAME" ]; then
    require_file "$INPUT_4K_VIDEO"
    mkdir -p "$(dirname "$INPUT_4K_FRAME")"
    ffmpeg -y -i "$INPUT_4K_VIDEO" -frames:v 1 "$INPUT_4K_FRAME" >/tmp/corridorkey_validate_frame_extract.log 2>&1
fi

require_file "$INPUT_4K_FRAME"

rm -rf "$OUTPUT_ROOT"
mkdir -p "$OUTPUT_ROOT"
rm -rf "$UNPACK_DIR"

CORRIDORKEY_BUILD_DIR="$BUILD_DIR" "${ROOT_DIR}/scripts/package_mac.sh"

require_file "$DIST_ZIP"
python3 -m zipfile -e "$DIST_ZIP" "$UNPACK_DIR"

BUNDLE_ROOT="${UNPACK_DIR}/CorridorKey_Mac_v${VERSION}"
CLI="${BUNDLE_ROOT}/bin/corridorkey"
MODELS_DIR="${BUNDLE_ROOT}/models"

require_file "$CLI"
require_file "${MODELS_DIR}/corridorkey_mlx.safetensors"
require_file "${MODELS_DIR}/corridorkey_mlx_bridge_512.mlxfn"
require_file "${MODELS_DIR}/corridorkey_mlx_bridge_1024.mlxfn"
require_file "${MODELS_DIR}/corridorkey_int8_512.onnx"

chmod +x "$CLI"
chmod +x "${BUNDLE_ROOT}/smoke_test.sh"
bash "${BUNDLE_ROOT}/smoke_test.sh"

(cd "$BUNDLE_ROOT" && ./bin/corridorkey info --json) > "${OUTPUT_ROOT}/info.json"
(cd "$BUNDLE_ROOT" && ./bin/corridorkey doctor --json) > "${OUTPUT_ROOT}/doctor.json"
(cd "$BUNDLE_ROOT" && ./bin/corridorkey models --json) > "${OUTPUT_ROOT}/models.json"
(cd "$BUNDLE_ROOT" && ./bin/corridorkey presets --json) > "${OUTPUT_ROOT}/presets.json"

"$CLI" benchmark --json -m "${MODELS_DIR}/corridorkey_mlx.safetensors" -d auto \
    -i "$INPUT_SMOKE" -o "${OUTPUT_ROOT}/corridor_output" > "${OUTPUT_ROOT}/corridor_benchmark.json"

"$CLI" benchmark --json -m "${MODELS_DIR}/corridorkey_mlx.safetensors" -d auto --tiled \
    -i "$INPUT_4K_FRAME" -o "${OUTPUT_ROOT}/frame_4k_benchmark_output" > "${OUTPUT_ROOT}/frame_4k_benchmark.json"

"$CLI" process --json -m "${MODELS_DIR}/corridorkey_mlx.safetensors" -d auto --tiled \
    -i "$INPUT_4K_FRAME" -o "${OUTPUT_ROOT}/frame_4k_output" > "${OUTPUT_ROOT}/frame_4k_process.ndjson"

ffprobe -v error -print_format json -show_streams "$INPUT_4K_FRAME" > "${OUTPUT_ROOT}/input_4k_ffprobe.json"
ffprobe -v error -print_format json -show_streams "${OUTPUT_ROOT}/frame_4k_output/Comp/$(basename "$INPUT_4K_FRAME")" > "${OUTPUT_ROOT}/output_4k_ffprobe.json"

CORRIDORKEY_VALIDATION_ROOT="$OUTPUT_ROOT" python3 - <<'PY'
import json
import os
from pathlib import Path

output_root = Path(os.environ["CORRIDORKEY_VALIDATION_ROOT"])
input_probe = json.loads((output_root / "input_4k_ffprobe.json").read_text())
output_probe = json.loads((output_root / "output_4k_ffprobe.json").read_text())
doctor = json.loads((output_root / "doctor.json").read_text())
corridor_bench = json.loads((output_root / "corridor_benchmark.json").read_text())
frame_bench = json.loads((output_root / "frame_4k_benchmark.json").read_text())

def first_video_stream(doc):
    for stream in doc.get("streams", []):
        if stream.get("codec_type") == "video":
            return stream
    raise SystemExit("No video stream found in ffprobe output")

input_video = first_video_stream(input_probe)
output_video = first_video_stream(output_probe)

summary = {
    "bundle_root": str((output_root / "bundle").resolve()),
    "bundle_healthy": doctor["summary"]["bundle_healthy"],
    "apple_acceleration_healthy": doctor["summary"]["apple_acceleration_healthy"],
    "doctor_healthy": doctor["summary"]["healthy"],
    "input_width": int(input_video["width"]),
    "input_height": int(input_video["height"]),
    "output_width": int(output_video["width"]),
    "output_height": int(output_video["height"]),
    "output_matches_input_resolution": int(input_video["width"]) == int(output_video["width"])
    and int(input_video["height"]) == int(output_video["height"]),
    "corridor_backend": corridor_bench["backend"],
    "corridor_total_duration_ms": corridor_bench["total_duration_ms"],
    "frame_4k_backend": frame_bench["backend"],
    "frame_4k_total_duration_ms": frame_bench["total_duration_ms"],
}

(output_root / "summary.json").write_text(json.dumps(summary, indent=2), encoding="utf-8")
print(json.dumps(summary, indent=2))
PY
