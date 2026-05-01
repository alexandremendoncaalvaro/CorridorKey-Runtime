import os
import sys
import math
import argparse
import warnings
import tempfile
import subprocess
import shutil


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


# Empirically validated against the green and blue checkpoints currently in
# C:\Dev\CorridorKey\CorridorKeyModule\checkpoints (CorridorKey_v1.0.pth from
# 2026-04-14 and CorridorKeyBlue_1.0.pth from 2026-04-30): with this exact
# upstream SHA, load_state_dict produces empty missing/unexpected key lists
# after the pos_embed interpolation pass. Bumping this pin requires re-running
# `uv run python export_onnx.py --ckpt <pth>` for both checkpoints and
# confirming that "Missing keys" / "Unexpected keys" lines stay absent.
NIKO_UPSTREAM_PIN = "422f9999d1d83323534d2da9d776086a3134050d"


def clone_original_repo():
    print(f"[Info] Cloning original CorridorKey repository (pinned to {NIKO_UPSTREAM_PIN[:12]})...")
    temp_dir = tempfile.mkdtemp(prefix="corridorkey_repo_")
    try:
        # Full clone (no --depth) so we can checkout the pinned SHA. Shallow
        # clones cannot reach arbitrary commits without server-side allowance.
        subprocess.run(
            ["git", "clone", "https://github.com/nikopueringer/CorridorKey.git", temp_dir],
            check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL
        )
        subprocess.run(
            ["git", "-C", temp_dir, "checkout", "--quiet", NIKO_UPSTREAM_PIN],
            check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL
        )
        return temp_dir
    except subprocess.CalledProcessError as e:
        print(f"[Error] Failed to clone or checkout pinned upstream: {e}")
        shutil.rmtree(temp_dir, ignore_errors=True)
        sys.exit(1)


def load_model_with_pos_embed_interpolation(checkpoint_path, img_size, allow_key_drift=False):
    """Load GreenFormer with position embedding interpolation for arbitrary resolutions.

    Replicates the logic from CorridorKeyEngine._load_model() but works on all
    platforms (the original CorridorKeyEngine only assigns self.model on
    Linux/Windows due to a torch.compile platform guard).
    """
    import torch
    import torch.nn.functional as F
    from CorridorKeyModule.core.model_transformer import GreenFormer

    print(f"[Info] Loading GreenFormer at {img_size}x{img_size}...")
    model = GreenFormer(
        encoder_name="hiera_base_plus_224.mae_in1k_ft_in1k",
        img_size=img_size,
        use_refiner=True,
    )
    model = model.to("cpu")
    model.eval()

    device = "cpu"
    print(f"[Info] Using device: {device}")
    model = model.to(device)

    checkpoint = torch.load(checkpoint_path, map_location=device, weights_only=True)
    state_dict = checkpoint.get("state_dict", checkpoint)

    new_state_dict = {}
    model_state = model.state_dict()

    for k, v in state_dict.items():
        if k.startswith("_orig_mod."):
            k = k[10:]

        if "pos_embed" in k and k in model_state:
            if v.shape != model_state[k].shape:
                print(f"  Resizing {k} from {v.shape} to {model_state[k].shape}")
                N_src = v.shape[1]
                C = v.shape[2]
                grid_src = int(math.sqrt(N_src))
                grid_dst = int(math.sqrt(model_state[k].shape[1]))
                v_img = v.permute(0, 2, 1).view(1, C, grid_src, grid_src)
                v_resized = F.interpolate(
                    v_img, size=(grid_dst, grid_dst), mode="bicubic", align_corners=False
                )
                v = v_resized.flatten(2).transpose(1, 2)

        new_state_dict[k] = v

    missing, unexpected = model.load_state_dict(new_state_dict, strict=False)

    # After the pos_embed interpolation pass, missing/unexpected lists must be
    # empty. A non-empty list means the upstream GreenFormer architecture has
    # drifted from the checkpoint we are loading -- strict=False would silently
    # leave layers either untrained (missing) or unused (unexpected), producing
    # an ONNX whose math no longer matches the original PyTorch model. Fail
    # loudly so this never reaches a release. --allow-key-drift exists for
    # deliberate upstream-bump or training experiments; never use it in CI or
    # when packaging shipped models.
    if missing or unexpected:
        message_lines = [
            "[Error] state_dict mismatch between checkpoint and GreenFormer.",
            f"  checkpoint: {checkpoint_path}",
            f"  resolution: {img_size}",
        ]
        if missing:
            message_lines.append(f"  missing keys ({len(missing)}): {missing}")
        if unexpected:
            message_lines.append(f"  unexpected keys ({len(unexpected)}): {unexpected}")
        message_lines.append(
            "  Bump NIKO_UPSTREAM_PIN to a commit whose GreenFormer matches this "
            "checkpoint, or pass --repo-path to a known-good local checkout."
        )
        if not allow_key_drift:
            for line in message_lines:
                print(line, file=sys.stderr)
            print(
                "  Pass --allow-key-drift to override (training experiments only -- "
                "the resulting ONNX has untrained or stranded layers).",
                file=sys.stderr,
            )
            sys.exit(1)
        for line in message_lines:
            print(line.replace("[Error]", "[Warning]"))
        print("  --allow-key-drift was set; continuing despite drift.")

    return model


def export_resolution(args, resolution):
    import torch
    disable_torch_compile_for_export(torch)

    # Per-resolution static is the contract the canonical pipeline expects:
    # corridorkey[_blue]_fp32_<res>.onnx regardless of static-ness, with the
    # static export only applied to the rungs TensorRT requires (1536, 2048).
    # The legacy --static flag is retained, but it keeps the historical
    # _static_<res>.onnx suffix to avoid breaking any standalone caller that
    # already relied on it.
    static_resolutions = getattr(args, 'static_resolutions', None) or []
    static_global = getattr(args, 'static', False)
    static = (resolution in static_resolutions) or static_global
    use_legacy_naming = static_global and not static_resolutions
    suffix = f'_static_{resolution}' if (static and use_legacy_naming) else f'_{resolution}'
    out_path = args.out.replace('.onnx', f'{suffix}.onnx')
    mode_label = "STATIC" if static else "DYNAMIC"
    print(f"\n[Info] === Exporting Resolution {resolution}x{resolution} ({mode_label}) ===")

    base_model = load_model_with_pos_embed_interpolation(
        args.ckpt, resolution, allow_key_drift=getattr(args, 'allow_key_drift', False)
    )

    class ONNXWrapper(torch.nn.Module):
        def __init__(self, model):
            super().__init__()
            self.model = model

        def forward(self, x):
            out = self.model(x)
            return out["alpha"], out.get("fg", x[:, :3, :, :])

    device = next(base_model.parameters()).device
    model = ONNXWrapper(base_model)
    dummy_x = torch.randn(1, 4, resolution, resolution, device=device)

    if resolution >= 1536:
        print(f"[Info] Forcing FP16 mode for {resolution}px to save memory...")
        model = model.half()
        dummy_x = dummy_x.half()

    print(f"[Info] Exporting ONNX model to {out_path}...")
    os.makedirs(os.path.dirname(os.path.abspath(out_path)), exist_ok=True)

    # Static export omits dynamic_axes so all dimensions (including batch_size=1) are baked in.
    # This is required for TensorRT compatibility at 1536+ resolutions, where dynamic Shape ops
    # cause TRT to compute impossible intermediate tensor volumes that exceed its 2^31 limit.
    export_kwargs = dict(
        export_params=True,
        opset_version=16,
        do_constant_folding=True,
        input_names=['input_rgb_hint'],
        output_names=['alpha', 'fg'],
    )
    if not static:
        export_kwargs['dynamic_axes'] = {
            'input_rgb_hint': {0: 'batch_size'},
            'alpha': {0: 'batch_size'},
            'fg': {0: 'batch_size'}
        }

    with warnings.catch_warnings():
        warnings.filterwarnings("ignore", category=UserWarning)
        warnings.filterwarnings("ignore", category=torch.jit.TracerWarning)
        from torch.nn.attention import SDPBackend, sdpa_kernel

        # Use the TorchScript-based exporter that ships in torch 2.3.1+cu121
        # (the version pinned in pyproject.toml). The Hiera model uses 5D SDPA
        # tensors [batch, units, tokens, heads, dim] that the dynamo-based
        # exporter (introduced in torch 2.5) cannot translate. SDPBackend.MATH
        # decomposes SDPA into matmul+softmax before the tracer sees it.
        # The legacy exporter is the only torch.onnx.export() path in 2.3.1, so
        # there is no `dynamo=` kwarg to set.
        with sdpa_kernel(SDPBackend.MATH):
            torch.onnx.export(model, dummy_x, out_path, **export_kwargs)
    print(f"[Success] Exported ONNX {'static' if static else 'FP32'} {resolution}px model to {out_path}")


def main():
    parser = argparse.ArgumentParser(description="Export CorridorKey PyTorch model to ONNX.")
    parser.add_argument("--ckpt", type=str, required=True, help="Path to CorridorKey_v1.0.pth")
    parser.add_argument("--out", type=str, required=True, help="Base output ONNX file path")
    parser.add_argument("--resolutions", type=int, nargs="+", default=[512, 768, 1024, 1536, 2048],
                        help="Resolutions to export (default: 512 768 1024 1536 2048)")
    parser.add_argument("--static", action="store_true",
                        help="Export every resolution with fully static shapes "
                             "(batch_size=1 baked in). Legacy global flag; output filenames "
                             "carry a _static_<res> suffix.")
    parser.add_argument("--static-resolutions", type=int, nargs="+", default=[],
                        help="Resolutions in this list are exported with static shapes; "
                             "all others stay dynamic. Output filenames keep the canonical "
                             "<base>_<res>.onnx form, which is what optimize_model.py and "
                             "the windows-rtx pipeline expect. Use this to enable TensorRT "
                             "at 1536+ resolutions without breaking downstream naming.")
    parser.add_argument("--repo-path", type=str,
                        help="Path to local CorridorKey repo (cloned automatically if omitted). "
                             "When omitted, the upstream is cloned and pinned to "
                             f"{NIKO_UPSTREAM_PIN[:12]} -- the SHA empirically validated against "
                             "the green and blue checkpoints currently shipped.")
    parser.add_argument("--allow-key-drift", action="store_true",
                        help="Continue export even when the checkpoint's state_dict has missing or "
                             "unexpected keys against the upstream GreenFormer. Default behavior is "
                             "to fail hard so we never silently ship an ONNX with untrained layers.")
    args = parser.parse_args()

    repo_to_clean = None
    if args.repo_path:
        repo_path = os.path.abspath(args.repo_path)
    else:
        repo_path = clone_original_repo()
        repo_to_clean = repo_path

    if not os.path.exists(repo_path) or not os.path.exists(os.path.join(repo_path, "CorridorKeyModule")):
        print(f"[Error] Valid CorridorKey repository not found at '{repo_path}'.", file=sys.stderr)
        if repo_to_clean: shutil.rmtree(repo_to_clean, ignore_errors=True)
        sys.exit(1)

    sys.path.insert(0, repo_path)

    try:
        for res in args.resolutions:
            export_resolution(args, res)
    finally:
        if repo_to_clean:
            print("[Info] Cleaning up temporary repository...")
            shutil.rmtree(repo_to_clean, ignore_errors=True)


if __name__ == "__main__":
    main()
