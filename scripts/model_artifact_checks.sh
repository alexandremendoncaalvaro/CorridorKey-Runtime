#!/bin/bash

require_vcpkg_root() {
    if [ -z "${VCPKG_ROOT:-}" ]; then
        echo "ERROR: VCPKG_ROOT is required by CMakePresets.json." >&2
        exit 1
    fi

    if [ ! -d "$VCPKG_ROOT" ]; then
        echo "ERROR: VCPKG_ROOT does not exist: $VCPKG_ROOT" >&2
        exit 1
    fi
}

require_min_size() {
    local path="$1"
    local min_bytes="$2"
    local actual_bytes

    actual_bytes="$(stat -f%z "$path")"
    if [ "$actual_bytes" -lt "$min_bytes" ]; then
        echo "ERROR: File too small: $path ($actual_bytes bytes, expected >= $min_bytes)" >&2
        exit 1
    fi
}

is_git_lfs_pointer_file() {
    local path="$1"
    local header

    header="$(head -c 256 "$path" 2>/dev/null || true)"
    [[ "$header" == version\ https://git-lfs.github.com/spec/v1* ]] || return 1
    [[ "$header" == *"oid sha256:"* ]] || return 1
    [[ "$header" == *"size "* ]] || return 1
    return 0
}

require_real_model_artifact() {
    local path="$1"
    local min_bytes="$2"
    local label="${3:-model artifact}"

    if [ ! -f "$path" ]; then
        echo "ERROR: Missing required ${label}: $path" >&2
        exit 1
    fi

    if is_git_lfs_pointer_file "$path"; then
        echo "ERROR: ${label} is still a Git LFS pointer placeholder: $path" >&2
        exit 1
    fi

    require_min_size "$path" "$min_bytes"
}

write_macos_model_inventory() {
    local output_path="$1"
    local package_type="$2"
    local release_label="$3"
    local bundle_track="$4"
    local include_mlx_2048="${5:-1}"

    python3 - "$output_path" "$package_type" "$release_label" "$bundle_track" "$include_mlx_2048" <<'PY'
import json
import sys
from pathlib import Path

output_path = Path(sys.argv[1])
package_type = sys.argv[2]
release_label = sys.argv[3]
bundle_track = sys.argv[4]
include_mlx_2048 = sys.argv[5] == "1"

expected_models = [
    "corridorkey_int8_512.onnx",
    "corridorkey_mlx.safetensors",
    "corridorkey_mlx_bridge_512.mlxfn",
    "corridorkey_mlx_bridge_768.mlxfn",
    "corridorkey_mlx_bridge_1024.mlxfn",
    "corridorkey_mlx_bridge_1536.mlxfn",
]
if include_mlx_2048:
    expected_models.append("corridorkey_mlx_bridge_2048.mlxfn")

inventory = {
    "package_type": package_type,
    "model_profile": "apple-silicon-mlx",
    "bundle_track": bundle_track,
    "release_label": release_label,
    "optimization_profile_id": "apple-silicon-mlx",
    "optimization_profile_label": "Apple Silicon MLX",
    "backend_intent": "mlx",
    "fallback_policy": "curated_primary_pack_with_bridge_exports",
    "warmup_policy": "bridge_import_and_callable_compile",
    "certification_tier": "official_apple_silicon_track",
    "unrestricted_quality_attempt": True,
    "expected_models": expected_models,
    "present_models": expected_models,
    "missing_models": [],
    "missing_count": 0,
    "compiled_context_models": [],
    "expected_compiled_context_models": [],
    "missing_compiled_context_models": [],
    "compiled_context_complete": True,
}

output_path.write_text(json.dumps(inventory, indent=2) + "\n", encoding="utf-8")
PY
}
