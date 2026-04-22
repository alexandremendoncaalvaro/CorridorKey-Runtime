"""Spike: attempt a direct CoreML export of the Hiera-based GreenFormer teacher.

If this conversion succeeds we can ship teacher quality on the Apple Neural
Engine without paying for a full distillation run. The obstacle is Hiera's
``MaskUnitAttention`` forward: it rearranges the query/key/value tensors to
a 5-dimensional layout of ``(B, num_mask_units, Q, heads, head_dim)`` before
calling ``F.scaled_dot_product_attention``. ``coremltools`` lowers SDPA via a
path that assumes 4-D ``(B, heads, Q, head_dim)`` inputs and bails out on the
5-D case, so tracing the unmodified teacher never produces an ``.mlpackage``.

The workaround comes from two sources that landed the same pattern:

* ``apple/ml-ane-transformers`` demonstrates that an einsum-style 4-D SDPA is
  required for the compiler to keep the op on the Neural Engine rather than
  falling back to GPU/CPU.
* ``99oblivius/CorridorKey-Engine`` (``ck_engine/optimized_model.py``) patches
  Hiera's ``MaskUnitAttention.forward`` to ``reshape`` into 4-D, run SDPA, and
  ``reshape`` back, which is functionally equivalent for contiguous inputs.

This module performs the same patch, runs a trace, and tries to convert. The
outcome - success with an ``.mlpackage`` on disk, or a logged conversion
failure - decides whether the distillation work in ``tools/coreml_student`` is
still needed. The script performs no runtime changes; it is diagnostic only.
"""

from __future__ import annotations

import argparse
import sys
import types
from pathlib import Path
from typing import Any

import torch
import torch.nn.functional as F


def _patched_mask_unit_attention_forward(self: Any, x: torch.Tensor) -> torch.Tensor:
    """Drop-in replacement for ``hiera.MaskUnitAttention.forward``.

    Original layout (condensed):

        qkv = self.qkv(x).reshape(B, N, 3, num_mu, heads, head_dim)
        q, k, v = qkv.unbind(2)                              # each (B, N, num_mu, heads, head_dim)
        attn = F.scaled_dot_product_attention(q, k, v)       # 5-D -> coremltools bails
        x    = attn.reshape(B, N, num_mu * heads * head_dim)

    Patched layout folds ``num_mu`` into the batch axis so SDPA sees the
    canonical 4-D ``(B', heads, Q, head_dim)`` shape that coremltools knows
    how to partition onto the Neural Engine.
    """
    B, N, _ = x.shape
    num_mu = self.q_stride
    q_dim = self.dim_out // num_mu
    head_dim = q_dim // self.heads

    qkv = (
        self.qkv(x)
        .reshape(B, N, 3, num_mu, self.heads, head_dim)
        .permute(2, 0, 3, 4, 1, 5)  # (3, B, num_mu, heads, N, head_dim)
    )
    q, k, v = qkv[0], qkv[1], qkv[2]

    q = q.reshape(B * num_mu, self.heads, N, head_dim)
    k = k.reshape(B * num_mu, self.heads, N, head_dim)
    v = v.reshape(B * num_mu, self.heads, N, head_dim)

    attn = F.scaled_dot_product_attention(q, k, v)
    attn = attn.reshape(B, num_mu, self.heads, N, head_dim)
    attn = attn.permute(0, 3, 1, 2, 4).reshape(B, N, num_mu * self.heads * head_dim)

    proj = self.proj(attn)
    return proj


def patch_hiera_attention() -> int:
    """Monkey-patch every ``MaskUnitAttention`` instance's forward method.

    Returns the number of classes patched so the caller can sanity-check that
    at least one site was hit; a zero return means Hiera's layout changed
    upstream and the spike must be revisited.
    """
    patched = 0
    for module_name, module in list(sys.modules.items()):
        if not module_name.startswith(("hiera", "timm.models.hiera")):
            continue
        for attr_name in dir(module):
            if attr_name != "MaskUnitAttention":
                continue
            cls = getattr(module, attr_name)
            if not isinstance(cls, type):
                continue
            cls.forward = _patched_mask_unit_attention_forward  # type: ignore[assignment]
            patched += 1
    return patched


class _TracingWrapper(torch.nn.Module):
    """Flatten ``GreenFormer``'s dict output into a tuple for tracing."""

    def __init__(self, teacher: torch.nn.Module) -> None:
        super().__init__()
        self._teacher = teacher

    def forward(self, x: torch.Tensor) -> tuple[torch.Tensor, torch.Tensor]:
        raw = self._teacher(x)
        alpha = raw["alpha"]
        foreground = raw.get("fg")
        if foreground is None:
            foreground = x[:, :3]
        return alpha, foreground


def run_spike(
    checkpoint_path: Path,
    output_path: Path,
    img_size: int,
    repo_path: Path | None,
) -> int:
    """Load teacher, apply the SDPA patch, trace, and convert to CoreML.

    Exit codes mirror typical CLI conventions: ``0`` on a viable
    ``.mlpackage``, ``1`` on conversion failure (expected outcome that
    invalidates the Phase 2 hypothesis), ``2`` on setup failure (missing
    dependency, checkpoint, etc.).
    """
    try:
        import coremltools as ct
    except ImportError as exc:
        print(f"[hiera_export] coremltools not installed: {exc}", file=sys.stderr)
        return 2

    from .teacher import load_teacher

    try:
        teacher = load_teacher(
            checkpoint_path=checkpoint_path,
            img_size=img_size,
            repo_path=repo_path,
            device="cpu",
        )
    except Exception as exc:
        print(f"[hiera_export] teacher load failed: {exc}", file=sys.stderr)
        return 2

    patched = patch_hiera_attention()
    if patched == 0:
        print(
            "[hiera_export] MaskUnitAttention not found; Hiera layout may "
            "have changed upstream. Spike aborted.",
            file=sys.stderr,
        )
        return 2
    print(f"[hiera_export] patched {patched} MaskUnitAttention class(es)")

    wrapper = _TracingWrapper(teacher.module).eval()
    example = torch.zeros(1, 4, img_size, img_size)

    try:
        traced = torch.jit.trace(wrapper, example, strict=False)
    except Exception as exc:
        print(f"[hiera_export] torch.jit.trace failed: {exc}", file=sys.stderr)
        return 1

    try:
        mlmodel = ct.convert(
            traced,
            inputs=[
                ct.TensorType(
                    name="input_rgba",
                    shape=ct.EnumeratedShapes(
                        shapes=[
                            (1, 4, 512, 512),
                            (1, 4, 768, 768),
                            (1, 4, 1024, 1024),
                        ],
                        default=(1, 4, img_size, img_size),
                    ),
                    dtype=float,
                )
            ],
            compute_units=ct.ComputeUnit.CPU_AND_NE,
            compute_precision=ct.precision.FLOAT16,
            convert_to="mlprogram",
            minimum_deployment_target=ct.target.macOS14,
        )
    except Exception as exc:
        print(f"[hiera_export] coremltools.convert failed: {exc}", file=sys.stderr)
        return 1

    output_path.parent.mkdir(parents=True, exist_ok=True)
    mlmodel.save(str(output_path))
    print(f"[hiera_export] saved {output_path}")
    return 0


def _cli(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Phase 2 spike: direct Hiera->CoreML export with SDPA squeeze."
    )
    parser.add_argument("--ckpt", default="models/CorridorKey.pth")
    parser.add_argument("--output", default="build/coreml_spike/greenformer_hiera.mlpackage")
    parser.add_argument("--img-size", type=int, default=512)
    parser.add_argument("--repo-path", default=None)
    args = parser.parse_args(argv)

    return run_spike(
        checkpoint_path=Path(args.ckpt),
        output_path=Path(args.output),
        img_size=args.img_size,
        repo_path=Path(args.repo_path) if args.repo_path else None,
    )


if __name__ == "__main__":
    raise SystemExit(_cli())
