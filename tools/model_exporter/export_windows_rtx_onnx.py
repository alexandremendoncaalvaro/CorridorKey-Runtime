import argparse
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


def load_model_with_pos_embed_interpolation(checkpoint_path, img_size):
    import math
    import torch
    import torch.nn.functional as f
    from CorridorKeyModule.core.model_transformer import GreenFormer

    model = GreenFormer(
        encoder_name="hiera_base_plus_224.mae_in1k_ft_in1k",
        img_size=img_size,
        use_refiner=True,
    )
    model = model.to("cpu")
    model.eval()

    checkpoint = torch.load(checkpoint_path, map_location="cpu", weights_only=True)
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


def export_profile(profile, checkpoint_path, output_dir):
    import torch
    from torch.nn.attention import SDPBackend, sdpa_kernel

    disable_torch_compile_for_export(torch)

    resolution = int(profile["resolution"])
    onnx_config = profile["onnx_config"]
    input_shape = onnx_config["input_shape"]
    if input_shape != [resolution, resolution]:
        raise ValueError(f"Profile {profile['name']} input shape mismatch.")

    output_path = output_dir / profile["artifacts"]["raw_onnx"]
    output_path.parent.mkdir(parents=True, exist_ok=True)

    base_model = load_model_with_pos_embed_interpolation(checkpoint_path, resolution)

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
        dtype=torch.float32,
    )

    export_kwargs = dict(
        export_params=True,
        opset_version=16,
        do_constant_folding=True,
        input_names=[onnx_config["input_name"]],
        output_names=onnx_config["output_names"],
    )

    with warnings.catch_warnings():
        warnings.filterwarnings("ignore", category=UserWarning)
        warnings.filterwarnings("ignore", category=torch.jit.TracerWarning)
        with sdpa_kernel(SDPBackend.MATH):
            torch.onnx.export(model, dummy_input, str(output_path), **export_kwargs)

    print(f"[export] {profile['name']} -> {output_path}")
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
    args = parser.parse_args()

    repo_path = Path(args.repo_path).resolve()
    if not (repo_path / "CorridorKeyModule").exists():
        raise SystemExit(f"Invalid CorridorKey source repository: {repo_path}")

    sys.path.insert(0, str(repo_path))
    profiles = load_profiles(args.profiles)
    output_dir = Path(args.output_dir).resolve()
    output_dir.mkdir(parents=True, exist_ok=True)

    exported = []
    for profile in profiles:
        exported.append(
            {
                "name": profile["name"],
                "resolution": profile["resolution"],
                "raw_onnx": export_profile(profile, args.ckpt, output_dir),
            }
        )

    print(json.dumps({"exported_profiles": exported}, indent=2))


if __name__ == "__main__":
    main()
