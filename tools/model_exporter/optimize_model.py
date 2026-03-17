import os
import argparse

def simplify_model(input_model_path: str):
    import onnx
    from onnxsim import simplify

    print("  -> Loading original ONNX...")
    model = onnx.load(input_model_path)

    print("  -> Running ONNX Simplifier...")
    model_simp, check = simplify(model)
    if not check:
        print("  [Warning] Simplified model check failed. Reusing the original graph.")
        return model

    return model_simp

def simplify_and_optimize_portable_ort(input_model_path: str, output_model_path: str,
                                       fp16: bool = False):
    print(f"\n[Info] Optimizing {input_model_path} -> {output_model_path}")

    # The ORT transformer optimizer is useful for the generic runtime path, but it
    # introduces contrib ops that TensorRT RTX cannot compile.
    import onnx

    model_simp = simplify_model(input_model_path)

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

def simplify_and_convert_windows_rtx(input_model_path: str, fp32_output_path: str,
                                     fp16_output_path: str):
    print(f"\n[Info] Preparing TensorRT RTX-compatible models from {input_model_path}")

    import onnx
    from onnxruntime.transformers.float16 import convert_float_to_float16

    model_simp = simplify_model(input_model_path)

    print(f"  -> Saving simplified FP32 model to {fp32_output_path}")
    onnx.save(model_simp, fp32_output_path)

    print("  -> Converting graph weights and internal tensors to FP16 while preserving FP32 I/O...")
    fp16_model = convert_float_to_float16(
        model_simp,
        keep_io_types=True,
        disable_shape_infer=False,
        force_fp16_initializers=True
    )
    onnx.save(fp16_model, fp16_output_path)
    print(f"  [Success] Saved TensorRT RTX-compatible FP16 model to {fp16_output_path}")

def main():
    parser = argparse.ArgumentParser(description="Optimize and Quantize CorridorKey ONNX models.")
    parser.add_argument("--dir", type=str, default="../../models", help="Directory containing raw exported ONNX models")
    parser.add_argument(
        "--target",
        type=str,
        choices=("portable-ort", "windows-rtx"),
        default="portable-ort",
        help="Select the optimization profile for the generated artifacts."
    )
    args = parser.parse_args()

    models_dir = os.path.abspath(args.dir)

    resolutions = [512, 768, 1024, 1536, 2048]

    for res in resolutions:
        raw_path = os.path.join(models_dir, f"corridorkey_fp32_{res}.onnx")
        if not os.path.exists(raw_path):
            print(f"[Skip] {raw_path} not found.")
            continue

        # Target names
        fp32_opt_path = os.path.join(models_dir, f"corridorkey_fp32_{res}_opt.onnx")
        fp16_opt_path = os.path.join(models_dir, f"corridorkey_fp16_{res}.onnx")

        if args.target == "windows-rtx":
            simplify_and_convert_windows_rtx(raw_path, raw_path, fp16_opt_path)
            continue

        # Create optimized FP32
        simplify_and_optimize_portable_ort(raw_path, fp32_opt_path, fp16=False)

        # Create optimized FP16
        simplify_and_optimize_portable_ort(raw_path, fp16_opt_path, fp16=True)

        # Overwrite raw FP32 with optimized FP32 for distribution
        os.replace(fp32_opt_path, raw_path)

if __name__ == "__main__":
    main()
