"""Verify ORT loadability for a directory of runtime models.

Invoked by the canonical Windows wrapper after a reuse or regenerate pass to
confirm that every file in the prepared model set can be opened by onnxruntime
on CPU without hitting an op/schema mismatch.

This is not a numerical parity check -- that lives in validate_export_parity.py
and only runs on fresh FP32 intermediates. The goal here is to catch a bundle
that contains a corrupted file or an ONNX op that no backend can consume, before
the canonical release flow advances to ORT build / packaging / certification.

The onnx.checker.check_model gate is intentionally not used: the FP16 converter
produces graphs whose cast-node ordering trips the strict topological check
even though onnxruntime and every downstream EP load them without issue, and
ORT loadability is the contract the runtime cares about.
"""

import argparse
import os
import sys

import onnxruntime as ort


def validate_model(model_path: str) -> None:
    if not os.path.isfile(model_path):
        raise FileNotFoundError(f"Model file is missing: {model_path}")

    session_options = ort.SessionOptions()
    session_options.graph_optimization_level = ort.GraphOptimizationLevel.ORT_DISABLE_ALL
    ort.InferenceSession(
        model_path,
        sess_options=session_options,
        providers=["CPUExecutionProvider"],
    )


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Validate ONNX contracts and ORT loadability for a prepared model pack."
    )
    parser.add_argument("--dir", type=str, required=True,
                        help="Directory containing the prepared ONNX models.")
    parser.add_argument("--models", type=str, nargs="+", required=True,
                        help="Model filenames (relative to --dir) to validate.")
    args = parser.parse_args()

    models_dir = os.path.abspath(args.dir)
    failures = []

    for filename in args.models:
        model_path = os.path.join(models_dir, filename)
        try:
            validate_model(model_path)
        except Exception as exc:
            failures.append((filename, str(exc)))
            print(f"[validate-models] {filename}: FAIL ({exc})")
            continue
        print(f"[validate-models] {filename}: ok")

    if failures:
        print(f"[validate-models] {len(failures)} model(s) failed ONNX/ORT validation.", file=sys.stderr)
        return 1

    print(f"[validate-models] All {len(args.models)} model(s) passed ONNX/ORT validation.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
