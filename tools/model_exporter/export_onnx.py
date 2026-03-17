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


def clone_original_repo():
    print("[Info] Cloning original CorridorKey repository for export dependencies...")
    temp_dir = tempfile.mkdtemp(prefix="corridorkey_repo_")
    try:
        subprocess.run(
            ["git", "clone", "--depth", "1", "https://github.com/nikopueringer/CorridorKey.git", temp_dir],
            check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL
        )
        return temp_dir
    except subprocess.CalledProcessError as e:
        print(f"[Error] Failed to clone original repository: {e}")
        shutil.rmtree(temp_dir, ignore_errors=True)
        sys.exit(1)


def load_model_with_pos_embed_interpolation(checkpoint_path, img_size):
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

    checkpoint = torch.load(checkpoint_path, map_location="cpu", weights_only=True)
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
    if missing:
        print(f"  [Warning] Missing keys: {missing}")
    if unexpected:
        print(f"  [Warning] Unexpected keys: {unexpected}")

    return model


def export_resolution(args, resolution):
    import torch
    disable_torch_compile_for_export(torch)

    out_path = args.out.replace('.onnx', f'_{resolution}.onnx')
    print(f"\n[Info] === Exporting Resolution {resolution}x{resolution} ===")

    base_model = load_model_with_pos_embed_interpolation(args.ckpt, resolution)

    class ONNXWrapper(torch.nn.Module):
        def __init__(self, model):
            super().__init__()
            self.model = model

        def forward(self, x):
            out = self.model(x)
            return out["alpha"], out.get("fg", x[:, :3, :, :])

    model = ONNXWrapper(base_model)
    dummy_x = torch.randn(1, 4, resolution, resolution, device="cpu")

    print(f"[Info] Exporting ONNX model to {out_path}...")
    os.makedirs(os.path.dirname(os.path.abspath(out_path)), exist_ok=True)

    with warnings.catch_warnings():
        warnings.filterwarnings("ignore", category=UserWarning)
        warnings.filterwarnings("ignore", category=torch.jit.TracerWarning)
        from torch.nn.attention import SDPBackend, sdpa_kernel

        with sdpa_kernel(SDPBackend.MATH):
            torch.onnx.export(
                model,
                dummy_x,
                out_path,
                export_params=True,
                opset_version=16,
                do_constant_folding=True,
                input_names=['input_rgb_hint'],
                output_names=['alpha', 'fg'],
                dynamic_axes={
                    'input_rgb_hint': {0: 'batch_size'},
                    'alpha': {0: 'batch_size'},
                    'fg': {0: 'batch_size'}
                }
            )
    print(f"[Success] Exported ONNX FP32 {resolution}px model to {out_path}")


def main():
    parser = argparse.ArgumentParser(description="Export CorridorKey PyTorch model to ONNX.")
    parser.add_argument("--ckpt", type=str, required=True, help="Path to CorridorKey_v1.0.pth")
    parser.add_argument("--out", type=str, required=True, help="Base output ONNX file path")
    parser.add_argument("--resolutions", type=int, nargs="+", default=[512, 768, 1024, 1536, 2048],
                        help="Resolutions to export (default: 512 768 1024 1536 2048)")
    parser.add_argument("--repo-path", type=str,
                        help="Path to local CorridorKey repo (cloned automatically if omitted)")
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
