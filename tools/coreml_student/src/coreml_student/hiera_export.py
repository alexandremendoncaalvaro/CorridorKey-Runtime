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
    """Drop-in replacement for ``timm.models.hiera.MaskUnitAttention.forward``.

    Timm's current Hiera keeps two code paths: a 5-D windowed path
    (``use_mask_unit_attn=True``) that shapes QKV as
    ``(B, heads, num_windows, tokens_per_window, head_dim)``, and a 4-D
    global path (``use_mask_unit_attn=False``) that already matches what
    coremltools expects. The windowed path is where ``coremltools.convert``
    bails out because its SDPA lowering assumes 4-D inputs.

    This patch preserves semantics: when windowed, it folds
    ``num_windows`` into the batch axis before SDPA and unfolds after, so
    the math is identical but SDPA sees the canonical 4-D layout that
    coremltools can keep on the Neural Engine. The global path is passed
    through untouched.
    """
    # Force Python-int shape values so the trace stores them as constants
    # rather than ``floor_divide`` graph ops. On Windows ``x.shape[1]``
    # during trace resolves to a numpy.intc-typed value; propagating that
    # through ``//`` creates a MIL ``floor_divide`` with an unsupported
    # dtype. ``int(...)`` coerces to Python int on every platform so
    # coremltools sees scalar constants instead.
    B = int(x.shape[0])
    N = int(x.shape[1])

    if self.use_mask_unit_attn:
        num_windows = N // (int(self.q_stride) * int(self.window_size))
        tpw = N // num_windows  # tokens per window before q_stride pooling

        # Split QKV along the channel axis first so no intermediate tensor
        # ever reaches rank 6 (Core ML refuses rank > 5). timm's reference
        # forward builds ``(B, tpw, num_windows, 3, heads, head_dim)`` which
        # is rank 6 and would be rejected by the converter.
        qkv_proj = self.qkv(x)  # (B, N, 3*heads*head_dim)
        q_proj, k_proj, v_proj = qkv_proj.chunk(3, dim=-1)

        # Reshape each projection to (B, tpw, num_windows, heads, head_dim).
        # The ``-1`` slot in timm's version resolves to ``tpw``; the memory
        # layout assumes mask-unit tokens are interleaved so ``tpw`` is the
        # outer axis and ``num_windows`` is the inner axis of N.
        q = q_proj.reshape(B, tpw, num_windows, self.heads, self.head_dim)
        k = k_proj.reshape(B, tpw, num_windows, self.heads, self.head_dim)
        v = v_proj.reshape(B, tpw, num_windows, self.heads, self.head_dim)

        # Target 4-D SDPA layout: ``(B*num_windows, heads, tpw, head_dim)``.
        # Fold ``num_windows`` into the batch axis so SDPA sees the canonical
        # shape that coremltools keeps on the Neural Engine.
        q = q.permute(0, 2, 3, 1, 4).reshape(
            B * num_windows, self.heads, tpw, self.head_dim
        )
        k = k.permute(0, 2, 3, 1, 4).reshape(
            B * num_windows, self.heads, tpw, self.head_dim
        )
        v = v.permute(0, 2, 3, 1, 4).reshape(
            B * num_windows, self.heads, tpw, self.head_dim
        )

        if self.q_stride > 1:
            tpw_q = tpw // self.q_stride
            q = q.reshape(
                B * num_windows, self.heads, self.q_stride, tpw_q, self.head_dim
            ).amax(dim=2)
        else:
            tpw_q = tpw

        q, k, v = q.contiguous(), k.contiguous(), v.contiguous()
        attn = F.scaled_dot_product_attention(q, k, v)

        # Restore the flattened ``(B, tpw_q * num_windows, heads * head_dim)``
        # layout. timm emits ``transpose(1, 3).reshape(B, -1, dim_out)`` from
        # ``(B, heads, num_windows, tpw_q, head_dim)``, which puts tpw_q
        # outer and num_windows inner in the final N axis; the permute below
        # reproduces that ordering without going through rank 6.
        attn = attn.reshape(B, num_windows, self.heads, tpw_q, self.head_dim)
        out = attn.permute(0, 3, 1, 2, 4).reshape(B, tpw_q * num_windows, self.dim_out)
    else:
        qkv = self.qkv(x).reshape(B, N, 3, self.heads, self.head_dim).permute(2, 0, 3, 1, 4)
        q, k, v = qkv.unbind(0)

        if self.q_stride > 1:
            q = q.view(B, self.heads, self.q_stride, -1, self.head_dim).amax(dim=2)

        q, k, v = q.contiguous(), k.contiguous(), v.contiguous()
        attn = F.scaled_dot_product_attention(q, k, v)
        out = attn.transpose(1, 2).reshape(B, -1, self.dim_out)

    return self.proj(out)


def _patched_reroll_forward(
    self: Any,
    x: torch.Tensor,
    block_idx: int,
    mask: torch.Tensor = None,
) -> torch.Tensor:
    """Drop-in replacement for ``timm.models.hiera.Reroll.forward``.

    timm's version does ``x = x.view(B, *strides, N // math.prod(strides),
    *cur_mu_shape, C)`` inside a loop where ``N`` is re-read from
    ``x.shape[1]`` each iteration. On Windows those shape reads can
    resolve to ``numpy.intc`` during trace, and the subsequent ``//``
    emits a MIL ``floor_divide`` op whose dtype coremltools refuses.
    The patch wraps every shape read in ``int(...)`` so the arithmetic
    stays in pure Python and the trace stores the counts as constants.
    Numerical behavior is unchanged; the input image size is fixed at
    trace time so all sizes are statically known.
    """
    import math as _math

    schedule, size = self.schedule[block_idx]
    B = int(x.shape[0])
    N = int(x.shape[1])
    C = int(x.shape[2])

    D = len(size)
    cur_mu_shape = [1] * D

    for strides in schedule:
        x = x.view(B, *strides, N // _math.prod(strides), *cur_mu_shape, C)
        L = len(x.shape)
        permute = (
            [0, 1 + D]
            + sum(
                [list(p) for p in zip(range(1, 1 + D), range(1 + D + 1, L - 1))],
                [],
            )
            + [L - 1]
        )
        x = x.permute(permute)
        for i in range(D):
            cur_mu_shape[i] *= strides[i]
        x = x.reshape(B, -1, *cur_mu_shape, C)
        N = int(x.shape[1])

    x = x.view(B, N, *cur_mu_shape, C)

    if mask is not None:
        return x

    # ``undo_windowing`` is defined in the same module. Resolve it via the
    # class's MRO so we do not have to duplicate the import here.
    from timm.models.hiera import undo_windowing

    x = undo_windowing(x, size, cur_mu_shape)
    return x


def patch_hiera_reroll() -> int:
    """Monkey-patch every ``Reroll`` class's ``forward`` method.

    Mirrors :func:`patch_hiera_attention` but targets the shape-dependent
    reshape loop in timm's ``Reroll.forward`` that emits ``floor_divide``
    graph ops on Windows.
    """
    patched = 0
    for module_name, module in list(sys.modules.items()):
        if not module_name.startswith(("hiera", "timm.models.hiera")):
            continue
        for attr_name in dir(module):
            if attr_name != "Reroll":
                continue
            cls = getattr(module, attr_name)
            if not isinstance(cls, type):
                continue
            cls.forward = _patched_reroll_forward  # type: ignore[assignment]
            patched += 1
    return patched


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


_ORIGINAL_INTERPOLATE = F.interpolate


def _patched_interpolate(
    input: torch.Tensor,
    size: Any = None,
    scale_factor: Any = None,
    mode: str = "nearest",
    align_corners: Any = None,
    recompute_scale_factor: Any = None,
    antialias: bool = False,
) -> torch.Tensor:
    """Convert ``size=`` calls into ``scale_factor=`` when target size is
    derivable from input shape.

    coremltools' ``torch_upsample_to_core_upsample`` MIL pass refuses to
    lower ``upsample_bilinear2d`` when the target size is a tuple of shape
    operations (e.g. ``c1.shape[2:]``). It only handles static scale
    factors. GreenFormer's decoder neck upsamples feature maps to
    ``c1.shape[2:]`` and upsamples alpha/fg logits to ``input_size``; in
    both cases the ratios are well-defined integer factors at tracing
    resolution.

    This wrapper computes the integer scale factor from the live input
    and target shapes (both static during ``torch.jit.trace``) and
    delegates to ``F.interpolate`` with ``scale_factor=`` instead of
    ``size=``. The traced graph then encodes the scales as constants,
    which the MIL pass can lower.
    """
    if size is not None and scale_factor is None:
        src_h, src_w = int(input.shape[-2]), int(input.shape[-1])
        if isinstance(size, (tuple, list)) and len(size) == 2:
            tgt_h, tgt_w = int(size[0]), int(size[1])
        elif isinstance(size, int):
            tgt_h, tgt_w = int(size), int(size)
        else:
            tgt_h, tgt_w = None, None  # unsupported; fall through
        if tgt_h is not None and tgt_w is not None:
            scale_h = tgt_h / src_h
            scale_w = tgt_w / src_w
            return _ORIGINAL_INTERPOLATE(
                input,
                scale_factor=(scale_h, scale_w),
                mode=mode,
                align_corners=align_corners,
                recompute_scale_factor=recompute_scale_factor,
                antialias=antialias,
            )
    return _ORIGINAL_INTERPOLATE(
        input,
        size=size,
        scale_factor=scale_factor,
        mode=mode,
        align_corners=align_corners,
        recompute_scale_factor=recompute_scale_factor,
        antialias=antialias,
    )


def patch_interpolate_for_trace() -> None:
    """Install :func:`_patched_interpolate` on both ``F.interpolate`` and
    ``torch.nn.functional.interpolate``.

    Patching both names covers modules that bound ``F.interpolate``
    directly at import time (``from torch.nn.functional import
    interpolate``) as well as those that look it up at call time.
    """
    F.interpolate = _patched_interpolate  # type: ignore[assignment]
    torch.nn.functional.interpolate = _patched_interpolate  # type: ignore[assignment]


def unpatch_interpolate() -> None:
    """Restore the original ``F.interpolate`` after trace/convert."""
    F.interpolate = _ORIGINAL_INTERPOLATE  # type: ignore[assignment]
    torch.nn.functional.interpolate = _ORIGINAL_INTERPOLATE  # type: ignore[assignment]


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
    use_refiner: bool = True,
    static_shape: bool = False,
    save_traced_path: Path | None = None,
    from_traced_path: Path | None = None,
) -> int:
    """Load teacher, apply the SDPA patch, trace, and convert to CoreML.

    Exit codes mirror typical CLI conventions: ``0`` on a viable
    ``.mlpackage``, ``1`` on conversion failure (expected outcome that
    invalidates the Phase 2 hypothesis), ``2`` on setup failure (missing
    dependency, checkpoint, etc.).

    The trace and convert steps can be run on separate hosts when the
    tracing host cannot execute the final ``.mlpackage`` save. coremltools'
    native BlobWriter (``libmilstoragepython``) is macOS-only, so Windows
    can trace + convert but not save. Use ``save_traced_path`` on the
    tracing host to emit a torchscript ``.pt``, then run with
    ``from_traced_path`` on a macOS host to finish the conversion.
    """
    try:
        import coremltools as ct
    except ImportError as exc:
        print(f"[hiera_export] coremltools not installed: {exc}", file=sys.stderr)
        return 2

    if from_traced_path is not None:
        # Convert-only path: skip teacher load + trace, just rehydrate the
        # pre-traced torchscript module and run ct.convert + save. This
        # flow is intended for the macOS host in a split Windows-trace /
        # Mac-save pipeline; the traced graph already encodes all the
        # patches (attention, reroll, interpolate) because they were
        # applied on the tracing host before ``torch.jit.trace``.
        try:
            traced = torch.jit.load(str(from_traced_path), map_location="cpu")
        except Exception as exc:
            print(
                f"[hiera_export] torch.jit.load failed: {exc}", file=sys.stderr
            )
            return 2
        print(f"[hiera_export] loaded traced module from {from_traced_path}")
    else:
        from .teacher import load_teacher

        try:
            teacher = load_teacher(
                checkpoint_path=checkpoint_path,
                img_size=img_size,
                repo_path=repo_path,
                device="cpu",
                use_refiner=use_refiner,
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

        # Reroll.forward re-reads ``N = x.shape[1]`` on every loop iteration and
        # on Windows that read resolves to numpy.intc during trace, which makes
        # the subsequent ``N // math.prod(strides)`` emit a MIL floor_divide op
        # with an unsupported dtype. The patch forces Python-int arithmetic.
        reroll_patched = patch_hiera_reroll()
        if reroll_patched == 0:
            print(
                "[hiera_export] Reroll not found; Hiera layout may have changed "
                "upstream. Spike aborted.",
                file=sys.stderr,
            )
            return 2
        print(f"[hiera_export] patched {reroll_patched} Reroll class(es)")

        # Convert every ``F.interpolate(..., size=...)`` call into
        # ``scale_factor=(sh, sw)`` form so coremltools' upsample lowering pass
        # sees static scale constants instead of shape-op-derived target sizes.
        patch_interpolate_for_trace()
        print("[hiera_export] patched F.interpolate for static scale_factor")

        wrapper = _TracingWrapper(teacher.module).eval()
        example = torch.zeros(1, 4, img_size, img_size)

        try:
            traced = torch.jit.trace(wrapper, example, strict=False)
        except Exception as exc:
            print(f"[hiera_export] torch.jit.trace failed: {exc}", file=sys.stderr)
            return 1

        if save_traced_path is not None:
            # Trace-only path: persist the torchscript to disk and exit. The
            # Mac side of the split pipeline will pick it up with
            # ``--from-traced``. Saving here keeps the memory-heavy trace on
            # the 32 GB host while the .mlpackage save (macOS-only because of
            # BlobWriter) happens on the 16 GB Mac.
            save_traced_path.parent.mkdir(parents=True, exist_ok=True)
            torch.jit.save(traced, str(save_traced_path))
            print(f"[hiera_export] saved traced module to {save_traced_path}")
            return 0

    if static_shape:
        # Fixed shape: ANE refuses models with enumerated/flexible inputs.
        # The production path would export one .mlpackage per bridge
        # resolution so the compiler can commit to a single NE schedule.
        shape_arg: Any = (1, 4, img_size, img_size)
    else:
        shape_arg = ct.EnumeratedShapes(
            shapes=[
                (1, 4, 512, 512),
                (1, 4, 768, 768),
                (1, 4, 1024, 1024),
            ],
            default=(1, 4, img_size, img_size),
        )
    try:
        mlmodel = ct.convert(
            traced,
            inputs=[
                ct.TensorType(
                    name="input_rgba",
                    shape=shape_arg,
                    dtype=float,
                )
            ],
            compute_units=ct.ComputeUnit.CPU_AND_NE,
            compute_precision=ct.precision.FLOAT16,
            convert_to="mlprogram",
            minimum_deployment_target=ct.target.macOS15,
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
    parser.add_argument(
        "--no-refiner",
        action="store_true",
        help="Disable the GreenFormer refiner; isolates whether the refiner's "
        "upsample op blocks CoreML conversion.",
    )
    parser.add_argument(
        "--static-shape",
        action="store_true",
        help="Emit a single fixed input shape instead of EnumeratedShapes. "
        "Apple Neural Engine refuses to deploy models whose input uses "
        "enumerated/flexible shapes; a static export is required to see "
        "whether ANE residency is achievable at all.",
    )
    parser.add_argument(
        "--save-traced",
        default=None,
        help="Trace-only mode: run the teacher trace with the attention / "
        "reroll / interpolate patches applied and write the torchscript "
        "module to this path. Skips ct.convert and .mlpackage save. "
        "Intended for the tracing host in a split Windows-trace / "
        "Mac-save pipeline (Windows can trace at 1024 with 32 GB RAM "
        "where macOS with 16 GB OOMs; but only macOS can save the "
        "final .mlpackage because coremltools BlobWriter is macOS-only).",
    )
    parser.add_argument(
        "--from-traced",
        default=None,
        help="Convert-only mode: load a torchscript module previously "
        "produced by --save-traced and run ct.convert + save. Skips "
        "teacher load, patching, and tracing entirely; the patches must "
        "have been baked in on the tracing host.",
    )
    args = parser.parse_args(argv)

    return run_spike(
        checkpoint_path=Path(args.ckpt),
        output_path=Path(args.output),
        img_size=args.img_size,
        repo_path=Path(args.repo_path) if args.repo_path else None,
        use_refiner=not args.no_refiner,
        static_shape=args.static_shape,
        save_traced_path=Path(args.save_traced) if args.save_traced else None,
        from_traced_path=Path(args.from_traced) if args.from_traced else None,
    )


if __name__ == "__main__":
    raise SystemExit(_cli())
