import argparse
import itertools
import json
import os
import subprocess
import tempfile
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Any


REQUIRED_CORPUS_CATEGORIES = (
    "hair",
    "fine_edge",
    "motion_blur",
    "transparency",
    "spill",
)
GPU_INT8_SPEEDUP_THRESHOLD = 1.8


@dataclass(frozen=True)
class VisualCorpusCase:
    case_id: str
    category: str
    input_path: Path
    alpha_hint_path: Path | None
    notes: str


def load_json(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as handle:
        return json.load(handle)


def normalize_case_id(raw_case: dict[str, Any]) -> str:
    case_id = str(raw_case.get("id", "")).strip()
    if not case_id:
        raise ValueError("Every visual corpus case must define a non-empty 'id'.")
    return case_id


def resolve_optional_path(base_dir: Path, raw_value: Any) -> Path | None:
    if raw_value is None:
        return None
    text = str(raw_value).strip()
    if not text:
        return None
    path = Path(text)
    if not path.is_absolute():
        path = (base_dir / path).resolve()
    return path


def validate_visual_corpus_manifest(manifest: dict[str, Any], manifest_path: Path) -> list[VisualCorpusCase]:
    if manifest.get("version") != 1:
        raise ValueError("The INT8 visual corpus manifest must declare version 1.")

    raw_cases = manifest.get("cases")
    if not isinstance(raw_cases, list) or not raw_cases:
        raise ValueError("The INT8 visual corpus manifest must contain a non-empty 'cases' list.")

    base_dir = manifest_path.parent.resolve()
    resolved_cases: list[VisualCorpusCase] = []
    seen_ids: set[str] = set()
    seen_categories: set[str] = set()

    for raw_case in raw_cases:
        if not isinstance(raw_case, dict):
            raise ValueError("Every corpus case entry must be an object.")

        case_id = normalize_case_id(raw_case)
        if case_id in seen_ids:
            raise ValueError(f"Duplicate corpus case id: {case_id}")
        seen_ids.add(case_id)

        category = str(raw_case.get("category", "")).strip()
        if category not in REQUIRED_CORPUS_CATEGORIES:
            raise ValueError(
                f"Unsupported corpus category '{category}' for case '{case_id}'."
            )
        seen_categories.add(category)

        input_path = resolve_optional_path(base_dir, raw_case.get("input"))
        if input_path is None or not input_path.exists():
            raise ValueError(f"Input image is missing for corpus case '{case_id}'.")

        alpha_hint_path = resolve_optional_path(base_dir, raw_case.get("alpha_hint"))
        if alpha_hint_path is not None and not alpha_hint_path.exists():
            raise ValueError(f"Alpha hint is missing for corpus case '{case_id}'.")

        resolved_cases.append(
            VisualCorpusCase(
                case_id=case_id,
                category=category,
                input_path=input_path,
                alpha_hint_path=alpha_hint_path,
                notes=str(raw_case.get("notes", "")).strip(),
            )
        )

    missing_categories = sorted(set(REQUIRED_CORPUS_CATEGORIES) - seen_categories)
    if missing_categories:
        raise ValueError(
            "The INT8 visual corpus manifest is incomplete. Missing categories: "
            + ", ".join(missing_categories)
        )

    return resolved_cases


def load_visual_corpus_cases(manifest_path: Path) -> list[VisualCorpusCase]:
    return validate_visual_corpus_manifest(load_json(manifest_path), manifest_path)


def benchmark_resolution_args(resolution: int) -> list[str]:
    return ["--resolution", str(resolution)]


def run_cli_benchmark(
    cli_path: Path,
    models_dir: Path,
    backend: str,
    precision: str,
    resolution: int,
    corpus_case: VisualCorpusCase | None,
) -> dict[str, Any]:
    command = [
        str(cli_path),
        "benchmark",
        "--device",
        backend,
        "--precision",
        precision,
        "--json",
        *benchmark_resolution_args(resolution),
    ]
    environment = os.environ.copy()
    environment["CORRIDORKEY_MODELS_DIR"] = str(models_dir)

    temp_output_path: Path | None = None
    if corpus_case is not None:
        temp_output_dir = Path(tempfile.mkdtemp(prefix="corridorkey-int8-review-"))
        temp_output_path = temp_output_dir / f"{corpus_case.case_id}_{backend}_{precision}"
        command.extend(["--input", str(corpus_case.input_path), "--output", str(temp_output_path)])
        if corpus_case.alpha_hint_path is not None:
            command.extend(["--alpha-hint", str(corpus_case.alpha_hint_path)])

    completed = subprocess.run(
        command,
        capture_output=True,
        text=True,
        check=False,
        env=environment,
    )
    if completed.returncode != 0:
        raise RuntimeError(
            f"corridorkey benchmark failed for {backend}/{precision}/{resolution}: "
            f"{completed.stderr or completed.stdout}"
        )

    report = json.loads(completed.stdout)
    if temp_output_path is not None:
        report["temporary_output_path"] = str(temp_output_path)
    return report


def summarize_cli_report(report: dict[str, Any], backend: str, precision: str, resolution: int) -> dict[str, Any]:
    return {
        "engine": "corridorkey-cli",
        "backend": backend,
        "requested_precision": precision,
        "requested_resolution": resolution,
        "artifact": report.get("artifact", ""),
        "effective_precision": report.get("effective_precision", ""),
        "effective_resolution": report.get("effective_resolution", 0),
        "cold_latency_ms": report.get("cold_latency_ms", 0.0),
        "steady_state_avg_latency_ms": report.get("avg_latency_ms", 0.0),
        "fps": report.get("fps", 0.0),
        "quality_fallback_used": report.get("quality_fallback_used", False),
        "manual_override_above_safe_ceiling": report.get(
            "manual_override_above_safe_ceiling", False
        ),
        "safe_quality_ceiling": report.get("safe_quality_ceiling", 0),
        "latency_ms": report.get("latency_ms", {}),
        "raw_report": report,
    }


def import_torch_modules() -> tuple[Any, Any]:
    import torch

    try:
        import torch_tensorrt  # type: ignore
    except ImportError as exc:
        raise RuntimeError(
            "Torch-TensorRT is not installed in tools/model_exporter. "
            "Install it in this environment before running the GPU INT8 spike."
        ) from exc

    return torch, torch_tensorrt


def load_case_tensor(case: VisualCorpusCase, resolution: int) -> tuple[Any, Any]:
    import cv2
    import numpy as np
    import torch

    image = cv2.imread(str(case.input_path), cv2.IMREAD_COLOR)
    if image is None:
        raise RuntimeError(f"Could not load corpus image: {case.input_path}")
    image = cv2.cvtColor(image, cv2.COLOR_BGR2RGB)
    image = cv2.resize(image, (resolution, resolution), interpolation=cv2.INTER_AREA)

    hint = None
    if case.alpha_hint_path is not None:
        hint = cv2.imread(str(case.alpha_hint_path), cv2.IMREAD_GRAYSCALE)
        if hint is None:
            raise RuntimeError(f"Could not load alpha hint: {case.alpha_hint_path}")
        hint = cv2.resize(hint, (resolution, resolution), interpolation=cv2.INTER_AREA)
    else:
        hint = np.zeros((resolution, resolution), dtype=np.uint8)

    rgb = image.astype("float32") / 255.0
    alpha_hint = hint.astype("float32") / 255.0
    stacked = np.concatenate([rgb, alpha_hint[..., None]], axis=2)
    tensor = torch.from_numpy(stacked.transpose(2, 0, 1)).unsqueeze(0).contiguous()
    return tensor, stacked


def build_calibration_batches(cases: list[VisualCorpusCase], resolution: int, limit: int) -> list[Any]:
    import torch

    batches: list[Any] = []
    for case in cases[:limit]:
        tensor, _ = load_case_tensor(case, resolution)
        batches.append(tensor.to(dtype=torch.float32))
    if not batches:
        raise ValueError("At least one visual corpus case is required for calibration.")
    return batches


def load_corridorkey_model(checkpoint_path: Path, repo_path: Path, resolution: int, device_name: str) -> Any:
    import sys

    sys.path.insert(0, str(repo_path))
    from export_windows_rtx_onnx import load_model_with_pos_embed_interpolation

    return load_model_with_pos_embed_interpolation(str(checkpoint_path), resolution, device_name)


def build_corridorkey_torch_wrapper(torch_module: Any, model: Any) -> Any:
    class CorridorKeyTorchWrapper(torch_module.nn.Module):
        def __init__(self, wrapped_model: Any):
            super().__init__()
            self.model = wrapped_model

        def forward(self, tensor: Any) -> tuple[Any, Any]:
            output = self.model(tensor)
            return output["alpha"], output.get("fg", tensor[:, :3, :, :])

    return CorridorKeyTorchWrapper(model)


def benchmark_torch_callable(
    module: Any,
    input_batches: list[Any],
    warmup_runs: int,
    benchmark_runs: int,
    synchronize: Any,
) -> dict[str, Any]:
    import torch

    module.eval()
    warmup_latencies: list[float] = []
    benchmark_latencies: list[float] = []

    with torch.inference_mode():
        start = time.perf_counter()
        alpha, fg = module(input_batches[0])
        synchronize()
        cold_latency_ms = (time.perf_counter() - start) * 1000.0

        iterator = itertools.cycle(input_batches)
        for _ in range(warmup_runs):
            warmup_input = next(iterator)
            start = time.perf_counter()
            module(warmup_input)
            synchronize()
            warmup_latencies.append((time.perf_counter() - start) * 1000.0)

        total_start = time.perf_counter()
        for _ in range(benchmark_runs):
            benchmark_input = next(iterator)
            start = time.perf_counter()
            module(benchmark_input)
            synchronize()
            benchmark_latencies.append((time.perf_counter() - start) * 1000.0)
        total_elapsed_ms = (time.perf_counter() - total_start) * 1000.0

    steady_state_avg = sum(benchmark_latencies) / len(benchmark_latencies)
    return {
        "cold_latency_ms": cold_latency_ms,
        "warmup_runs": warmup_runs,
        "benchmark_runs": benchmark_runs,
        "steady_state_avg_latency_ms": steady_state_avg,
        "fps": (1000.0 * benchmark_runs / total_elapsed_ms) if total_elapsed_ms > 0.0 else 0.0,
        "latency_ms": {
            "warmup": warmup_latencies,
            "steady_state": benchmark_latencies,
        },
        "last_alpha_shape": list(alpha.shape),
        "last_fg_shape": list(fg.shape),
    }


def build_ptq_calibrator(torch_tensorrt_module: Any, calibration_batches: list[Any], cache_path: Path | None) -> Any:
    ptq = getattr(torch_tensorrt_module, "ptq", None)
    if ptq is None or not hasattr(ptq, "DataLoaderCalibrator"):
        raise RuntimeError(
            "This Torch-TensorRT build does not expose ptq.DataLoaderCalibrator."
        )

    class CalibrationLoader:
        def __iter__(self_inner):
            for batch in calibration_batches:
                yield [batch]

        def __len__(self_inner):
            return len(calibration_batches)

    calibration_algo = None
    if hasattr(ptq, "CalibrationAlgo"):
        calibration_algo = getattr(
            ptq.CalibrationAlgo,
            "ENTROPY_CALIBRATION_2",
            getattr(ptq.CalibrationAlgo, "ENTROPY_CALIBRATION", None),
        )

    kwargs = {"data_loader": CalibrationLoader()}
    if cache_path is not None:
        kwargs["cache_file"] = str(cache_path)
        kwargs["use_cache"] = cache_path.exists()
    if calibration_algo is not None:
        kwargs["algo_type"] = calibration_algo

    try:
        return ptq.DataLoaderCalibrator(**kwargs)
    except TypeError:
        kwargs.pop("data_loader", None)
        kwargs["dataloader"] = CalibrationLoader()
        return ptq.DataLoaderCalibrator(**kwargs)


def compile_torch_tensorrt_candidate(
    model: Any,
    torch_module: Any,
    torch_tensorrt_module: Any,
    resolution: int,
    precision: str,
    calibration_batches: list[Any] | None,
    calibration_cache_path: Path | None,
) -> Any:
    input_spec = [
        torch_tensorrt_module.Input(
            min_shape=(1, 4, resolution, resolution),
            opt_shape=(1, 4, resolution, resolution),
            max_shape=(1, 4, resolution, resolution),
            dtype=torch_module.float32,
        )
    ]
    compile_kwargs: dict[str, Any] = {
        "ir": "dynamo",
        "inputs": input_spec,
        "truncate_long_and_double": True,
        "require_full_compilation": False,
    }

    if precision == "fp16":
        compile_kwargs["enabled_precisions"] = {torch_module.float16}
        return torch_tensorrt_module.compile(model, **compile_kwargs)

    compile_kwargs["enabled_precisions"] = {torch_module.int8}
    if calibration_batches:
        try:
            compile_kwargs["calibrator"] = build_ptq_calibrator(
                torch_tensorrt_module, calibration_batches, calibration_cache_path
            )
        except RuntimeError:
            pass

    return torch_tensorrt_module.compile(model, **compile_kwargs)


def compare_outputs(reference_alpha: Any, reference_fg: Any, candidate_alpha: Any, candidate_fg: Any) -> dict[str, float]:
    import torch

    alpha_delta = torch.abs(reference_alpha - candidate_alpha)
    fg_delta = torch.abs(reference_fg - candidate_fg)
    return {
        "alpha_mean_abs_diff": float(alpha_delta.mean().item()),
        "alpha_max_abs_diff": float(alpha_delta.max().item()),
        "fg_mean_abs_diff": float(fg_delta.mean().item()),
        "fg_max_abs_diff": float(fg_delta.max().item()),
    }


def run_torch_tensorrt_candidate_benchmark(
    checkpoint_path: Path,
    repo_path: Path,
    cases: list[VisualCorpusCase],
    resolution: int,
    precision: str,
    warmup_runs: int,
    benchmark_runs: int,
    calibration_case_limit: int,
    calibration_cache_path: Path | None,
) -> dict[str, Any]:
    torch, torch_tensorrt = import_torch_modules()

    if not torch.cuda.is_available():
        raise RuntimeError("CUDA is required for the Torch-TensorRT INT8 spike.")

    base_model = load_corridorkey_model(checkpoint_path, repo_path, resolution, "cuda")
    wrapped_model = build_corridorkey_torch_wrapper(torch, base_model).eval()
    calibration_batches = None
    input_batches = []
    numeric_drift = []

    if cases:
        calibration_batches = build_calibration_batches(cases, resolution, calibration_case_limit)
        for case in cases:
            tensor, _ = load_case_tensor(case, resolution)
            input_batches.append(tensor.to(device="cuda", dtype=torch.float32))
    else:
        input_batches.append(
            torch.randn((1, 4, resolution, resolution), device="cuda", dtype=torch.float32)
        )

    compiled = compile_torch_tensorrt_candidate(
        wrapped_model,
        torch,
        torch_tensorrt,
        resolution,
        precision,
        calibration_batches,
        calibration_cache_path,
    )

    synchronize = torch.cuda.synchronize
    benchmark = benchmark_torch_callable(
        compiled, input_batches, warmup_runs, benchmark_runs, synchronize
    )

    if cases:
        with torch.inference_mode():
            for case, batch in zip(cases, input_batches):
                reference_alpha, reference_fg = wrapped_model(batch)
                candidate_alpha, candidate_fg = compiled(batch)
                drift = compare_outputs(
                    reference_alpha, reference_fg, candidate_alpha, candidate_fg
                )
                drift["case_id"] = case.case_id
                drift["category"] = case.category
                numeric_drift.append(drift)

    return {
        "engine": "torch_tensorrt",
        "precision": precision,
        "resolution": resolution,
        "benchmark": benchmark,
        "numeric_drift": numeric_drift,
        "calibration_case_count": len(calibration_batches or []),
    }


def compute_speedup(baseline_latency_ms: float, candidate_latency_ms: float) -> float:
    if baseline_latency_ms <= 0.0 or candidate_latency_ms <= 0.0:
        return 0.0
    return baseline_latency_ms / candidate_latency_ms


def summarize_numeric_drift(candidate_reports: list[dict[str, Any]]) -> dict[str, float]:
    alpha_mean = []
    alpha_max = []
    fg_mean = []
    fg_max = []
    for report in candidate_reports:
        for drift in report.get("numeric_drift", []):
            alpha_mean.append(drift["alpha_mean_abs_diff"])
            alpha_max.append(drift["alpha_max_abs_diff"])
            fg_mean.append(drift["fg_mean_abs_diff"])
            fg_max.append(drift["fg_max_abs_diff"])

    if not alpha_mean:
        return {}

    return {
        "alpha_mean_abs_diff": sum(alpha_mean) / len(alpha_mean),
        "alpha_max_abs_diff": max(alpha_max),
        "fg_mean_abs_diff": sum(fg_mean) / len(fg_mean),
        "fg_max_abs_diff": max(fg_max),
    }


def build_decision_summary(
    fp16_cli_reports: list[dict[str, Any]],
    cpu_int8_cli_reports: list[dict[str, Any]],
    gpu_int8_reports: list[dict[str, Any]],
    visual_corpus_cases: list[VisualCorpusCase],
) -> dict[str, Any]:
    speedups: dict[str, float] = {}
    reasons: list[str] = []
    gpu_gate_failed = False
    visual_review_pending = False
    if not gpu_int8_reports:
        reasons.append("Torch-TensorRT GPU INT8 candidate results are unavailable.")

    for baseline, candidate in zip(fp16_cli_reports, gpu_int8_reports):
        resolution = str(candidate["resolution"])
        speedup = compute_speedup(
            baseline["steady_state_avg_latency_ms"],
            candidate["benchmark"]["steady_state_avg_latency_ms"],
        )
        speedups[resolution] = speedup
        if speedup < GPU_INT8_SPEEDUP_THRESHOLD:
            gpu_gate_failed = True
            reasons.append(
                f"GPU INT8 at {resolution}px did not reach the {GPU_INT8_SPEEDUP_THRESHOLD:.1f}x "
                f"steady-state speedup gate."
            )

    numeric_drift = summarize_numeric_drift(gpu_int8_reports)
    if not visual_corpus_cases:
        visual_review_pending = True
        reasons.append("Visual corpus review is still pending because no corpus manifest was provided.")
    if not gpu_int8_reports:
        decision_status = "insufficient_data"
    elif gpu_gate_failed:
        decision_status = "hold"
    elif visual_review_pending:
        decision_status = "pending_visual_review"
    else:
        decision_status = "candidate_pass"

    return {
        "status": decision_status,
        "gpu_int8_speedup_threshold": GPU_INT8_SPEEDUP_THRESHOLD,
        "gpu_int8_speedups": speedups,
        "cpu_int8_fallback_available": any(
            report.get("effective_precision") == "int8" for report in cpu_int8_cli_reports
        ),
        "visual_review_required_categories": list(REQUIRED_CORPUS_CATEGORIES),
        "visual_corpus_case_count": len(visual_corpus_cases),
        "numeric_drift_summary": numeric_drift,
        "reasons": reasons,
    }


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Run the CorridorKey INT8 decision program for Windows RTX."
    )
    parser.add_argument("--corridorkey-cli", type=str, required=True, help="Path to corridorkey.exe")
    parser.add_argument("--models-dir", type=str, required=True, help="Path to the packaged models directory")
    parser.add_argument("--output", type=str, required=True, help="Path to the JSON decision report")
    parser.add_argument("--checkpoint", type=str, default="", help="CorridorKey PyTorch checkpoint")
    parser.add_argument("--repo-path", type=str, default="", help="CorridorKey Python source repository")
    parser.add_argument("--corpus-manifest", type=str, default="", help="Path to the fixed visual corpus manifest")
    parser.add_argument(
        "--resolutions",
        type=int,
        nargs="+",
        default=[1024, 1536],
        help="Resolutions to compare for the GPU INT8 gate",
    )
    parser.add_argument("--warmup-runs", type=int, default=2, help="Warmup iterations per benchmark")
    parser.add_argument("--benchmark-runs", type=int, default=5, help="Steady-state iterations per benchmark")
    parser.add_argument(
        "--calibration-case-limit",
        type=int,
        default=4,
        help="Maximum number of corpus cases to reuse for INT8 calibration",
    )
    parser.add_argument(
        "--calibration-cache",
        type=str,
        default="",
        help="Optional Torch-TensorRT INT8 calibration cache path",
    )
    parser.add_argument("--skip-cli", action="store_true", help="Skip CorridorKey CLI benchmarks")
    parser.add_argument(
        "--skip-torchtrt",
        action="store_true",
        help="Skip Torch-TensorRT GPU candidate benchmarks",
    )
    return parser.parse_args()


def main() -> None:
    args = parse_args()

    cli_path = Path(args.corridorkey_cli).resolve()
    models_dir = Path(args.models_dir).resolve()
    output_path = Path(args.output).resolve()
    output_path.parent.mkdir(parents=True, exist_ok=True)

    visual_corpus_cases: list[VisualCorpusCase] = []
    if args.corpus_manifest:
        visual_corpus_cases = load_visual_corpus_cases(Path(args.corpus_manifest).resolve())

    fp16_cli_reports: list[dict[str, Any]] = []
    cpu_int8_cli_reports: list[dict[str, Any]] = []
    gpu_int8_reports: list[dict[str, Any]] = []
    errors: list[str] = []

    if not args.skip_cli:
        for resolution in args.resolutions:
            fp16_report = run_cli_benchmark(
                cli_path, models_dir, "tensorrt", "fp16", resolution, None
            )
            fp16_cli_reports.append(
                summarize_cli_report(fp16_report, "tensorrt", "fp16", resolution)
            )

            cpu_report = run_cli_benchmark(
                cli_path, models_dir, "cpu", "int8", resolution, None
            )
            cpu_int8_cli_reports.append(
                summarize_cli_report(cpu_report, "cpu", "int8", resolution)
            )

    if not args.skip_torchtrt:
        if not args.checkpoint or not args.repo_path:
            errors.append("Torch-TensorRT benchmark was requested without --checkpoint and --repo-path.")
        else:
            calibration_cache_path = (
                Path(args.calibration_cache).resolve() if args.calibration_cache else None
            )
            for resolution in args.resolutions:
                try:
                    gpu_int8_reports.append(
                        run_torch_tensorrt_candidate_benchmark(
                            checkpoint_path=Path(args.checkpoint).resolve(),
                            repo_path=Path(args.repo_path).resolve(),
                            cases=visual_corpus_cases,
                            resolution=resolution,
                            precision="int8",
                            warmup_runs=args.warmup_runs,
                            benchmark_runs=args.benchmark_runs,
                            calibration_case_limit=args.calibration_case_limit,
                            calibration_cache_path=calibration_cache_path,
                        )
                    )
                except Exception as exc:  # noqa: BLE001
                    errors.append(f"Torch-TensorRT INT8 benchmark failed at {resolution}px: {exc}")

    decision = build_decision_summary(
        fp16_cli_reports, cpu_int8_cli_reports, gpu_int8_reports, visual_corpus_cases
    )

    report = {
        "program": "corridorkey_int8_decision_program",
        "version": 1,
        "thresholds": {
            "gpu_int8_min_speedup": GPU_INT8_SPEEDUP_THRESHOLD,
            "requires_visual_review": True,
        },
        "corpus": {
            "required_categories": list(REQUIRED_CORPUS_CATEGORIES),
            "case_count": len(visual_corpus_cases),
            "manifest": str(Path(args.corpus_manifest).resolve()) if args.corpus_manifest else "",
        },
        "fp16_cli_baseline": fp16_cli_reports,
        "cpu_int8_fallback": cpu_int8_cli_reports,
        "torch_tensorrt_gpu_int8": gpu_int8_reports,
        "decision": decision,
        "errors": errors,
    }

    output_path.write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
