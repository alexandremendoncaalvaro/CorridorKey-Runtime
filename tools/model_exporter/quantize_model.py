import os
import sys
import argparse
import numpy as np

def quantize_int8(input_model_path: str, output_model_path: str):
    print(f"\n[Info] Quantizing to INT8: {input_model_path} -> {output_model_path}")
    
    from onnxruntime.quantization import quantize_dynamic, QuantType

    try:
        quantize_dynamic(
            model_input=input_model_path,
            model_output=output_model_path,
            weight_type=QuantType.QUInt8,
            per_channel=True,
            reduce_range=True,
            extra_options={'WeightSymmetric': False, 'MatMulConstBOnly': True}
        )
        print(f"  [Success] Saved quantized model to {output_model_path}")
    except Exception as e:
        print(f"  [Error] Quantization failed: {e}")

def main():
    parser = argparse.ArgumentParser(description="Quantize CorridorKey ONNX models to INT8.")
    parser.add_argument("--dir", type=str, default="../../models", help="Directory containing optimized FP32 ONNX models")
    args = parser.parse_args()

    models_dir = os.path.abspath(args.dir)
    resolutions = [512, 768, 1024]

    for res in resolutions:
        fp32_opt_path = os.path.join(models_dir, f"corridorkey_fp32_{res}.onnx")
        if not os.path.exists(fp32_opt_path):
            print(f"[Skip] {fp32_opt_path} not found.")
            continue

        int8_out_path = os.path.join(models_dir, f"corridorkey_int8_{res}.onnx")
        quantize_int8(fp32_opt_path, int8_out_path)

if __name__ == "__main__":
    main()
