"""Compile a CorridorKey PyTorch checkpoint into a Torch-TensorRT (.ts) engine.

Reproducible recipe for the blue Windows RTX model pack. Each rung of the
catalog (`src/app/runtime_contracts.cpp` make_model_entry "fp16-blue" /
"fp32-blue") points at one .ts produced by this script.

Why this script exists separately from `tools/model_exporter/export_onnx.py`:
- `export_onnx.py` is pinned to `torch==2.3.1+cu121` so it can produce ONNX
  consumable by ONNX Runtime + TensorRT-RTX EP (the green path).
- Torch-TensorRT 2.8.0 requires `torch==2.8.0+cu128`. The two stacks cannot
  coexist in one venv. This subproject owns the torch 2.8 / TRT 10.12 pin
  that matches `vendor/torchtrt-windows/` (the runtime DLL bundle the OFX
  plugin loads via AddDllDirectory at startup).

Why blue uses .ts instead of ONNX:
- The blue checkpoint's FP16 ONNX produces all-NaN inference output on the
  TensorRT and CUDA execution providers (see
  docs/OPTIMIZATION_MEASUREMENTS.md "Blue dedicated baselines"). Compiling
  the blue weights directly through Torch-TensorRT bypasses the ONNX → EP
  layer entirely.

Why FP32 at >=1536:
- FP16 trace-time conversion of the blue weights NaNs out at 1536 and
  2048. The blue checkpoint is more sensitive than green to FP16
  underflow at high resolutions. Recipe forces FP32 enabled-precision
  for those rungs and FP16 for 512 / 1024.

GPU memory budget: TensorRT's engine builder peaks at roughly 5-10x the
final engine size. Blue 2048 FP32 final engine is ~825 MB; the build is
expected to OOM on cards with <16 GB VRAM. RTX 3080 (10 GB) compiles
512/1024/1536 cleanly but cannot reach 2048 -- that rung is staged via
cloud GPU compile per HANDOFF Sprint 2.
"""

from __future__ import annotations

import argparse
import math
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent.parent

# Pinned upstream SHA must match tools/model_exporter/export_onnx.py
# NIKO_UPSTREAM_PIN. Keeping the two in sync is a manual contract: if
# export_onnx.py bumps its pin, this script must bump as well so the
# compiled .ts and the produced .onnx come from the same GreenFormer
# definition.
NIKO_UPSTREAM_PIN = "422f9999d1d83323534d2da9d776086a3134050d"

# Default rungs: 512 / 1024 use FP16 (passes through Torch-TensorRT cleanly
# for blue), 1536 / 2048 use FP32 (FP16 NaNs at trace time for blue).
DEFAULT_RUNGS_FP16 = (512, 1024)
DEFAULT_RUNGS_FP32 = (1536, 2048)

# Upstream blue checkpoint published by the model author. Mirrors the
# fetch_models.ps1 $HfUpstreamBlueRepo default.
DEFAULT_HF_BLUE_REPO = "nikopueringer/CorridorKeyBlue_1.0"
DEFAULT_HF_BLUE_FILENAME = "CorridorKeyBlue_1.0.pth"


def clone_upstream_repo(target_dir: Path) -> Path:
    """Clone nikopueringer/CorridorKey at the pinned SHA.

    Mirrors the helper in tools/model_exporter/export_onnx.py so the
    GreenFormer module is identical between the ONNX and the .ts
    pipelines.
    """
    print(f"[compile_torchtrt] Cloning upstream pinned to {NIKO_UPSTREAM_PIN[:12]} ...", flush=True)
    subprocess.run(
        ["git", "clone", "https://github.com/nikopueringer/CorridorKey.git", str(target_dir)],
        check=True,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    subprocess.run(
        ["git", "-C", str(target_dir), "checkout", "--quiet", NIKO_UPSTREAM_PIN],
        check=True,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    return target_dir


def download_blue_checkpoint(out_path: Path, repo: str, filename: str) -> Path:
    """Fetch the upstream blue .pth into ``out_path`` if not already there."""
    if out_path.exists():
        print(f"[compile_torchtrt] Reusing existing checkpoint at {out_path}", flush=True)
        return out_path
    from huggingface_hub import hf_hub_download

    print(f"[compile_torchtrt] Downloading {repo}/{filename} ...", flush=True)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    downloaded = hf_hub_download(
        repo_id=repo,
        filename=filename,
        local_dir=str(out_path.parent),
    )
    src = Path(downloaded)
    if src.resolve() != out_path.resolve():
        shutil.copy2(src, out_path)
    return out_path


def load_greenformer(checkpoint_path: Path, img_size: int):
    """Load GreenFormer + interpolate pos_embed to ``img_size``.

    Same logic as tools/model_exporter/export_onnx.py
    load_model_with_pos_embed_interpolation, duplicated here because the
    two scripts run in incompatible torch venvs and cannot import each
    other.
    """
    import torch
    import torch.nn.functional as F  # noqa: N812
    from CorridorKeyModule.core.model_transformer import GreenFormer

    print(f"[compile_torchtrt] Building GreenFormer at {img_size}x{img_size} ...", flush=True)
    model = GreenFormer(
        encoder_name="hiera_base_plus_224.mae_in1k_ft_in1k",
        img_size=img_size,
        use_refiner=True,
    )
    model.eval()

    checkpoint = torch.load(checkpoint_path, map_location="cpu", weights_only=True)
    state_dict = checkpoint.get("state_dict", checkpoint)

    new_state_dict = {}
    model_state = model.state_dict()
    for k, v in state_dict.items():
        if k.startswith("_orig_mod."):
            k = k[10:]
        if "pos_embed" in k and k in model_state and v.shape != model_state[k].shape:
            n_src = v.shape[1]
            channels = v.shape[2]
            grid_src = int(math.sqrt(n_src))
            grid_dst = int(math.sqrt(model_state[k].shape[1]))
            v_img = v.permute(0, 2, 1).view(1, channels, grid_src, grid_src)
            v_resized = F.interpolate(
                v_img, size=(grid_dst, grid_dst), mode="bicubic", align_corners=False
            )
            v = v_resized.flatten(2).transpose(1, 2)
        new_state_dict[k] = v

    missing, unexpected = model.load_state_dict(new_state_dict, strict=False)
    if missing or unexpected:
        print(f"[compile_torchtrt] state_dict mismatch missing={missing[:3]} unexpected={unexpected[:3]}",
              file=sys.stderr)
        raise SystemExit(
            "Bump NIKO_UPSTREAM_PIN to a SHA whose GreenFormer matches the checkpoint."
        )
    return model


def compile_one_rung(checkpoint_path: Path, resolution: int, precision: str,
                     out_path: Path) -> None:
    """Compile a single rung -> .ts using torch_tensorrt.compile."""
    import torch
    import torch_tensorrt as torch_trt

    if not torch.cuda.is_available():
        raise SystemExit("CUDA device required for torch_tensorrt.compile.")

    base = load_greenformer(checkpoint_path, resolution)

    class CompileWrapper(torch.nn.Module):
        """Mirrors the ONNXWrapper in export_onnx.py so the runtime
        TorchTrtSession sees a (alpha, fg) tuple regardless of precision.
        """

        def __init__(self, model: torch.nn.Module) -> None:
            super().__init__()
            self.model = model

        def forward(self, x: torch.Tensor) -> tuple[torch.Tensor, torch.Tensor]:
            out = self.model(x)
            return out["alpha"], out.get("fg", x[:, :3, :, :])

    wrapped = CompileWrapper(base)
    if precision == "fp16":
        wrapped = wrapped.half()
        torch_dtype = torch.float16
        enabled = {torch.float16}
    elif precision == "fp32":
        torch_dtype = torch.float32
        enabled = {torch.float32}
    else:
        raise ValueError(f"unknown precision {precision!r}")

    wrapped = wrapped.cuda().eval()

    example = torch.zeros((1, 4, resolution, resolution), dtype=torch_dtype, device="cuda")

    print(
        f"[compile_torchtrt] torch_tensorrt.compile (dynamo IR) "
        f"resolution={resolution} precision={precision} ...",
        flush=True,
    )
    # Why ir="dynamo" instead of ir="ts":
    # - ir="ts" internally torch.jit.scripts the model. The Hiera backbone
    #   uses `x.view(*([B] + cur_size + [C]))` patterns that torch.jit.script
    #   rejects with "cannot statically infer the expected size of a list".
    # - Trying torch.jit.trace + torch_trt.ts.compile produces a multi-engine
    #   ScriptModule whose intermediate tensors flow through PyTorch ops,
    #   tripping setInputShape failures inside the TRT sub-engines at
    #   runtime ("Error while setting the input shape").
    # - ir="dynamo" routes through torch.export and produces a single
    #   GraphModule with one TRT engine, which the C++ runtime
    #   (src/core/torch_trt_session.cpp) loads via torch::jit::load and
    #   forwards a single tensor to. Inspection of the existing
    #   models/corridorkey_blue_torchtrt_fp16_1024.ts confirms it is also
    #   a GraphModule produced by this path.
    with torch.no_grad():
        compiled = torch_trt.compile(
            wrapped,
            ir="dynamo",
            inputs=[example],
            enabled_precisions=enabled,
            truncate_double=True,
        )

    # The dynamo path returns a torch.fx.GraphModule. Wrap as ScriptModule
    # via torch.jit.trace so torch.jit.save produces a .ts the C++ runtime
    # can load.
    out_path.parent.mkdir(parents=True, exist_ok=True)
    print(f"[compile_torchtrt] Tracing dynamo GraphModule into ScriptModule ...",
          flush=True)
    with torch.no_grad():
        scripted = torch.jit.trace(compiled, example, strict=False)
    print(f"[compile_torchtrt] Saving {out_path} ...", flush=True)
    torch.jit.save(scripted, str(out_path))
    print(f"[compile_torchtrt] Done {out_path} ({out_path.stat().st_size / 1e6:.1f} MB)", flush=True)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__.split("\n", 1)[0])
    parser.add_argument(
        "--checkpoint",
        type=Path,
        help="Local path to CorridorKeyBlue_1.0.pth. When omitted, the file is "
             "downloaded from --hf-blue-repo into models/.",
    )
    parser.add_argument(
        "--out-dir",
        type=Path,
        default=REPO_ROOT / "models",
        help="Directory where the compiled .ts files are written.",
    )
    parser.add_argument(
        "--resolutions-fp16",
        type=int,
        nargs="*",
        default=list(DEFAULT_RUNGS_FP16),
        help="Resolutions to compile with FP16 enabled-precision.",
    )
    parser.add_argument(
        "--resolutions-fp32",
        type=int,
        nargs="*",
        default=list(DEFAULT_RUNGS_FP32),
        help="Resolutions to compile with FP32 enabled-precision (NaN-safe for blue).",
    )
    parser.add_argument(
        "--repo-path",
        type=Path,
        help="Local checkout of nikopueringer/CorridorKey. When omitted the repo "
             f"is cloned and pinned to {NIKO_UPSTREAM_PIN[:12]}.",
    )
    parser.add_argument(
        "--hf-blue-repo",
        default=DEFAULT_HF_BLUE_REPO,
        help="Upstream HF repo for the blue training checkpoint.",
    )
    parser.add_argument(
        "--hf-blue-filename",
        default=DEFAULT_HF_BLUE_FILENAME,
    )
    parser.add_argument(
        "--force",
        action="store_true",
        help="Overwrite existing .ts files in --out-dir.",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()

    repo_to_clean: Path | None = None
    if args.repo_path:
        repo_path = args.repo_path.resolve()
    else:
        repo_path = Path(tempfile.mkdtemp(prefix="corridorkey_blue_compile_"))
        clone_upstream_repo(repo_path)
        repo_to_clean = repo_path

    if not (repo_path / "CorridorKeyModule").exists():
        print(f"[compile_torchtrt] Upstream repo missing CorridorKeyModule under {repo_path}",
              file=sys.stderr)
        return 1
    sys.path.insert(0, str(repo_path))

    checkpoint_path = args.checkpoint or (REPO_ROOT / "models" / DEFAULT_HF_BLUE_FILENAME)
    if not checkpoint_path.exists():
        download_blue_checkpoint(checkpoint_path, args.hf_blue_repo, args.hf_blue_filename)

    args.out_dir.mkdir(parents=True, exist_ok=True)

    failures: list[tuple[int, str, str]] = []
    try:
        for resolution in args.resolutions_fp16:
            out_path = args.out_dir / f"corridorkey_blue_torchtrt_fp16_{resolution}.ts"
            if out_path.exists() and not args.force:
                print(f"[compile_torchtrt] Skip {out_path.name} (exists, pass --force to overwrite)")
                continue
            try:
                compile_one_rung(checkpoint_path, resolution, "fp16", out_path)
            except Exception as exc:  # noqa: BLE001 -- log every failure but keep going
                failures.append((resolution, "fp16", f"{type(exc).__name__}: {exc}"))
                print(f"[compile_torchtrt] FAILED fp16 {resolution}: {exc}", file=sys.stderr)

        for resolution in args.resolutions_fp32:
            out_path = args.out_dir / f"corridorkey_blue_torchtrt_fp32_{resolution}.ts"
            if out_path.exists() and not args.force:
                print(f"[compile_torchtrt] Skip {out_path.name} (exists, pass --force to overwrite)")
                continue
            try:
                compile_one_rung(checkpoint_path, resolution, "fp32", out_path)
            except Exception as exc:  # noqa: BLE001
                failures.append((resolution, "fp32", f"{type(exc).__name__}: {exc}"))
                print(f"[compile_torchtrt] FAILED fp32 {resolution}: {exc}", file=sys.stderr)
    finally:
        if repo_to_clean is not None:
            shutil.rmtree(repo_to_clean, ignore_errors=True)

    if failures:
        print("[compile_torchtrt] Failures:", file=sys.stderr)
        for res, prec, msg in failures:
            print(f"  {prec}_{res}: {msg}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
