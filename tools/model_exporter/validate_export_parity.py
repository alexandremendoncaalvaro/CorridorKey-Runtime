import argparse
import os
import re
import sys

import numpy as np

from export_onnx import clone_original_repo, disable_torch_compile_for_export, load_model_with_pos_embed_interpolation

MODEL_GROUP_PATTERN = re.compile(r"^corridorkey_(?P<group>[^_]+)_(?P<resolution>\d+)\.onnx$")


def parse_resolution_from_filename(filename):
    stem = os.path.splitext(os.path.basename(filename))[0]
    token = stem.rsplit("_", maxsplit=1)[-1]
    if not token.isdigit():
        raise ValueError(f"Unable to resolve model resolution from filename: {filename}")
    return int(token)


def group_name_for_model(filename):
    match = MODEL_GROUP_PATTERN.match(os.path.basename(filename))
    if match is None:
        raise ValueError(f"Unable to resolve model family from filename: {filename}")
    return match.group("group")


def tolerances_for_model(filename, args):
    group_name = group_name_for_model(filename)
    if group_name == "fp16":
        return args.fp16_atol, args.fp16_rtol
    if group_name == "int8":
        return args.int8_atol, args.int8_rtol
    return args.fp32_atol, args.fp32_rtol


def build_test_input(resolution, seed):
    rng = np.random.default_rng(seed + resolution)
    rgb = rng.random((1, 3, resolution, resolution), dtype=np.float32)
    hint = rng.random((1, 1, resolution, resolution), dtype=np.float32)
    return np.concatenate([rgb, hint], axis=1)


def compare_tensor(name, torch_value, ort_value, atol, rtol):
    torch_array = torch_value.detach().cpu().float().numpy()
    ort_array = np.asarray(ort_value, dtype=np.float32)

    if torch_array.shape != ort_array.shape:
        return False, f"{name} shape mismatch: torch={torch_array.shape} ort={ort_array.shape}"

    diff = np.abs(torch_array - ort_array)
    max_abs = float(diff.max(initial=0.0))
    mean_abs = float(diff.mean()) if diff.size else 0.0
    if not np.allclose(torch_array, ort_array, atol=atol, rtol=rtol):
        return False, (
            f"{name} parity mismatch: max_abs={max_abs:.6f} mean_abs={mean_abs:.6f} "
            f"atol={atol} rtol={rtol}"
        )

    return True, f"{name} parity ok: max_abs={max_abs:.6f} mean_abs={mean_abs:.6f}"


def validate_model(model_path, checkpoint_path, repo_path, seed, atol, rtol):
    import onnxruntime as ort
    import torch

    disable_torch_compile_for_export(torch)

    resolution = parse_resolution_from_filename(model_path)
    input_np = build_test_input(resolution, seed)

    model = load_model_with_pos_embed_interpolation(checkpoint_path, resolution)
    model.eval()

    class ONNXWrapper(torch.nn.Module):
        def __init__(self, wrapped_model):
            super().__init__()
            self.model = wrapped_model

        def forward(self, x):
            out = self.model(x)
            return out["alpha"], out.get("fg", x[:, :3, :, :])

    wrapped = ONNXWrapper(model)
    input_t = torch.from_numpy(input_np)

    with torch.inference_mode():
        torch_alpha, torch_fg = wrapped(input_t)

    session_options = ort.SessionOptions()
    session_options.graph_optimization_level = ort.GraphOptimizationLevel.ORT_DISABLE_ALL
    session = ort.InferenceSession(
        model_path,
        sess_options=session_options,
        providers=["CPUExecutionProvider"],
    )
    ort_alpha, ort_fg = session.run(None, {"input_rgb_hint": input_np})

    alpha_ok, alpha_detail = compare_tensor("alpha", torch_alpha, ort_alpha, atol, rtol)
    fg_ok, fg_detail = compare_tensor("fg", torch_fg, ort_fg, atol, rtol)
    return alpha_ok and fg_ok, [alpha_detail, fg_detail]


def main():
    parser = argparse.ArgumentParser(
        description="Validate exported CorridorKey ONNX models against the canonical PyTorch implementation."
    )
    parser.add_argument("--ckpt", type=str, required=True, help="Path to the CorridorKey checkpoint")
    parser.add_argument("--dir", type=str, required=True, help="Directory containing exported FP32 ONNX models")
    parser.add_argument("--models", type=str, nargs="+", required=True, help="Model filenames to validate")
    parser.add_argument("--repo-path", type=str, help="Path to the CorridorKey source repository")
    parser.add_argument("--seed", type=int, default=1234, help="Deterministic seed for parity inputs")
    parser.add_argument("--fp32-atol", type=float, default=1e-3,
                        help="Absolute tolerance for FP32 parity checks")
    parser.add_argument("--fp32-rtol", type=float, default=1e-3,
                        help="Relative tolerance for FP32 parity checks")
    parser.add_argument("--fp16-atol", type=float, default=2e-3,
                        help="Absolute tolerance for FP16 parity checks")
    parser.add_argument("--fp16-rtol", type=float, default=2e-3,
                        help="Relative tolerance for FP16 parity checks")
    parser.add_argument("--int8-atol", type=float, default=5e-2,
                        help="Absolute tolerance for INT8 parity checks")
    parser.add_argument("--int8-rtol", type=float, default=5e-2,
                        help="Relative tolerance for INT8 parity checks")
    args = parser.parse_args()

    repo_to_clean = None
    if args.repo_path:
        repo_path = os.path.abspath(args.repo_path)
    else:
        repo_path = clone_original_repo()
        repo_to_clean = repo_path

    if not os.path.exists(repo_path) or not os.path.exists(os.path.join(repo_path, "CorridorKeyModule")):
        print(f"[Error] Valid CorridorKey repository not found at '{repo_path}'.", file=sys.stderr)
        if repo_to_clean:
            import shutil
            shutil.rmtree(repo_to_clean, ignore_errors=True)
        return 1

    sys.path.insert(0, repo_path)

    healthy = True
    try:
        models_dir = os.path.abspath(args.dir)
        for filename in args.models:
            model_path = os.path.join(models_dir, filename)
            if not os.path.exists(model_path):
                print(f"[parity] {filename}: missing")
                healthy = False
                continue

            try:
                atol, rtol = tolerances_for_model(filename, args)
                model_ok, details = validate_model(
                    model_path, args.ckpt, repo_path, args.seed, atol, rtol
                )
            except Exception as exception:
                print(f"[parity] {filename}: failed ({exception})")
                healthy = False
                continue

            print(f"[parity] {filename}: tolerances atol={atol} rtol={rtol}")
            for detail in details:
                print(f"[parity] {filename}: {detail}")
            healthy = healthy and model_ok
    finally:
        if repo_to_clean:
            import shutil
            shutil.rmtree(repo_to_clean, ignore_errors=True)

    return 0 if healthy else 1


if __name__ == "__main__":
    sys.exit(main())
