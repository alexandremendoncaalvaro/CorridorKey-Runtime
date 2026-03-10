#!/bin/bash
set -euo pipefail

CLI_PATH="${CORRIDORKEY_CLI:-build/release/src/cli/corridorkey}"
OUTPUT_ROOT="${CORRIDORKEY_OUTPUT_ROOT:-build/runtime_corpus}"
MODE="${CORRIDORKEY_CORPUS_PROFILE:-baseline}"
DEVICE="${CORRIDORKEY_DEVICE:-auto}"
SKIP_EXISTING="${CORRIDORKEY_SKIP_EXISTING:-1}"
MODEL_SMOKE="${CORRIDORKEY_MODEL_SMOKE:-models/corridorkey_int8_512.onnx}"
MODEL_HIGH_QUALITY="${CORRIDORKEY_MODEL_HIGH_QUALITY:-models/corridorkey_int8_768.onnx}"
SEQUENCE_SOURCE="assets/image_samples/thelikelyandunfortunatedemiseofyourgpu"
SEQUENCE_SUBSET_DIR="${OUTPUT_ROOT}/inputs/thelikelyandunfortunatedemiseofyourgpu_20"
COMMAND_LOG="${OUTPUT_ROOT}/commands.log"

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

run_capture() {
    local artifact_path="$1"
    shift

    mkdir -p "$(dirname "$artifact_path")"
    if [ "$SKIP_EXISTING" = "1" ] && [ -f "$artifact_path" ]; then
        echo "Skipping existing artifact: $artifact_path"
        return
    fi

    log_command "$CLI_PATH $*"
    "$CLI_PATH" "$@" > "$artifact_path"
}

run_case_process() {
    local ndjson_path="$1"
    shift

    mkdir -p "$(dirname "$ndjson_path")"
    if [ "$SKIP_EXISTING" = "1" ] && [ -f "$ndjson_path" ]; then
        echo "Skipping existing artifact: $ndjson_path"
        return
    fi

    log_command "$CLI_PATH $*"
    "$CLI_PATH" "$@" > "$ndjson_path"
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

run_common_artifacts() {
    run_capture "${OUTPUT_ROOT}/doctor.json" doctor --json
    run_capture "${OUTPUT_ROOT}/models.json" models --json
    run_capture "${OUTPUT_ROOT}/presets.json" presets --json
    run_capture "${OUTPUT_ROOT}/benchmark_synthetic_cpu_512.json" benchmark --json -m "$MODEL_SMOKE" -d cpu
}

run_smoke_cases() {
    run_capture "${OUTPUT_ROOT}/corridor_png/benchmark.json" benchmark --json \
        -m "$MODEL_SMOKE" -d "$DEVICE" -i assets/corridor.png -o "${OUTPUT_ROOT}/corridor_png/output"
    run_case_process "${OUTPUT_ROOT}/corridor_png/process.ndjson" process --json \
        -m "$MODEL_SMOKE" -d "$DEVICE" -i assets/corridor.png -o "${OUTPUT_ROOT}/corridor_png/output"
}

run_baseline_cases() {
    prepare_sequence_subset

    run_capture "${OUTPUT_ROOT}/sequence_20/benchmark.json" benchmark --json \
        -m "$MODEL_SMOKE" -d "$DEVICE" -i "$SEQUENCE_SUBSET_DIR" -o "${OUTPUT_ROOT}/sequence_20/output"
    run_case_process "${OUTPUT_ROOT}/sequence_20/process.ndjson" process --json \
        -m "$MODEL_SMOKE" -d "$DEVICE" -i "$SEQUENCE_SUBSET_DIR" -o "${OUTPUT_ROOT}/sequence_20/output"

    run_capture "${OUTPUT_ROOT}/video_4k_short/benchmark.json" benchmark --json \
        -m "$MODEL_HIGH_QUALITY" -d "$DEVICE" --tiled \
        -i assets/video_samples/100745-video-2160.mp4 \
        -o "${OUTPUT_ROOT}/video_4k_short/output.mp4"
}

run_full_cases() {
    run_capture "${OUTPUT_ROOT}/greenscreen_1769569137/benchmark.json" benchmark --json \
        -m "$MODEL_HIGH_QUALITY" -d "$DEVICE" -i assets/video_samples/greenscreen_1769569137.mp4 \
        -o "${OUTPUT_ROOT}/greenscreen_1769569137/output.mp4"
    run_capture "${OUTPUT_ROOT}/greenscreen_1769569320/benchmark.json" benchmark --json \
        -m "$MODEL_HIGH_QUALITY" -d "$DEVICE" -i assets/video_samples/greenscreen_1769569320.mp4 \
        -o "${OUTPUT_ROOT}/greenscreen_1769569320/output.mp4"
    run_capture "${OUTPUT_ROOT}/mixkit_4k/benchmark.json" benchmark --json \
        -m "$MODEL_HIGH_QUALITY" -d "$DEVICE" --tiled \
        -i assets/video_samples/mixkit-girl-dancing-with-her-earphones-on-a-green-background-28306-4k.mp4 \
        -o "${OUTPUT_ROOT}/mixkit_4k/output.mp4"
}

require_file "$CLI_PATH"
require_file "$MODEL_SMOKE"
require_file "$MODEL_HIGH_QUALITY"

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
