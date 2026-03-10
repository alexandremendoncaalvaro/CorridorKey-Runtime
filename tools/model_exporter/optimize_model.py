import os
import sys
import argparse
import numpy as np

def simplify_and_optimize(input_model_path: str, output_model_path: str, fp16: bool = False):
    print(f"\n[Info] Optimizing {input_model_path} -> {output_model_path}")

    # 1. Run ONNX Simplifier
    import onnx
    from onnxsim import simplify

    print("  -> Loading original ONNX...")
    model = onnx.load(input_model_path)

    print("  -> Running ONNX Simplifier...")
    model_simp, check = simplify(model)
    if not check:
        print("  [Warning] Simplified model check failed. Skipping simplification.")
        model_simp = model

    # 2. Run ONNX Runtime Optimizer (Transformer specific)
    # We must save the simplified model first
    temp_simp_path = output_model_path + ".temp.onnx"
    onnx.save(model_simp, temp_simp_path)

    print("  -> Running ONNXRuntime Transformer Optimizer...")
    from onnxruntime.transformers.optimizer import optimize_model

    try:
        # GreenFormer uses a Hiera Vision Transformer, which falls under 'vit'
        opt_model = optimize_model(
            temp_simp_path,
            model_type='vit',
            opt_level=1,
            use_gpu=False,
            only_onnxruntime=False
        )

        if fp16:
            print("  -> Converting weights to FP16...")
            opt_model.convert_float_to_float16()

        opt_model.save_model_to_file(output_model_path)
        print(f"  [Success] Saved optimized model to {output_model_path}")

    except Exception as e:
        print(f"  [Warning] ORT Optimizer failed: {e}")
        print("  [Info] Falling back to simplified model.")
        import shutil
        shutil.copy2(temp_simp_path, output_model_path)
    finally:
        if os.path.exists(temp_simp_path):
            os.remove(temp_simp_path)


def main():
    parser = argparse.ArgumentParser(description="Optimize and Quantize CorridorKey ONNX models.")
    parser.add_argument("--dir", type=str, default="../../models", help="Directory containing raw exported ONNX models")
    args = parser.parse_args()

    models_dir = os.path.abspath(args.dir)

    resolutions = [512, 768, 1024]

    for res in resolutions:
        raw_path = os.path.join(models_dir, f"corridorkey_fp32_{res}.onnx")
        if not os.path.exists(raw_path):
            print(f"[Skip] {raw_path} not found.")
            continue

        # Target names
        fp32_opt_path = os.path.join(models_dir, f"corridorkey_fp32_{res}_opt.onnx")
        fp16_opt_path = os.path.join(models_dir, f"corridorkey_fp16_{res}.onnx")

        # Create optimized FP32
        simplify_and_optimize(raw_path, fp32_opt_path, fp16=False)

        # Create optimized FP16
        simplify_and_optimize(raw_path, fp16_opt_path, fp16=True)

        # Overwrite raw FP32 with optimized FP32 for distribution
        os.replace(fp32_opt_path, raw_path)

if __name__ == "__main__":
    main()
