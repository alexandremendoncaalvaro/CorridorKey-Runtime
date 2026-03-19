#!/bin/bash
set -u

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="${CORRIDORKEY_SUPPORT_ROOT:-$(cd "${SCRIPT_DIR}/.." && pwd)}"
EMBEDDED_VENV_DIR="${REPO_ROOT}/.venv-macos-mlx"
EMBEDDED_PYTHON_HOME="${CORRIDORKEY_EMBEDDED_PYTHON_HOME:-${EMBEDDED_VENV_DIR}/python-home}"
MODELS_DIR="${REPO_ROOT}/models"
OUTPUTS_DIR="${CORRIDORKEY_SUPPORT_OUTPUT_ROOT:-${REPO_ROOT}/outputs}"
TIMESTAMP="$(date +"%Y%m%d_%H%M%S")"
SUPPORT_DIR="${OUTPUTS_DIR}/mlx_2048_support_${TIMESTAMP}"
LOG_PATH="${SUPPORT_DIR}/generation.log"
REPORT_PATH="${SUPPORT_DIR}/send_this_to_support.txt"
SYSTEM_REPORT_PATH="${SUPPORT_DIR}/system_report.txt"
SUPPORT_ZIP_PATH="${SUPPORT_DIR}.zip"
BRIDGE_PATH="${MODELS_DIR}/corridorkey_mlx_bridge_2048.mlxfn"
WEIGHTS_PATH="${MODELS_DIR}/corridorkey_mlx.safetensors"
CHECKPOINT_PATH="${MODELS_DIR}/CorridorKey.pth"
DRY_RUN=0
FORCE_REGENERATE=0
RECOMMENDED_MEMORY_GB=24

print_divider() {
    printf '%s\n' "------------------------------------------------------------"
}

print_step() {
    print_divider
    printf '%s\n' "$1"
    print_divider
}

pause_if_interactive() {
    if [ -t 0 ]; then
        printf '\nPress Enter to close this window...'
        read -r _ || true
    fi
}

fail() {
    printf '\nERROR: %s\n' "$1" >&2
    printf 'Support log: %s\n' "$LOG_PATH" >&2
    pause_if_interactive
    exit 1
}

confirm_continue() {
    local prompt="$1"
    local answer
    printf '%s [y/N]: ' "$prompt"
    read -r answer || true
    case "$answer" in
        y | Y | yes | YES)
            return 0
            ;;
        *)
            return 1
            ;;
    esac
}

write_support_files() {
    local bridge_size
    local memory_bytes
    local memory_gb
    local sha256

    mkdir -p "$SUPPORT_DIR"
    memory_bytes="$(sysctl -n hw.memsize 2>/dev/null || echo 0)"
    memory_gb=$((memory_bytes / 1024 / 1024 / 1024))
    bridge_size="$(stat -f%z "$BRIDGE_PATH")"
    sha256="$(shasum -a 256 "$BRIDGE_PATH" | awk '{print $1}')"

    {
        printf 'date=%s\n' "$(date)"
        printf 'repo_root=%s\n' "$REPO_ROOT"
        printf 'bridge_path=%s\n' "$BRIDGE_PATH"
        printf 'bridge_size_bytes=%s\n' "$bridge_size"
        printf 'bridge_sha256=%s\n' "$sha256"
        printf 'weights_path=%s\n' "$WEIGHTS_PATH"
        printf 'weights_exists=%s\n' "$([ -f "$WEIGHTS_PATH" ] && echo yes || echo no)"
        printf 'checkpoint_path=%s\n' "$CHECKPOINT_PATH"
        printf 'checkpoint_exists=%s\n' "$([ -f "$CHECKPOINT_PATH" ] && echo yes || echo no)"
        printf 'machine=%s\n' "$(sysctl -n hw.model 2>/dev/null || echo unknown)"
        printf 'memory_gb=%s\n' "$memory_gb"
        printf 'architecture=%s\n' "$(uname -m)"
        printf 'macos=%s\n' "$(sw_vers -productVersion 2>/dev/null || echo unknown)"
        printf 'python=%s\n' "${PYTHON_BIN:-unknown}"
    } > "$SYSTEM_REPORT_PATH"

    {
        printf 'Please send these back to support:\n\n'
        printf '1. %s\n' "$BRIDGE_PATH"
        printf '2. %s\n' "$LOG_PATH"
        printf '3. %s\n' "$SYSTEM_REPORT_PATH"
        printf '\nBridge SHA-256:\n%s\n' "$sha256"
    } > "$REPORT_PATH"

    ditto -c -k --keepParent "$SUPPORT_DIR" "$SUPPORT_ZIP_PATH" >/dev/null 2>&1 || true
}

find_python() {
    local candidate
    local candidates=()

    if [ -n "${CORRIDORKEY_MLX_PYTHON:-}" ]; then
        candidates+=("${CORRIDORKEY_MLX_PYTHON}")
    fi
    if [ -n "${VIRTUAL_ENV:-}" ] && [ -x "${VIRTUAL_ENV}/bin/python" ]; then
        candidates+=("${VIRTUAL_ENV}/bin/python")
    fi
    if [ -x "${EMBEDDED_VENV_DIR}/bin/python" ]; then
        candidates+=("${EMBEDDED_VENV_DIR}/bin/python")
    fi
    if command -v python3 >/dev/null 2>&1; then
        candidates+=("$(command -v python3)")
    fi

    for candidate in "${candidates[@]}"; do
        [ -x "$candidate" ] || continue
        if run_python_candidate "$candidate" - <<'PY' >/dev/null 2>&1
import mlx.core  # noqa: F401
import corridorkey_mlx  # noqa: F401
PY
        then
            printf '%s\n' "$candidate"
            return 0
        fi
    done

    return 1
}

python_uses_embedded_home() {
    case "$1" in
        "${EMBEDDED_VENV_DIR}/bin/"*)
            [ -d "${EMBEDDED_PYTHON_HOME}/lib" ]
            ;;
        *)
            return 1
            ;;
    esac
}

run_python_candidate() {
    local candidate="$1"
    shift

    if python_uses_embedded_home "$candidate"; then
        PYTHONHOME="${EMBEDDED_PYTHON_HOME}" PYTHONNOUSERSITE=1 "$candidate" "$@"
        return
    fi

    "$candidate" "$@"
}

existing_bridge_is_valid() {
    if [ ! -f "$BRIDGE_PATH" ]; then
        return 1
    fi

    local size_bytes
    size_bytes="$(stat -f%z "$BRIDGE_PATH" 2>/dev/null || echo 0)"
    [ "$size_bytes" -ge 200000000 ]
}

for arg in "$@"; do
    case "$arg" in
        --dry-run)
            DRY_RUN=1
            ;;
        --force)
            FORCE_REGENERATE=1
            ;;
        *)
            printf 'Unknown argument: %s\n' "$arg" >&2
            printf 'Supported arguments: --dry-run, --force\n' >&2
            exit 1
            ;;
    esac
done

mkdir -p "$SUPPORT_DIR"
exec > >(tee -a "$LOG_PATH") 2>&1

print_step "CorridorKey MLX 2048 Bridge Generator"
printf 'Repository: %s\n' "$REPO_ROOT"
printf 'Log file: %s\n' "$LOG_PATH"

[ -d "$MODELS_DIR" ] || fail "Could not find the models folder at ${MODELS_DIR}."
[ "$(uname -s)" = "Darwin" ] || fail "This script must be run on macOS."
[ "$(uname -m)" = "arm64" ] || fail "This script must be run on an Apple Silicon Mac."

MEMORY_BYTES="$(sysctl -n hw.memsize 2>/dev/null || echo 0)"
MEMORY_GB=$((MEMORY_BYTES / 1024 / 1024 / 1024))
printf 'Detected memory: %s GB\n' "$MEMORY_GB"

if [ "$MEMORY_GB" -lt "$RECOMMENDED_MEMORY_GB" ]; then
    printf '\nThis Mac is below the recommended memory for a 2048 bridge export.\n'
    printf 'Recommended: %s GB or more\n' "$RECOMMENDED_MEMORY_GB"
    printf 'Detected: %s GB\n' "$MEMORY_GB"
    if [ "$DRY_RUN" -eq 0 ] && ! confirm_continue "Do you want to try anyway?"; then
        fail "Stopped before generation because this Mac is below the recommended memory."
    fi
fi

PYTHON_BIN="$(find_python)" || fail "Could not find a Python environment with mlx and corridorkey_mlx installed."
printf 'Using Python: %s\n' "$PYTHON_BIN"

SOURCE_ARGS=()
SOURCE_DESCRIPTION=""
if [ -f "$WEIGHTS_PATH" ]; then
    SOURCE_ARGS=(--weights-path "$WEIGHTS_PATH")
    SOURCE_DESCRIPTION="existing MLX weights"
elif [ -f "$CHECKPOINT_PATH" ]; then
    SOURCE_ARGS=(--checkpoint "$CHECKPOINT_PATH")
    SOURCE_DESCRIPTION="PyTorch checkpoint"
else
    fail "Could not find ${WEIGHTS_PATH} or ${CHECKPOINT_PATH}."
fi

printf 'Source: %s\n' "$SOURCE_DESCRIPTION"

if [ "$FORCE_REGENERATE" -eq 0 ] && existing_bridge_is_valid; then
    print_step "A valid 2048 bridge already exists"
    write_support_files
    printf 'Bridge: %s\n' "$BRIDGE_PATH"
    printf 'Support report: %s\n' "$SUPPORT_ZIP_PATH"
    open -R "$BRIDGE_PATH" >/dev/null 2>&1 || true
    open -R "$REPORT_PATH" >/dev/null 2>&1 || true
    pause_if_interactive
    exit 0
fi

COMMAND_ARGS=(
    "${REPO_ROOT}/scripts/prepare_mlx_model_pack.py"
    --output-dir "$MODELS_DIR"
    --bridge-resolutions 2048
    "${SOURCE_ARGS[@]}"
)

print_step "Generation command"
if python_uses_embedded_home "$PYTHON_BIN"; then
    printf 'PYTHONHOME=%q PYTHONNOUSERSITE=1 ' "$EMBEDDED_PYTHON_HOME"
fi
printf '%q ' "$PYTHON_BIN" "${COMMAND_ARGS[@]}"
printf '\n'

if [ "$DRY_RUN" -eq 1 ]; then
    print_step "Dry run complete"
    printf 'No files were generated because --dry-run was used.\n'
    pause_if_interactive
    exit 0
fi

print_step "Generating corridorkey_mlx_bridge_2048.mlxfn"
run_python_candidate "$PYTHON_BIN" "${COMMAND_ARGS[@]}" || fail "Bridge generation failed."

[ -f "$BRIDGE_PATH" ] || fail "The generation step finished, but ${BRIDGE_PATH} was not created."

BRIDGE_SIZE="$(stat -f%z "$BRIDGE_PATH" 2>/dev/null || echo 0)"
[ "$BRIDGE_SIZE" -ge 200000000 ] || fail "The bridge file was created, but it is too small to be valid."

write_support_files

print_step "Done"
printf 'Bridge created successfully.\n'
printf 'Bridge: %s\n' "$BRIDGE_PATH"
printf 'Support report: %s\n' "$SUPPORT_ZIP_PATH"
printf '\nPlease send both items back to support.\n'

open -R "$BRIDGE_PATH" >/dev/null 2>&1 || true
open -R "$REPORT_PATH" >/dev/null 2>&1 || true

pause_if_interactive
