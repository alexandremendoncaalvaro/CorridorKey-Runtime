import argparse
import gc
import json
import sys
from pathlib import Path

from export_windows_rtx_onnx import (
    load_model_with_pos_embed_interpolation,
    load_profiles,
    normalize_device_name,
    select_profiles,
)


PACKAGED_WINDOWS_RTX_RESOLUTIONS = {512, 1024, 1536, 2048}


def torch_torchscript_artifact_name(profile):
    artifacts = profile.get("artifacts", {})
    explicit_name = artifacts.get("torch_torchscript")
    if explicit_name:
        return explicit_name

    runtime_name = artifacts.get("runtime_onnx")
    resolution = int(profile["resolution"])
    if runtime_name:
        stem = Path(runtime_name).stem
        if stem.startswith("corridorkey_fp16_"):
            return stem.replace("corridorkey_fp16_", "corridorkey_torchtrt_fp16_") + ".ts"

    return f"corridorkey_torchtrt_fp16_{resolution}.ts"


def default_torch_profiles(profiles):
    return [
        profile
        for profile in profiles
        if int(profile["resolution"]) in PACKAGED_WINDOWS_RTX_RESOLUTIONS
    ]


def build_compile_kwargs(profile, cache_root):
    resolution = int(profile["resolution"])
    common_config = profile.get("backend_config", {}).get("common_config", {})
    profile_cache_root = cache_root / f"torchtrt_{resolution}"
    engine_cache_dir = profile_cache_root / "engines"
    timing_cache_path = profile_cache_root / "timing_cache.bin"

    return {
        "ir": "dynamo",
        "enabled_precisions": {"fp16"},
        "use_explicit_typing": False,
        "use_python_runtime": False,
        "require_full_compilation": True,
        "min_block_size": 1,
        "optimization_level": 5,
        "use_fast_partitioner": False,
        "hardware_compatible": True,
        "workspace_size": int(common_config.get("max_workspace_size", 0)),
        "num_avg_timing_iters": 8,
        "cache_built_engines": True,
        "reuse_cached_engines": True,
        "engine_cache_dir": str(engine_cache_dir),
        "timing_cache_path": str(timing_cache_path),
        "immutable_weights": True,
    }


def cleanup_export_device(torch_module):
    if hasattr(torch_module.cuda, "synchronize"):
        torch_module.cuda.synchronize()
    if hasattr(torch_module.cuda, "empty_cache"):
        torch_module.cuda.empty_cache()
    gc.collect()


def export_profile(profile, checkpoint_path, output_dir, repo_path, cache_root):
    import torch
    import torch_tensorrt

    resolution = int(profile["resolution"])
    artifact_name = torch_torchscript_artifact_name(profile)
    output_path = output_dir / artifact_name
    output_path.parent.mkdir(parents=True, exist_ok=True)

    model = load_model_with_pos_embed_interpolation(checkpoint_path, resolution, "cuda").half()

    class TorchWrapper(torch.nn.Module):
        def __init__(self, wrapped_model):
            super().__init__()
            self.model = wrapped_model

        def forward(self, x):
            out = self.model(x)
            return out["alpha"], out.get("fg", x[:, :3, :, :])

    wrapped = TorchWrapper(model).eval().cuda()
    dummy_input = torch.randn(1, 4, resolution, resolution, device="cuda", dtype=torch.float16)
    compile_kwargs = build_compile_kwargs(profile, cache_root)
    compile_kwargs["enabled_precisions"] = {torch.float16}

    with torch.inference_mode():
        compiled = torch_tensorrt.compile(wrapped, inputs=[dummy_input], **compile_kwargs)
        traced = torch.jit.trace(compiled, dummy_input, strict=False)
        traced.save(str(output_path))
        loaded = torch.jit.load(str(output_path), map_location="cuda")
        loaded.eval()
        smoke_output = loaded(dummy_input)

    if not isinstance(smoke_output, tuple) or len(smoke_output) < 2:
        raise RuntimeError(
            f"Torch-TensorRT smoke test returned an unexpected output contract for {artifact_name}"
        )

    alpha, fg = smoke_output[0], smoke_output[1]
    expected_alpha = (1, 1, resolution, resolution)
    expected_fg = (1, 3, resolution, resolution)
    if tuple(alpha.shape) != expected_alpha or tuple(fg.shape) != expected_fg:
        raise RuntimeError(
            f"Torch-TensorRT smoke test shape mismatch for {artifact_name}: "
            f"alpha={tuple(alpha.shape)} fg={tuple(fg.shape)}"
        )

    cleanup_export_device(torch)
    return {
        "name": profile["name"],
        "resolution": resolution,
        "torch_torchscript": str(output_path),
        "cache_dir": str(cache_root / f"torchtrt_{resolution}"),
        "repo_path": str(repo_path),
    }


def main():
    parser = argparse.ArgumentParser(
        description="Export Windows RTX Torch-TensorRT TorchScript artifacts for the C++ runtime."
    )
    parser.add_argument("--profiles", type=str, required=True, help="Deploy profile JSON path")
    parser.add_argument("--ckpt", type=str, required=True, help="Path to the CorridorKey checkpoint")
    parser.add_argument(
        "--output-dir",
        type=str,
        required=True,
        help="Directory where Torch-TensorRT TorchScript artifacts will be written",
    )
    parser.add_argument(
        "--repo-path",
        type=str,
        required=True,
        help="Path to the CorridorKey Engine/source repository",
    )
    parser.add_argument(
        "--cache-dir",
        type=str,
        default="",
        help="Optional directory for Torch-TensorRT engine and timing caches",
    )
    parser.add_argument(
        "--device",
        type=str,
        default="cuda",
        help="Export device to use. Torch-TensorRT export requires cuda.",
    )
    parser.add_argument(
        "--profile-name",
        action="append",
        default=[],
        help="Explicit deploy profile name to export. Repeat to export multiple profiles.",
    )
    parser.add_argument(
        "--continue-on-error",
        action="store_true",
        help="Continue exporting other profiles even if one resolution fails.",
    )
    args = parser.parse_args()

    device_name = normalize_device_name(args.device)
    if device_name != "cuda":
        raise SystemExit("Torch-TensorRT export requires --device cuda.")

    repo_path = Path(args.repo_path).resolve()
    if not (repo_path / "CorridorKeyModule").exists():
        raise SystemExit(f"Invalid CorridorKey source repository: {repo_path}")

    sys.path.insert(0, str(repo_path))

    profiles = load_profiles(args.profiles)
    if args.profile_name:
        selected_profiles = select_profiles(profiles, args.profile_name)
    else:
        selected_profiles = default_torch_profiles(profiles)

    output_dir = Path(args.output_dir).resolve()
    output_dir.mkdir(parents=True, exist_ok=True)
    cache_root = (
        Path(args.cache_dir).resolve()
        if args.cache_dir
        else (output_dir / ".torchtrt_cache")
    )
    cache_root.mkdir(parents=True, exist_ok=True)

    exported = []
    failed = []
    for profile in selected_profiles:
        try:
            exported.append(
                export_profile(
                    profile,
                    Path(args.ckpt).resolve(),
                    output_dir,
                    repo_path,
                    cache_root,
                )
            )
            print(
                f"[export] {profile['name']} -> {output_dir / torch_torchscript_artifact_name(profile)}"
            )
        except Exception as error:  # noqa: BLE001
            failure = {
                "name": profile["name"],
                "resolution": int(profile["resolution"]),
                "error": str(error),
            }
            failed.append(failure)
            print(f"[error] {profile['name']} -> {error}", file=sys.stderr)
            if not args.continue_on_error:
                print(json.dumps({"exported_profiles": exported, "failed_profiles": failed}, indent=2))
                raise SystemExit(1) from error

    print(json.dumps({"exported_profiles": exported, "failed_profiles": failed}, indent=2))
    if failed:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
