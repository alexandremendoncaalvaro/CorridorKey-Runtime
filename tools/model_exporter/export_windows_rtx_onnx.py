import argparse
import gc
import inspect
import json
import os
import sys
import warnings
from pathlib import Path


def disable_torch_compile_for_export(torch_module):
    if not hasattr(torch_module, "compile"):
        return

    def passthrough_compile(*args, **kwargs):
        if len(args) == 1 and callable(args[0]) and not kwargs:
            return args[0]

        def decorator(obj):
            return obj

        return decorator

    torch_module.compile = passthrough_compile


def normalize_device_name(device_name):
    normalized = device_name.strip().lower()
    if normalized in ("cuda", "gpu"):
        return "cuda"
    if normalized == "cpu":
        return "cpu"
    raise ValueError(f"Unsupported export device: {device_name}")


def load_model_with_pos_embed_interpolation(checkpoint_path, img_size, device_name):
    import math
    import torch
    import torch.nn.functional as f
    from CorridorKeyModule.core.model_transformer import GreenFormer

    device = torch.device(device_name)
    model = GreenFormer(
        encoder_name="hiera_base_plus_224.mae_in1k_ft_in1k",
        img_size=img_size,
        use_refiner=True,
    )
    model = model.to(device)
    model.eval()

    checkpoint = torch.load(checkpoint_path, map_location=device, weights_only=True)
    state_dict = checkpoint.get("state_dict", checkpoint)

    new_state_dict = {}
    model_state = model.state_dict()

    for key, value in state_dict.items():
        if key.startswith("_orig_mod."):
            key = key[10:]

        if "pos_embed" in key and key in model_state and value.shape != model_state[key].shape:
            source_tokens = value.shape[1]
            channels = value.shape[2]
            source_grid = int(math.sqrt(source_tokens))
            target_grid = int(math.sqrt(model_state[key].shape[1]))
            value_image = value.permute(0, 2, 1).view(1, channels, source_grid, source_grid)
            value_resized = f.interpolate(
                value_image,
                size=(target_grid, target_grid),
                mode="bicubic",
                align_corners=False,
            )
            value = value_resized.flatten(2).transpose(1, 2)

        new_state_dict[key] = value

    missing, unexpected = model.load_state_dict(new_state_dict, strict=False)
    if missing:
        print(f"[warning] Missing checkpoint keys for {img_size}px: {missing}")
    if unexpected:
        print(f"[warning] Unexpected checkpoint keys for {img_size}px: {unexpected}")

    return model


def load_profiles(path):
    with open(path, "r", encoding="utf-8") as handle:
        data = json.load(handle)

    if data.get("pipeline") != "windows_rtx_mmdeploy_style":
        raise ValueError("Unsupported deploy profile pipeline.")

    profiles = data.get("profiles", [])
    if not profiles:
        raise ValueError("No deploy profiles were defined.")

    return profiles


def build_export_kwargs(onnx_config):
    shape_policy = onnx_config["shape_policy"]
    if shape_policy not in ("static", "dynamic"):
        raise ValueError(f"Unsupported shape policy: {shape_policy}")

    export_kwargs = dict(
        export_params=True,
        opset_version=16,
        do_constant_folding=True,
        input_names=[onnx_config["input_name"]],
        output_names=onnx_config["output_names"],
    )

    if shape_policy == "dynamic":
        input_name = onnx_config["input_name"]
        output_names = onnx_config["output_names"]
        export_kwargs["dynamic_axes"] = {
            input_name: {0: "batch_size"},
            output_names[0]: {0: "batch_size"},
            output_names[1]: {0: "batch_size"},
        }

    return export_kwargs


def finalize_export_kwargs(export_kwargs, export_callable):
    finalized = dict(export_kwargs)

    try:
        parameters = inspect.signature(export_callable).parameters
    except (TypeError, ValueError):
        parameters = {}

    if "dynamo" in parameters:
        finalized["dynamo"] = False

    return finalized


def select_profiles(profiles, selected_profile_names):
    if not selected_profile_names:
        return profiles

    selected_names = {name.strip() for name in selected_profile_names if name.strip()}
    filtered = [profile for profile in profiles if profile["name"] in selected_names]
    if len(filtered) != len(selected_names):
        known = {profile["name"] for profile in profiles}
        missing = sorted(selected_names - known)
        raise ValueError(f"Unknown deploy profiles: {', '.join(missing)}")

    return filtered


def cleanup_export_device(torch_module, device_name):
    if device_name != "cuda":
        return

    if hasattr(torch_module.cuda, "synchronize"):
        torch_module.cuda.synchronize()
    if hasattr(torch_module.cuda, "empty_cache"):
        torch_module.cuda.empty_cache()
    gc.collect()


def export_profile(profile, checkpoint_path, output_dir, device_name):
    import torch
    from torch.nn.attention import SDPBackend, sdpa_kernel

    disable_torch_compile_for_export(torch)
    device_name = normalize_device_name(device_name)
    if device_name == "cuda" and not torch.cuda.is_available():
        raise RuntimeError("CUDA export was requested, but CUDA is not available.")

    resolution = int(profile["resolution"])
    onnx_config = profile["onnx_config"]
    input_shape = onnx_config["input_shape"]
    if input_shape != [resolution, resolution]:
        raise ValueError(f"Profile {profile['name']} input shape mismatch.")

    output_path = output_dir / profile["artifacts"]["raw_onnx"]
    output_path.parent.mkdir(parents=True, exist_ok=True)

    base_model = load_model_with_pos_embed_interpolation(checkpoint_path, resolution, device_name)

    class OnnxWrapper(torch.nn.Module):
        def __init__(self, model):
            super().__init__()
            self.model = model

        def forward(self, x):
            out = self.model(x)
            return out["alpha"], out.get("fg", x[:, :3, :, :])

    model = OnnxWrapper(base_model)
    dummy_input = torch.randn(
        onnx_config["batch_size"],
        4,
        resolution,
        resolution,
        device=device_name,
        dtype=torch.float32,
    )

    export_fn = torch.onnx.export
    export_kwargs = finalize_export_kwargs(build_export_kwargs(onnx_config), export_fn)

    with warnings.catch_warnings():
        warnings.filterwarnings("ignore", category=UserWarning)
        warnings.filterwarnings("ignore", category=torch.jit.TracerWarning)
        with sdpa_kernel(SDPBackend.MATH):
            export_fn(model, dummy_input, str(output_path), **export_kwargs)

    print(f"[export] {profile['name']} -> {output_path}")
    cleanup_export_device(torch, device_name)
    return str(output_path)


def main():
    parser = argparse.ArgumentParser(
        description="Export explicit Windows RTX deploy profiles to raw ONNX IR."
    )
    parser.add_argument("--profiles", type=str, required=True, help="Deploy profile JSON path")
    parser.add_argument("--ckpt", type=str, required=True, help="Path to the CorridorKey checkpoint")
    parser.add_argument(
        "--output-dir",
        type=str,
        required=True,
        help="Directory where raw ONNX exports will be written",
    )
    parser.add_argument(
        "--repo-path",
        type=str,
        required=True,
        help="Path to the CorridorKey Engine/source repository",
    )
    parser.add_argument(
        "--device",
        type=str,
        default="cpu",
        help="Export device to use: cpu or cuda",
    )
    parser.add_argument(
        "--profile-name",
        action="append",
        default=[],
        help="Explicit deploy profile name to export. Repeat to export multiple profiles.",
    )
    args = parser.parse_args()

    repo_path = Path(args.repo_path).resolve()
    if not (repo_path / "CorridorKeyModule").exists():
        raise SystemExit(f"Invalid CorridorKey source repository: {repo_path}")

    sys.path.insert(0, str(repo_path))
    profiles = select_profiles(load_profiles(args.profiles), args.profile_name)
    output_dir = Path(args.output_dir).resolve()
    output_dir.mkdir(parents=True, exist_ok=True)

    exported = []
    for profile in profiles:
        exported.append(
            {
                "name": profile["name"],
                "resolution": profile["resolution"],
                "raw_onnx": export_profile(profile, args.ckpt, output_dir, args.device),
            }
        )

    print(json.dumps({"exported_profiles": exported}, indent=2))


if __name__ == "__main__":
    main()
