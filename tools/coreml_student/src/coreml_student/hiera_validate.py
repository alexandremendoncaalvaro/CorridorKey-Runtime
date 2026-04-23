"""Phase 1 measurement harness for the Hiera->CoreML spike.

Runs the two Phase 1 checks we can perform from Python:

1. ``.mlpackage`` loads and ``predict`` returns tensors of the expected
   shape. Failure here indicates the exported model is structurally
   broken despite conversion succeeding.
2. End-to-end latency on ``CPU_AND_NE`` compute units, measured as the
   wall-clock time of ``predict`` averaged over N trials after warmup.
3. Quality parity against the PyTorch teacher on the same input image,
   reported as per-pixel mean-absolute-error on the alpha channel.

ANE residency is the third Phase 1 criterion; coremltools does not
expose it programmatically, so it is checked out-of-band with Xcode
Instruments' Core ML track. This script surfaces the other two
criteria so the go/no-go call has hard numbers instead of conjecture.
"""

from __future__ import annotations

import argparse
import sys
import time
from pathlib import Path

import numpy as np
import torch


def _load_coreml_model(path: Path, compute: str) -> object:
    import coremltools as ct

    unit_map = {
        "cpu_and_ne": ct.ComputeUnit.CPU_AND_NE,
        "cpu_only": ct.ComputeUnit.CPU_ONLY,
        "cpu_and_gpu": ct.ComputeUnit.CPU_AND_GPU,
        "all": ct.ComputeUnit.ALL,
    }
    unit = unit_map[compute.lower()]
    return ct.models.MLModel(str(path), compute_units=unit)


def _synthetic_rgba(img_size: int, seed: int = 0) -> torch.Tensor:
    """Deterministic RGBA input so validation runs are reproducible.

    The distillation inputs are 4-channel (RGB premultiplied + trimap-ish
    alpha). A deterministic sinusoid per channel exercises attention
    without introducing randomness that would muddy the MAE reading.
    """
    torch.manual_seed(seed)
    xs = torch.linspace(0.0, 1.0, img_size)
    ys = torch.linspace(0.0, 1.0, img_size)
    grid_x, grid_y = torch.meshgrid(xs, ys, indexing="xy")
    chans = []
    for k in range(4):
        freq = 2.0 + k
        c = 0.5 + 0.4 * torch.sin(freq * grid_x * 3.14159) * torch.cos(freq * grid_y * 3.14159)
        chans.append(c)
    return torch.stack(chans, dim=0).unsqueeze(0).clamp(0.0, 1.0)


def _coreml_predict(mlmodel: object, x_np: np.ndarray) -> dict[str, np.ndarray]:
    feeds = {"input_rgba": x_np.astype(np.float32)}
    return mlmodel.predict(feeds)


def _timeit_coreml(mlmodel: object, x_np: np.ndarray, trials: int) -> float:
    # Warmup (first predict compiles the model for the compute unit).
    _ = _coreml_predict(mlmodel, x_np)
    _ = _coreml_predict(mlmodel, x_np)
    start = time.perf_counter()
    for _ in range(trials):
        _ = _coreml_predict(mlmodel, x_np)
    elapsed = time.perf_counter() - start
    return elapsed / trials


def _timeit_torch(teacher, x: torch.Tensor, trials: int) -> float:
    # Warmup
    _ = teacher(x)
    _ = teacher(x)
    start = time.perf_counter()
    for _ in range(trials):
        _ = teacher(x)
    elapsed = time.perf_counter() - start
    return elapsed / trials


def validate(
    mlpackage_path: Path,
    checkpoint_path: Path,
    repo_path: Path | None,
    img_size: int,
    trials: int,
    compute: str,
    use_refiner: bool,
) -> int:
    try:
        import coremltools  # noqa: F401
    except ImportError as exc:
        print(f"[hiera_validate] coremltools not installed: {exc}", file=sys.stderr)
        return 2

    from .teacher import load_teacher

    print(f"[hiera_validate] loading {mlpackage_path} on {compute}")
    mlmodel = _load_coreml_model(mlpackage_path, compute)

    spec = mlmodel.get_spec()
    in_names = [i.name for i in spec.description.input]
    out_names = [o.name for o in spec.description.output]
    print(f"[hiera_validate] input names: {in_names}")
    print(f"[hiera_validate] output names: {out_names}")

    print(f"[hiera_validate] loading teacher from {checkpoint_path}")
    teacher = load_teacher(
        checkpoint_path=checkpoint_path,
        img_size=img_size,
        repo_path=repo_path,
        device="cpu",
        use_refiner=use_refiner,
    )

    x = _synthetic_rgba(img_size).float()
    x_np = x.numpy()

    # Quality parity: alpha MAE against teacher forward.
    with torch.no_grad():
        ref = teacher(x)
    ref_alpha = ref.alpha.numpy()
    ref_fg = ref.foreground.numpy()

    cm_out = _coreml_predict(mlmodel, x_np)
    print(f"[hiera_validate] coreml output keys: {sorted(cm_out.keys())}")

    # coremltools returns outputs keyed by the model's output names;
    # resolve alpha vs foreground by shape (alpha is 1-channel).
    cm_alpha = None
    cm_fg = None
    for name, arr in cm_out.items():
        if arr.ndim == 4 and arr.shape[1] == 1:
            cm_alpha = arr
        elif arr.ndim == 4 and arr.shape[1] == 3:
            cm_fg = arr
    if cm_alpha is None:
        print(
            "[hiera_validate] could not identify alpha in coreml output",
            file=sys.stderr,
        )
        return 1

    mae_alpha = float(np.mean(np.abs(cm_alpha - ref_alpha)))
    max_err_alpha = float(np.max(np.abs(cm_alpha - ref_alpha)))
    print(
        f"[hiera_validate] alpha MAE={mae_alpha:.6f} max_err={max_err_alpha:.6f} "
        f"shape_cm={cm_alpha.shape} shape_ref={ref_alpha.shape}"
    )
    if cm_fg is not None and ref_fg is not None and cm_fg.shape == ref_fg.shape:
        mae_fg = float(np.mean(np.abs(cm_fg - ref_fg)))
        print(f"[hiera_validate] foreground MAE={mae_fg:.6f}")

    # Latency.
    print(f"[hiera_validate] timing {trials} trials on CoreML ({compute})")
    cm_mean_s = _timeit_coreml(mlmodel, x_np, trials)
    print(f"[hiera_validate] coreml mean latency: {cm_mean_s * 1000.0:.2f} ms")

    print(f"[hiera_validate] timing {trials} trials on PyTorch teacher (CPU)")
    torch_mean_s = _timeit_torch(teacher, x, trials)
    print(f"[hiera_validate] teacher mean latency: {torch_mean_s * 1000.0:.2f} ms")

    speedup = torch_mean_s / cm_mean_s if cm_mean_s > 0 else float("inf")
    print(f"[hiera_validate] speedup (teacher / coreml): {speedup:.2f}x")

    # Emit a small machine-parseable summary for the report generator.
    print("---RESULT---")
    print(f"mae_alpha={mae_alpha:.6f}")
    print(f"max_err_alpha={max_err_alpha:.6f}")
    print(f"coreml_ms={cm_mean_s * 1000.0:.2f}")
    print(f"teacher_ms={torch_mean_s * 1000.0:.2f}")
    print(f"img_size={img_size}")
    print(f"compute={compute}")
    return 0


def _cli(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Validate a Hiera->CoreML .mlpackage: load, predict, MAE vs teacher, latency."
    )
    parser.add_argument(
        "--mlpackage",
        default="build/coreml_spike/greenformer_hiera.mlpackage",
    )
    parser.add_argument("--ckpt", default="models/CorridorKey.pth")
    parser.add_argument("--repo-path", default=None)
    parser.add_argument("--img-size", type=int, default=512)
    parser.add_argument("--trials", type=int, default=10)
    parser.add_argument("--compute", default="cpu_and_ne")
    parser.add_argument(
        "--no-refiner",
        action="store_true",
        help="Match the no-refiner export if the .mlpackage was produced without it.",
    )
    args = parser.parse_args(argv)

    return validate(
        mlpackage_path=Path(args.mlpackage),
        checkpoint_path=Path(args.ckpt),
        repo_path=Path(args.repo_path) if args.repo_path else None,
        img_size=args.img_size,
        trials=args.trials,
        compute=args.compute,
        use_refiner=not args.no_refiner,
    )


if __name__ == "__main__":
    raise SystemExit(_cli())
