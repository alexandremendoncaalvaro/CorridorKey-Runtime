#!/bin/bash
set -euo pipefail

CLI_PATH="${CORRIDORKEY_CLI:-build/release/src/cli/corridorkey}"
OFX_BENCHMARK_HARNESS="${CORRIDORKEY_OFX_BENCHMARK_HARNESS:-build/release/tests/integration/ofx_benchmark_harness}"
OUTPUT_ROOT="${CORRIDORKEY_OUTPUT_ROOT:-build/runtime_corpus}"
MODE="${CORRIDORKEY_CORPUS_PROFILE:-baseline}"
DEVICE="${CORRIDORKEY_DEVICE:-auto}"
SKIP_EXISTING="${CORRIDORKEY_SKIP_EXISTING:-1}"
MODEL_CPU_BASELINE="${CORRIDORKEY_MODEL_CPU_BASELINE:-models/corridorkey_int8_512.onnx}"
MODEL_COMPAT_HIGH="${CORRIDORKEY_MODEL_COMPAT_HIGH:-models/corridorkey_int8_768.onnx}"
MODEL_APPLE="${CORRIDORKEY_MODEL_APPLE:-models/corridorkey_mlx.safetensors}"
MODEL_APPLE_SYNTHETIC="${CORRIDORKEY_MODEL_APPLE_SYNTHETIC:-models/corridorkey_mlx_bridge_512.mlxfn}"
INPUT_4K_VIDEO="${CORRIDORKEY_INPUT_4K_VIDEO:-assets/video_samples/100745-video-2160.mp4}"
INPUT_4K_FRAME="${CORRIDORKEY_INPUT_4K_FRAME:-build/runtime_inputs/100745-video-2160-frame0.png}"
SEQUENCE_SOURCE="assets/image_samples/thelikelyandunfortunatedemiseofyourgpu"
SEQUENCE_SUBSET_DIR="${OUTPUT_ROOT}/inputs/thelikelyandunfortunatedemiseofyourgpu_20"
COMMAND_LOG="${OUTPUT_ROOT}/commands.log"
PRIMARY_MODEL=""
PRIMARY_DEVICE="$DEVICE"
PRIMARY_SYNTHETIC_MODEL=""

require_file() {
    local path="$1"
    if [ ! -f "$path" ]; then
        echo "Missing required file: $path" >&2
        exit 1
    fi
}

log_command() {
    printf '%s\n' "$*" >> "$COMMAND_LOG"
}

run_tool_capture() {
    local executable="$1"
    local artifact_path="$2"
    shift 2

    mkdir -p "$(dirname "$artifact_path")"
    if [ "$SKIP_EXISTING" = "1" ] && [ -f "$artifact_path" ]; then
        echo "Skipping existing artifact: $artifact_path"
        return
    fi

    log_command "$executable $*"
    "$executable" "$@" > "$artifact_path"
}

run_capture() {
    local artifact_path="$1"
    shift
    run_tool_capture "$CLI_PATH" "$artifact_path" "$@"
}

run_case_process() {
    local ndjson_path="$1"
    shift
    run_tool_capture "$CLI_PATH" "$ndjson_path" "$@"
}

prepare_sequence_subset() {
    mkdir -p "$SEQUENCE_SUBSET_DIR"
    find "$SEQUENCE_SUBSET_DIR" -type l -delete

    local count=0
    while IFS= read -r frame_path; do
        ln -sf "$(cd "$(dirname "$frame_path")" && pwd)/$(basename "$frame_path")" \
            "$SEQUENCE_SUBSET_DIR/$(basename "$frame_path")"
        count=$((count + 1))
        if [ "$count" -ge 20 ]; then
            break
        fi
    done < <(find "$SEQUENCE_SOURCE" -type f -name '*.png' | sort)

    if [ "$count" -lt 20 ]; then
        echo "Expected at least 20 PNG frames in $SEQUENCE_SOURCE" >&2
        exit 1
    fi
}

prepare_4k_frame() {
    if [ -f "$INPUT_4K_FRAME" ]; then
        return
    fi

    require_file "$INPUT_4K_VIDEO"
    mkdir -p "$(dirname "$INPUT_4K_FRAME")"
    ffmpeg -y -i "$INPUT_4K_VIDEO" -frames:v 1 "$INPUT_4K_FRAME" >/tmp/corridorkey_run_corpus_frame_extract.log 2>&1
}

run_common_artifacts() {
    run_capture "${OUTPUT_ROOT}/doctor.json" doctor --json
    run_capture "${OUTPUT_ROOT}/models.json" models --json
    run_capture "${OUTPUT_ROOT}/presets.json" presets --json
    run_capture "${OUTPUT_ROOT}/benchmark_synthetic_cpu_512.json" benchmark --json -m "$MODEL_CPU_BASELINE" -d cpu
    run_capture "${OUTPUT_ROOT}/benchmark_synthetic_primary.json" benchmark --json \
        -m "$PRIMARY_SYNTHETIC_MODEL" -d "$PRIMARY_DEVICE"
    if [ -x "$OFX_BENCHMARK_HARNESS" ]; then
        run_tool_capture "$OFX_BENCHMARK_HARNESS" "${OUTPUT_ROOT}/benchmark_ofx_primary.json" \
            --model "$PRIMARY_SYNTHETIC_MODEL" --device "$PRIMARY_DEVICE"
    fi
}

run_smoke_cases() {
    run_capture "${OUTPUT_ROOT}/corridor_png/benchmark.json" benchmark --json \
        -m "$PRIMARY_MODEL" -d "$PRIMARY_DEVICE" -i assets/corridor.png -o "${OUTPUT_ROOT}/corridor_png/output"
    run_case_process "${OUTPUT_ROOT}/corridor_png/process.ndjson" process --json \
        -m "$PRIMARY_MODEL" -d "$PRIMARY_DEVICE" -i assets/corridor.png -o "${OUTPUT_ROOT}/corridor_png/output"
}

run_baseline_cases() {
    prepare_sequence_subset
    prepare_4k_frame

    run_capture "${OUTPUT_ROOT}/sequence_20/benchmark.json" benchmark --json \
        -m "$PRIMARY_MODEL" -d "$PRIMARY_DEVICE" -i "$SEQUENCE_SUBSET_DIR" -o "${OUTPUT_ROOT}/sequence_20/output"
    run_case_process "${OUTPUT_ROOT}/sequence_20/process.ndjson" process --json \
        -m "$PRIMARY_MODEL" -d "$PRIMARY_DEVICE" -i "$SEQUENCE_SUBSET_DIR" -o "${OUTPUT_ROOT}/sequence_20/output"

    run_capture "${OUTPUT_ROOT}/frame_4k/benchmark.json" benchmark --json \
        -m "$PRIMARY_MODEL" -d "$PRIMARY_DEVICE" --tiled \
        -i "$INPUT_4K_FRAME" \
        -o "${OUTPUT_ROOT}/frame_4k/output"
    run_case_process "${OUTPUT_ROOT}/frame_4k/process.ndjson" process --json \
        -m "$PRIMARY_MODEL" -d "$PRIMARY_DEVICE" --tiled \
        -i "$INPUT_4K_FRAME" \
        -o "${OUTPUT_ROOT}/frame_4k/output"
}

run_full_cases() {
    require_file "$INPUT_4K_VIDEO"
    run_capture "${OUTPUT_ROOT}/greenscreen_1769569137/benchmark.json" benchmark --json \
        -m "$PRIMARY_MODEL" -d "$PRIMARY_DEVICE" -i assets/video_samples/greenscreen_1769569137.mp4 \
        -o "${OUTPUT_ROOT}/greenscreen_1769569137/output.mp4"
    run_capture "${OUTPUT_ROOT}/greenscreen_1769569320/benchmark.json" benchmark --json \
        -m "$PRIMARY_MODEL" -d "$PRIMARY_DEVICE" -i assets/video_samples/greenscreen_1769569320.mp4 \
        -o "${OUTPUT_ROOT}/greenscreen_1769569320/output.mp4"
    run_capture "${OUTPUT_ROOT}/video_4k_short/benchmark.json" benchmark --json \
        -m "$PRIMARY_MODEL" -d "$PRIMARY_DEVICE" --tiled \
        -i "$INPUT_4K_VIDEO" \
        -o "${OUTPUT_ROOT}/video_4k_short/output.mp4"
    run_case_process "${OUTPUT_ROOT}/video_4k_short/process.ndjson" process --json \
        -m "$PRIMARY_MODEL" -d "$PRIMARY_DEVICE" --tiled \
        -i "$INPUT_4K_VIDEO" \
        -o "${OUTPUT_ROOT}/video_4k_short/output.mp4"
    run_capture "${OUTPUT_ROOT}/mixkit_4k/benchmark.json" benchmark --json \
        -m "$PRIMARY_MODEL" -d "$PRIMARY_DEVICE" --tiled \
        -i assets/video_samples/mixkit-girl-dancing-with-her-earphones-on-a-green-background-28306-4k.mp4 \
        -o "${OUTPUT_ROOT}/mixkit_4k/output.mp4"
    run_case_process "${OUTPUT_ROOT}/mixkit_4k/process.ndjson" process --json \
        -m "$PRIMARY_MODEL" -d "$PRIMARY_DEVICE" --tiled \
        -i assets/video_samples/mixkit-girl-dancing-with-her-earphones-on-a-green-background-28306-4k.mp4 \
        -o "${OUTPUT_ROOT}/mixkit_4k/output.mp4"
}

require_file "$CLI_PATH"
require_file "$MODEL_CPU_BASELINE"
if [ -f "$MODEL_APPLE" ]; then
    PRIMARY_MODEL="$MODEL_APPLE"
else
    PRIMARY_MODEL="$MODEL_COMPAT_HIGH"
fi
require_file "$PRIMARY_MODEL"
if [ -f "$MODEL_APPLE_SYNTHETIC" ]; then
    PRIMARY_SYNTHETIC_MODEL="$MODEL_APPLE_SYNTHETIC"
else
    PRIMARY_SYNTHETIC_MODEL="$PRIMARY_MODEL"
fi

mkdir -p "$OUTPUT_ROOT"
: > "$COMMAND_LOG"

case "$MODE" in
    smoke)
        run_common_artifacts
        run_smoke_cases
        ;;
    baseline)
        run_common_artifacts
        run_smoke_cases
        run_baseline_cases
        ;;
    full)
        run_common_artifacts
        run_smoke_cases
        run_baseline_cases
        run_full_cases
        ;;
    *)
        echo "Unsupported CORRIDORKEY_CORPUS_PROFILE: $MODE" >&2
        exit 1
        ;;
esac

echo "Artifacts written to $OUTPUT_ROOT"
