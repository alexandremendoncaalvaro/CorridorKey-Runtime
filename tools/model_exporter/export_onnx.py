import os
import sys
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

def export_resolution(args, resolution, repo_path):
    import torch
    disable_torch_compile_for_export(torch)
    from CorridorKeyModule.inference_engine import CorridorKeyEngine

    out_path = args.out.replace('.onnx', f'_{resolution}.onnx')
    print(f"\n[Info] === Exporting Resolution {resolution}x{resolution} ===")

    engine = CorridorKeyEngine(checkpoint_path=args.ckpt, device="cpu", img_size=resolution, use_refiner=True)
    base_model = engine.model
    base_model.eval()

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
    parser.add_argument("--repo-path", type=str, help="Optional: Path to local CorridorKey repo. If omitted, it will be cloned automatically.")
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
        resolutions = [512, 768, 1024]
        for res in resolutions:
            export_resolution(args, res, repo_path)
    finally:
        if repo_to_clean:
            print("[Info] Cleaning up temporary repository...")
            # Windows file locking can sometimes block immediate deletion, using ignore_errors
            shutil.rmtree(repo_to_clean, ignore_errors=True)

if __name__ == "__main__":
    main()
