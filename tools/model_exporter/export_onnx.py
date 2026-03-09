import os
import sys
import argparse
import warnings

def export_resolution(args, resolution, repo_path):
    import torch
    from CorridorKeyModule.inference_engine import CorridorKeyEngine

    out_path = args.out.replace('.onnx', f'_{resolution}.onnx')
    print(f"\n[Info] === Exporting Resolution {resolution}x{resolution} ===")
    
    # 2. Build and load the model using the original Engine
    # We lock the spatial dimensions because Hiera positional embeddings are static
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

    # 3. Prepare dummy inputs
    # Batch size can remain dynamic, but spatial resolution is locked
    dummy_x = torch.randn(1, 4, resolution, resolution, device="cpu")
    
    # 4. Export to ONNX
    print(f"[Info] Exporting ONNX model to {out_path}...")
    os.makedirs(os.path.dirname(os.path.abspath(out_path)), exist_ok=True)
    
    with warnings.catch_warnings():
        warnings.filterwarnings("ignore", category=UserWarning)
        warnings.filterwarnings("ignore", category=torch.jit.TracerWarning)
        
        # Fallback to math kernel to decompose SDPA into standard ops
        from torch.nn.attention import SDPBackend, sdpa_kernel
        
        with sdpa_kernel(SDPBackend.MATH):
            torch.onnx.export(
                model,
                dummy_x,
                out_path,
                export_params=True,
                opset_version=17,
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
    parser.add_argument("--out", type=str, required=True, help="Base output ONNX file path (e.g. model.onnx)")
    parser.add_argument("--repo-path", type=str, default="../../../CorridorKey", help="Path to original repo")
    args = parser.parse_args()

    repo_path = os.path.abspath(args.repo_path)
    if not os.path.exists(repo_path) or not os.path.exists(os.path.join(repo_path, "CorridorKeyModule")):
        print(f"[Error] Original CorridorKey repository not found at '{repo_path}'.", file=sys.stderr)
        sys.exit(1)
        
    sys.path.insert(0, repo_path)

    resolutions = [512, 768, 1024]
    for res in resolutions:
        export_resolution(args, res, repo_path)

if __name__ == "__main__":
    main()
