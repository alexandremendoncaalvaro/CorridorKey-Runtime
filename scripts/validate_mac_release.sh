#!/bin/bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${CORRIDORKEY_BUILD_DIR:-build/release-macos-portable}"
VERSION="${CORRIDORKEY_VERSION:-0.1.2}"
DIST_ZIP="${ROOT_DIR}/dist/CorridorKey_Mac_v${VERSION}.zip"
DIST_DMG="${ROOT_DIR}/dist/CorridorKey_Mac_v${VERSION}.dmg"
OUTPUT_ROOT="${CORRIDORKEY_VALIDATION_ROOT:-${ROOT_DIR}/build/macos_release_validation}"
UNPACK_DIR="${OUTPUT_ROOT}/bundle"
DMG_MOUNT_DIR="${OUTPUT_ROOT}/dmg_mount"
INPUT_4K_VIDEO="${CORRIDORKEY_VALIDATION_INPUT_4K_VIDEO:-${ROOT_DIR}/assets/video_samples/100745-video-2160.mp4}"
INPUT_4K_FRAME="${CORRIDORKEY_VALIDATION_INPUT_4K_FRAME:-${ROOT_DIR}/build/runtime_inputs/100745-video-2160-frame0.png}"
INPUT_SMOKE="${CORRIDORKEY_VALIDATION_INPUT_SMOKE:-${ROOT_DIR}/assets/corridor.png}"
INPUT_SAMPLE_VIDEO="${CORRIDORKEY_VALIDATION_INPUT_SAMPLE_VIDEO:-${ROOT_DIR}/assets/video_samples/greenscreen_1769569137.mp4}"
SAMPLE_VIDEO_CLIP="${OUTPUT_ROOT}/sample_video_clip.mp4"
EXPECTED_MINOS="${CORRIDORKEY_EXPECTED_MACOS_MINOS:-14.0}"
ARCHIVE_FORMAT="${CORRIDORKEY_ARCHIVE_FORMAT:-zip}"
REQUIRE_GATEKEEPER="${CORRIDORKEY_REQUIRE_GATEKEEPER:-0}"
MOUNTED_DMG=0

require_file() {
    local path="$1"
    if [ ! -f "$path" ]; then
        echo "Missing required file: $path" >&2
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
        echo "Bundle leak: absolute LC_RPATH entries found in $binary" >&2
        echo "$leaked_rpaths" >&2
        exit 1
    fi
}

require_minos() {
    local binary="$1"
    local actual

    actual="$(otool -l "$binary" | awk '
        $1 == "cmd" && $2 == "LC_BUILD_VERSION" { in_build = 1; next }
        in_build && $1 == "minos" { print $2; exit }
    ')"

    if [ "$actual" != "$EXPECTED_MINOS" ]; then
        echo "Unexpected deployment target for $binary: got $actual, expected $EXPECTED_MINOS" >&2
        exit 1
    fi
}

assess_gatekeeper() {
    local target_path="$1"
    local target_type="$2"

    if ! spctl --assess --type "$target_type" -vv "$target_path"; then
        echo "Gatekeeper rejected $target_path" >&2
        exit 1
    fi
}

cleanup() {
    if [ "$MOUNTED_DMG" = "1" ]; then
        hdiutil detach "$DMG_MOUNT_DIR" >/tmp/corridorkey_validate_detach.log 2>&1 || true
    fi
}

trap cleanup EXIT

require_file "${ROOT_DIR}/scripts/package_mac.sh"
require_file "$INPUT_SMOKE"
require_file "$INPUT_SAMPLE_VIDEO"

if [ ! -f "$INPUT_4K_FRAME" ]; then
    require_file "$INPUT_4K_VIDEO"
    mkdir -p "$(dirname "$INPUT_4K_FRAME")"
    ffmpeg -y -i "$INPUT_4K_VIDEO" -frames:v 1 "$INPUT_4K_FRAME" >/tmp/corridorkey_validate_frame_extract.log 2>&1
fi

require_file "$INPUT_4K_FRAME"

rm -rf "$OUTPUT_ROOT"
mkdir -p "$OUTPUT_ROOT"
rm -rf "$UNPACK_DIR"
rm -rf "$DMG_MOUNT_DIR"

ffmpeg -y -i "$INPUT_SAMPLE_VIDEO" -frames:v 8 "$SAMPLE_VIDEO_CLIP" >/tmp/corridorkey_validate_sample_clip.log 2>&1
require_file "$SAMPLE_VIDEO_CLIP"

CORRIDORKEY_BUILD_DIR="$BUILD_DIR" \
CORRIDORKEY_ARCHIVE_FORMAT="$ARCHIVE_FORMAT" \
CORRIDORKEY_SIGN_IDENTITY="${CORRIDORKEY_SIGN_IDENTITY:-}" \
CORRIDORKEY_NOTARY_PROFILE="${CORRIDORKEY_NOTARY_PROFILE:-}" \
CORRIDORKEY_PUBLIC_RELEASE="${CORRIDORKEY_PUBLIC_RELEASE:-0}" \
    "${ROOT_DIR}/scripts/package_mac.sh"

case "$ARCHIVE_FORMAT" in
    zip)
        require_file "$DIST_ZIP"
        python3 -m zipfile -e "$DIST_ZIP" "$UNPACK_DIR"
        ;;
    dmg)
        require_file "$DIST_DMG"
        mkdir -p "$DMG_MOUNT_DIR"
        hdiutil attach "$DIST_DMG" -mountpoint "$DMG_MOUNT_DIR" -nobrowse -readonly >/tmp/corridorkey_validate_attach.log
        MOUNTED_DMG=1
        cp -R "$DMG_MOUNT_DIR/CorridorKey_Mac_v${VERSION}" "$UNPACK_DIR/"
        ;;
    *)
        echo "Unsupported CORRIDORKEY_ARCHIVE_FORMAT '$ARCHIVE_FORMAT'" >&2
        exit 1
        ;;
esac

BUNDLE_ROOT="${UNPACK_DIR}/CorridorKey_Mac_v${VERSION}"
CLI="${BUNDLE_ROOT}/bin/corridorkey"
LAUNCHER="${BUNDLE_ROOT}/corridorkey"
MODELS_DIR="${BUNDLE_ROOT}/models"

require_file "$CLI"
require_file "$LAUNCHER"
require_file "${BUNDLE_ROOT}/bin/libcorridorkey_core.dylib"
require_file "${BUNDLE_ROOT}/bin/libonnxruntime.1.24.3.dylib"
require_file "${MODELS_DIR}/corridorkey_mlx.safetensors"
require_file "${MODELS_DIR}/corridorkey_mlx_bridge_512.mlxfn"
require_file "${MODELS_DIR}/corridorkey_mlx_bridge_1024.mlxfn"
require_file "${MODELS_DIR}/corridorkey_int8_512.onnx"

chmod +x "$CLI"
chmod +x "$LAUNCHER"
chmod +x "${BUNDLE_ROOT}/smoke_test.sh"
require_no_absolute_rpaths "$CLI"
require_no_absolute_rpaths "${BUNDLE_ROOT}/bin/libcorridorkey_core.dylib"
require_minos "$CLI"
require_minos "${BUNDLE_ROOT}/bin/libcorridorkey_core.dylib"
codesign --verify --verbose=2 "$CLI"
codesign --verify --verbose=2 "${BUNDLE_ROOT}/bin/libcorridorkey_core.dylib"
codesign --verify --verbose=2 "${BUNDLE_ROOT}/bin/libonnxruntime.1.24.3.dylib"
codesign --verify --verbose=2 "${BUNDLE_ROOT}/bin/libmlx.dylib"

if [ "$REQUIRE_GATEKEEPER" = "1" ]; then
    case "$ARCHIVE_FORMAT" in
        zip)
            assess_gatekeeper "$CLI" execute
            ;;
        dmg)
            assess_gatekeeper "$DIST_DMG" open
            xcrun stapler validate "$DIST_DMG"
            ;;
    esac
fi

bash "${BUNDLE_ROOT}/smoke_test.sh"

(cd "$BUNDLE_ROOT" && ./corridorkey info --json) > "${OUTPUT_ROOT}/info.json"
(cd "$BUNDLE_ROOT" && ./corridorkey doctor --json) > "${OUTPUT_ROOT}/doctor.json"
(cd "$BUNDLE_ROOT" && ./corridorkey models --json) > "${OUTPUT_ROOT}/models.json"
(cd "$BUNDLE_ROOT" && ./corridorkey presets --json) > "${OUTPUT_ROOT}/presets.json"

(cd "$BUNDLE_ROOT" && ./corridorkey benchmark --json -i "$INPUT_SMOKE" \
    -o "${OUTPUT_ROOT}/corridor_output") > "${OUTPUT_ROOT}/corridor_benchmark.json"

(cd "$BUNDLE_ROOT" && ./corridorkey benchmark --json --preset max -i "$INPUT_4K_FRAME" \
    -o "${OUTPUT_ROOT}/frame_4k_benchmark_output") > "${OUTPUT_ROOT}/frame_4k_benchmark.json"

(cd "$BUNDLE_ROOT" && ./corridorkey process --json --preset max -i "$INPUT_4K_FRAME" \
    -o "${OUTPUT_ROOT}/frame_4k_output") > "${OUTPUT_ROOT}/frame_4k_process.ndjson"

(cd "$BUNDLE_ROOT" && ./corridorkey process --json -i "$SAMPLE_VIDEO_CLIP" \
    -o "${OUTPUT_ROOT}/sample_video_output.mp4") > "${OUTPUT_ROOT}/sample_video_process.ndjson"

(cd "$BUNDLE_ROOT" && ./corridorkey process --json "$SAMPLE_VIDEO_CLIP" \
    sample_video_output_flat.mp4) > "${OUTPUT_ROOT}/sample_video_process_flat.ndjson"

ffprobe -v error -print_format json -show_streams "$INPUT_4K_FRAME" > "${OUTPUT_ROOT}/input_4k_ffprobe.json"
ffprobe -v error -print_format json -show_streams "${OUTPUT_ROOT}/frame_4k_output/Comp/$(basename "$INPUT_4K_FRAME")" > "${OUTPUT_ROOT}/output_4k_ffprobe.json"
ffprobe -v error -print_format json -show_streams "$SAMPLE_VIDEO_CLIP" > "${OUTPUT_ROOT}/input_sample_video_ffprobe.json"
ffprobe -v error -print_format json -show_streams "${OUTPUT_ROOT}/sample_video_output.mp4" > "${OUTPUT_ROOT}/output_sample_video_ffprobe.json"
ffprobe -v error -print_format json -show_streams "${BUNDLE_ROOT}/sample_video_output_flat.mp4" > "${OUTPUT_ROOT}/output_sample_video_flat_ffprobe.json"

CORRIDORKEY_VALIDATION_ROOT="$OUTPUT_ROOT" \
CORRIDORKEY_ARCHIVE_FORMAT="$ARCHIVE_FORMAT" \
    python3 - <<'PY'
import json
import os
from pathlib import Path

output_root = Path(os.environ["CORRIDORKEY_VALIDATION_ROOT"])
input_probe = json.loads((output_root / "input_4k_ffprobe.json").read_text())
output_probe = json.loads((output_root / "output_4k_ffprobe.json").read_text())
input_sample_video_probe = json.loads((output_root / "input_sample_video_ffprobe.json").read_text())
output_sample_video_probe = json.loads((output_root / "output_sample_video_ffprobe.json").read_text())
output_sample_video_flat_probe = json.loads((output_root / "output_sample_video_flat_ffprobe.json").read_text())
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
input_sample_video = first_video_stream(input_sample_video_probe)
output_sample_video = first_video_stream(output_sample_video_probe)
output_sample_video_flat = first_video_stream(output_sample_video_flat_probe)

summary = {
    "archive_format": os.environ.get("CORRIDORKEY_ARCHIVE_FORMAT", "zip"),
    "bundle_root": str((output_root / "bundle").resolve()),
    "bundle_healthy": doctor["summary"]["bundle_healthy"],
    "apple_acceleration_healthy": doctor["summary"]["apple_acceleration_healthy"],
    "doctor_healthy": doctor["summary"]["healthy"],
    "signed": doctor["bundle"]["signature"]["signed"],
    "notarized": doctor["bundle"]["signature"]["notarized"],
    "gatekeeper_accepted": doctor["bundle"]["signature"]["gatekeeper_accepted"],
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
    "sample_video_width": int(output_sample_video["width"]),
    "sample_video_height": int(output_sample_video["height"]),
    "sample_video_matches_input_resolution": int(input_sample_video["width"])
    == int(output_sample_video["width"])
    and int(input_sample_video["height"]) == int(output_sample_video["height"]),
    "sample_video_flat_matches_input_resolution": int(input_sample_video["width"])
    == int(output_sample_video_flat["width"])
    and int(input_sample_video["height"]) == int(output_sample_video_flat["height"]),
}

(output_root / "summary.json").write_text(json.dumps(summary, indent=2), encoding="utf-8")
print(json.dumps(summary, indent=2))
PY
