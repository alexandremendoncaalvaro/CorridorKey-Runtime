import argparse
import json
from pathlib import Path


def load_profiles(path):
    with open(path, "r", encoding="utf-8") as handle:
        data = json.load(handle)

    if data.get("pipeline") != "windows_rtx_mmdeploy_style":
        raise ValueError("Unsupported deploy profile pipeline.")

    profiles = data.get("profiles", [])
    if not profiles:
        raise ValueError("No deploy profiles were defined.")

    return profiles


def simplify_model(input_model_path):
    import onnx
    from onnxsim import simplify

    model = onnx.load(input_model_path)
    simplified, check = simplify(model)
    if not check:
        return model
    return simplified


def prepare_runtime_model(model, fp16_mode):
    if not fp16_mode:
        return model

    from onnxruntime.transformers.float16 import convert_float_to_float16

    return convert_float_to_float16(
        model,
        keep_io_types=True,
        disable_shape_infer=False,
        force_fp16_initializers=True,
    )


def prepare_profile_artifacts(profile, raw_path, output_dir):
    import onnx

    raw_name = profile["artifacts"]["raw_onnx"]
    runtime_name = profile["artifacts"]["runtime_onnx"]
    backend_config = profile["backend_config"]
    common_config = backend_config.get("common_config", {})
    fp16_mode = bool(common_config.get("fp16_mode", False))

    simplified = simplify_model(str(raw_path))
    runtime_model = prepare_runtime_model(simplified, fp16_mode)

    raw_copy_path = output_dir / raw_name
    runtime_path = output_dir / runtime_name
    onnx.save(simplified, raw_copy_path)
    onnx.save(runtime_model, runtime_path)

    return {
        "name": profile["name"],
        "resolution": profile["resolution"],
        "raw_onnx": str(raw_copy_path),
        "runtime_onnx": str(runtime_path),
        "backend_type": backend_config["type"],
        "shape_policy": profile["onnx_config"]["shape_policy"],
        "dtype_policy": profile["onnx_config"]["dtype_policy"],
        "fp16_mode": fp16_mode,
    }


def main():
    parser = argparse.ArgumentParser(
        description="Prepare Windows RTX backend artifacts from raw ONNX exports."
    )
    parser.add_argument("--profiles", type=str, required=True, help="Deploy profile JSON path")
    parser.add_argument("--raw-dir", type=str, required=True, help="Directory with raw ONNX exports")
    parser.add_argument(
        "--output-dir",
        type=str,
        required=True,
        help="Directory where prepared backend artifacts will be written",
    )
    args = parser.parse_args()

    profiles = load_profiles(args.profiles)
    raw_dir = Path(args.raw_dir).resolve()
    output_dir = Path(args.output_dir).resolve()
    output_dir.mkdir(parents=True, exist_ok=True)

    prepared = []
    for profile in profiles:
        raw_name = profile["artifacts"]["raw_onnx"]
        raw_path = raw_dir / raw_name
        if not raw_path.exists():
            raise FileNotFoundError(f"Raw ONNX export not found: {raw_path}")

        prepared_profile = prepare_profile_artifacts(profile, raw_path, output_dir)
        prepared.append(prepared_profile)
        print(f"[prepare] {profile['name']} -> {prepared_profile['runtime_onnx']}")

    print(json.dumps({"prepared_profiles": prepared}, indent=2))


if __name__ == "__main__":
    main()
