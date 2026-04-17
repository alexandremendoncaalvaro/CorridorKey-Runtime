#!/bin/bash
# benchmark_mac_m4.sh — Benchmark CorridorKey on a local Apple Silicon Mac.
#
# Captures a breakdown of load time, steady-state per-frame latency, peak RSS,
# system wired memory, and (when available) ANE/GPU/CPU watt draw via
# powermetrics. Output goes to dist/bench_m4_<hostname>_<timestamp>.json with an
# aggregated summary plus the raw per-resolution benchmark JSON.
#
# This script targets the Mac Mini M4 16 GB that the project uses as the
# baseline Apple Silicon hardware, but the logic is generic across
# Apple Silicon SKUs. It does not require the CLI to be installed; it resolves
# the build-tree binary by default and supports pointing at a packaged bundle.
#
# Usage:
#   scripts/benchmark_mac_m4.sh [--cli PATH] [--model PATH]
#                               [--resolutions "512,1024,1536,2048"]
#                               [--powermetrics] [--output PATH] [--tag LABEL]
#
# The benchmark command uses its built-in warmup/steady-state run counts
# (2 warmup, 5 steady) — they are not tunable from the CLI.
#
# --powermetrics runs `sudo powermetrics` in the background while each
# resolution benchmark executes. It requires the user to have sudo available
# without a TTY prompt (e.g. via a pre-authenticated session). When sudo is not
# available the script falls back to skipping the power samples and logs a
# warning; the benchmark numbers are still captured.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_ROOT"

CLI_PATH=""
MODEL_PATH=""
RESOLUTIONS="512,1024,1536,2048"
USE_POWERMETRICS=0
OUTPUT_PATH=""
LABEL=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        --cli) CLI_PATH="$2"; shift 2 ;;
        --model) MODEL_PATH="$2"; shift 2 ;;
        --resolutions) RESOLUTIONS="$2"; shift 2 ;;
        --powermetrics) USE_POWERMETRICS=1; shift ;;
        --output) OUTPUT_PATH="$2"; shift 2 ;;
        --tag) LABEL="$2"; shift 2 ;;
        -h|--help)
            sed -n '2,28p' "$0" | sed 's/^# \{0,1\}//'
            exit 0 ;;
        *) echo "Unknown argument: $1" >&2; exit 1 ;;
    esac
done

# ---------------------------------------------------------------------------
# Resolve CLI binary
# ---------------------------------------------------------------------------
if [[ -z "$CLI_PATH" ]]; then
    for candidate in \
        "build/release-macos-portable/src/cli/corridorkey" \
        "build/debug-macos-portable/src/cli/corridorkey" \
        "build/debug/src/cli/corridorkey" \
        "build/release/src/cli/corridorkey"; do
        if [[ -x "$candidate" ]]; then
            CLI_PATH="$candidate"
            break
        fi
    done
fi

if [[ -z "$CLI_PATH" || ! -x "$CLI_PATH" ]]; then
    echo "Could not locate CorridorKey CLI. Pass --cli PATH or build first." >&2
    exit 2
fi
CLI_PATH="$(cd "$(dirname "$CLI_PATH")" && pwd)/$(basename "$CLI_PATH")"

# ---------------------------------------------------------------------------
# Resolve MLX model pack
# ---------------------------------------------------------------------------
if [[ -z "$MODEL_PATH" ]]; then
    for candidate in \
        "models/corridorkey_mlx.safetensors" \
        "dist/CorridorKey.ofx.bundle/Contents/Resources/models/corridorkey_mlx.safetensors" \
        "build/debug-macos-portable/models/corridorkey_mlx.safetensors" \
        "build/release-macos-portable/models/corridorkey_mlx.safetensors"; do
        if [[ -f "$candidate" ]]; then
            MODEL_PATH="$candidate"
            break
        fi
    done
fi

if [[ -z "$MODEL_PATH" || ! -f "$MODEL_PATH" ]]; then
    echo "Could not locate corridorkey_mlx.safetensors. Pass --model PATH." >&2
    exit 3
fi
MODEL_PATH="$(cd "$(dirname "$MODEL_PATH")" && pwd)/$(basename "$MODEL_PATH")"

# ---------------------------------------------------------------------------
# Output path
# ---------------------------------------------------------------------------
TIMESTAMP="$(date -u +%Y%m%dT%H%M%SZ)"
HOSTNAME_SANE="$(hostname -s | tr -cd '[:alnum:]-')"
if [[ -z "$OUTPUT_PATH" ]]; then
    mkdir -p "dist"
    OUTPUT_PATH="dist/bench_m4_${HOSTNAME_SANE}_${TIMESTAMP}.json"
fi

TMPDIR_RUN="$(mktemp -d -t corridorkey_bench_mac_m4)"
trap 'rm -rf "$TMPDIR_RUN"' EXIT

# ---------------------------------------------------------------------------
# Optional powermetrics wrapper
# ---------------------------------------------------------------------------
POWER_PID=""
POWER_LOG=""
start_powermetrics() {
    local label="$1"
    if [[ "$USE_POWERMETRICS" -eq 0 ]]; then
        return
    fi
    if ! command -v powermetrics >/dev/null 2>&1; then
        echo "  (powermetrics not available, skipping power sampling)" >&2
        return
    fi
    POWER_LOG="$TMPDIR_RUN/power_${label}.log"
    if sudo -n true >/dev/null 2>&1; then
        sudo -n powermetrics \
            --samplers gpu_power,ane_power,cpu_power \
            -i 1000 \
            -o "$POWER_LOG" >/dev/null 2>&1 &
        POWER_PID=$!
    else
        echo "  (sudo not pre-authorized; run 'sudo -v' before re-invoking" \
             "with --powermetrics for power samples)" >&2
        POWER_LOG=""
    fi
}

stop_powermetrics() {
    if [[ -n "$POWER_PID" ]]; then
        sudo -n kill "$POWER_PID" >/dev/null 2>&1 || true
        wait "$POWER_PID" 2>/dev/null || true
        POWER_PID=""
    fi
}

# ---------------------------------------------------------------------------
# Environment context
# ---------------------------------------------------------------------------
HW_MODEL="$(sysctl -n hw.model 2>/dev/null || echo unknown)"
HW_MEMSIZE="$(sysctl -n hw.memsize 2>/dev/null || echo 0)"
HW_NCPU="$(sysctl -n hw.ncpu 2>/dev/null || echo 0)"
OS_VERSION="$(sw_vers -productVersion 2>/dev/null || echo unknown)"
OS_BUILD="$(sw_vers -buildVersion 2>/dev/null || echo unknown)"

# ---------------------------------------------------------------------------
# Benchmark loop
# ---------------------------------------------------------------------------
IFS=',' read -r -a RES_ARRAY <<<"$RESOLUTIONS"

RESOLUTION_JSON_ENTRIES=()
for res in "${RES_ARRAY[@]}"; do
    res="$(echo "$res" | tr -d '[:space:]')"
    [[ -z "$res" ]] && continue
    echo ""
    echo "=== Benchmark resolution ${res} ==="
    start_powermetrics "$res"
    OUT_JSON="$TMPDIR_RUN/bench_${res}.json"
    set +e
    "$CLI_PATH" benchmark \
        --model "$MODEL_PATH" \
        --resolution "$res" \
        --json >"$OUT_JSON" 2>"$TMPDIR_RUN/bench_${res}.err"
    rc=$?
    set -e
    stop_powermetrics
    if [[ $rc -ne 0 || ! -s "$OUT_JSON" ]]; then
        echo "  benchmark failed (rc=$rc):" >&2
        sed 's/^/    /' "$TMPDIR_RUN/bench_${res}.err" >&2 || true
        RESOLUTION_JSON_ENTRIES+=("{\"resolution\":${res},\"ok\":false,\"error\":\"benchmark_failed\",\"rc\":${rc}}")
        continue
    fi
    # Embed the benchmark JSON plus the power log path (if any).
    POWER_LOG_ABS=""
    if [[ -n "$POWER_LOG" && -s "$POWER_LOG" ]]; then
        POWER_LOG_ABS="$(cd "$(dirname "$POWER_LOG")" && pwd)/$(basename "$POWER_LOG")"
    fi
    ENTRY="$(python3 - "$OUT_JSON" "$res" "$POWER_LOG_ABS" <<'PY'
import json, sys, pathlib
bench_path, resolution, power_log = sys.argv[1], int(sys.argv[2]), sys.argv[3]
with open(bench_path) as f:
    bench = json.load(f)
out = {
    "resolution": resolution,
    "ok": True,
    "benchmark": bench,
}
if power_log:
    out["power_log"] = power_log
print(json.dumps(out))
PY
    )"
    RESOLUTION_JSON_ENTRIES+=("$ENTRY")
    echo "  ok"
done

# ---------------------------------------------------------------------------
# Aggregate
# ---------------------------------------------------------------------------
ENTRIES_CSV="$(IFS=,; echo "${RESOLUTION_JSON_ENTRIES[*]}")"

python3 - "$OUTPUT_PATH" "$TIMESTAMP" "$HW_MODEL" "$HW_MEMSIZE" "$HW_NCPU" \
    "$OS_VERSION" "$OS_BUILD" "$LABEL" "$CLI_PATH" "$MODEL_PATH" "$ENTRIES_CSV" <<'PY'
import json, sys, pathlib

(_, out_path, ts, hw_model, hw_memsize, hw_ncpu, os_ver, os_build,
 label, cli_path, model_path, entries_csv) = sys.argv

entries = []
if entries_csv.strip():
    entries = json.loads("[" + entries_csv + "]")

report = {
    "generator": "scripts/benchmark_mac_m4.sh",
    "timestamp_utc": ts,
    "label": label,
    "host": {
        "hw_model": hw_model,
        "hw_memsize_bytes": int(hw_memsize or 0),
        "hw_ncpu": int(hw_ncpu or 0),
        "os_version": os_ver,
        "os_build": os_build,
    },
    "binary": {
        "cli_path": cli_path,
        "model_path": model_path,
    },
    "resolutions": entries,
}

pathlib.Path(out_path).write_text(json.dumps(report, indent=2))
print(f"\nwrote {out_path}")
PY
