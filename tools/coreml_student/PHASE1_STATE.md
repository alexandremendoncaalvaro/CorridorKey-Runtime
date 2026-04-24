# Phase 1 Spike State — Direct Hiera to Core ML

This document is the resume point for the Hiera-to-Core ML feasibility
probe. It records the validated decision, the constraint that blocked
full productionization, and the exact commands to pick up where this
spike stopped without re-reading the full history.

## Scope

The probe asked one question: can the GreenFormer teacher export to a
Core ML `.mlpackage` that lands on the Apple Neural Engine with
production quality, without paying for a full student distillation run.
The answer from the 512x512 static export is yes.

## Decision

`GO` on the direct export path, conditional on running the final
compile on a Mac with at least 32 GB of RAM.

The measured signals at 512x512 static shape, `ComputeUnit.CPU_AND_NE`,
FP16, `macOS15` target, on a 16 GB Mac Mini M4:

| Check | Target | Measured | Margin |
| --- | --- | --- | --- |
| `.mlpackage` loads and predicts | pass | pass | |
| Alpha MAE vs PyTorch teacher | < 5e-3 | 6e-5 | 80x below |
| Max per-pixel alpha error | informational | 9.5e-4 | |
| Foreground MAE vs teacher | informational | 1.3e-3 | |
| Mean latency on CPU_AND_NE | < 300 ms | 156 ms | about 2x below |
| Mean latency vs PyTorch CPU teacher | >= 2x speedup | 6.95x | |
| Enumerated-shape latency (same model) | -- | 1695 ms | 10x collapse vs static |

The 10x collapse between the enumerated-shape and static-shape exports
of the same operator graph is the fingerprint of ANE placement. Core ML
refuses to schedule flexible-input models on the Neural Engine and
falls back to CPU/GPU; the static variant drops to 156 ms because the
scheduler commits to one NE plan. Xcode `Performance` on the static
`.mlpackage` reports predict median 143.81 ms with a ~54 s cold load,
consistent with ANE firmware compile.

## Constraint that blocks 1024 on 16 GB hardware

`ANECompilerService` (the macOS XPC that plans the Neural Engine
schedule) does not complete the compile for a `1024x1024` input on a
16 GB machine. Observed behavior on Mac Mini M4 16 GB: the service
runs at 85-96 percent CPU for 3+ hours with pages-free dropping to
about 120 MB and swap I/O in the tens of gigabytes, without emitting
a compiled model. The 512 compile completes in ~54 seconds on the
same hardware.

The `torch.jit.trace` step at 1024 also does not fit in 16 GB. This
is a distinct bottleneck from the compile: tracing holds activations
for every layer simultaneously, while the compiler holds the full
schedule-planning state. Both steps need to be performed on a host
with enough memory.

`ANECompilerService` is macOS-only. There is no Windows or Linux
substitute; the compile for any deployment `.mlpackage` must run on
macOS.

## Artifacts produced

- `build/coreml_spike/greenformer_hiera.mlpackage` (138 MB, enumerated
  shapes, measurement comparison only)
- `build/coreml_spike/greenformer_hiera_static.mlpackage` (138 MB,
  static 512, the primary artifact; the Xcode `.mlperf` report sits
  next to it and confirms ~54 s cold ANE compile)
- `build/coreml_spike/traced_1024.pt` (311 MB, the successful
  TorchScript trace of the 1024 graph; produced on a 32 GB host via
  `--save-traced` and transferred to the Mac. Ready as input for the
  1024 `ct.convert` step once a 32 GB Mac is available. `build/` is
  gitignored, so this file is not distributed; regenerate with the
  command below)

## How to resume on a Mac with >=32 GB RAM

Assuming the `tools/coreml_student` venv is set up (`uv sync` inside
the tool directory) and the teacher checkpoint sits at
`models/CorridorKey.pth`, the 1024 path is:

```
# 1. Produce the TorchScript trace (memory-heavy step).
uv run python -m coreml_student.hiera_export \
    --ckpt models/CorridorKey.pth \
    --repo-path <path-to-CorridorKey-teacher-repo> \
    --img-size 1024 \
    --static-shape \
    --save-traced build/coreml_spike/traced_1024.pt

# 2. Convert TorchScript to .mlpackage (compile-heavy step; needs
#    macOS with ANECompilerService headroom).
uv run python -m coreml_student.hiera_export \
    --img-size 1024 \
    --static-shape \
    --from-traced build/coreml_spike/traced_1024.pt \
    --output build/coreml_spike/greenformer_hiera_static_1024.mlpackage

# 3. Validate on the same host.
uv run python -m coreml_student.hiera_validate \
    --mlpackage build/coreml_spike/greenformer_hiera_static_1024.mlpackage \
    --ckpt models/CorridorKey.pth \
    --repo-path <path-to-CorridorKey-teacher-repo> \
    --img-size 1024 \
    --trials 5 \
    --compute cpu_and_ne
```

Step 1 can be split across hosts: `--save-traced` on a 32 GB Windows
or Linux machine, then SCP the `.pt` file to the macOS host for step
2. The TorchScript is portable; the final `.mlpackage` save requires
`coremltools.libmilstoragepython` which is macOS-only.

## Required Instruments check before Phase 2A

Neither `coremltools` nor `MLModel.get_compiled_model_path()` expose
the ANE residency fraction programmatically. The Xcode `Performance`
tab on the generated `.mlpackage` reports per-compute-unit usage;
residency >= 80 percent on the Neural Engine row is the formal
Phase 1 go criterion. The 10x latency collapse documented above is
strong circumstantial evidence, but not the literal measurement.

If residency lands between 50 and 80 percent, Phase 2A proceeds and
the diagnostic panel lists which ops fell to CPU or GPU; those
become the surgery targets. Below 50 percent reopens the decision.

## Operational changes baked into `hiera_export.py`

The spike required three patches to the live PyTorch graph before
tracing. Each patch is documented inline; this section exists so a
later reader can tell which patches exist and why.

1. `MaskUnitAttention.forward` rewrite. The timm reference layout
   `(B, tpw, num_windows, 3, heads, head_dim)` is rank 6 and Core ML
   refuses to export tensors of rank > 5. The patch splits QKV along
   the channel axis first, reshapes each projection to rank 5, folds
   `num_windows` into the batch axis for a canonical 4-D SDPA call,
   and unfolds afterwards. Memory layout and numerics are preserved.

2. `F.interpolate(..., size=...)` rewrite. `coremltools`'
   `torch_upsample_to_core_upsample` MIL pass refuses to lower
   `upsample_bilinear2d` when the target size is a tuple of shape
   ops. GreenFormer's decoder neck uses `size=c1.shape[2:]` for the
   three feature-map upsamples and `size=input_size` for alpha and
   foreground logit upsampling. The patch computes the integer scale
   from the live tensor shapes at trace time and forwards
   `scale_factor=(sh, sw)` so the MIL pass sees constants it can
   lower.

3. `Reroll.forward` and `MaskUnitAttention.forward` shape-value
   coercion to Python ints. On Windows the tracer resolves
   `x.shape[i]` to a `numpy.intc` scalar; downstream `//` emits a
   MIL `floor_divide` op whose dtype `coremltools` refuses. Wrapping
   every shape read in `int(...)` forces pure-Python arithmetic so
   the traced graph stores the counts as constants. The behavior is
   platform-independent; on macOS the underlying type was already
   Python `int` and the patch is a no-op there.

All three patches are applied at module load time and reverted by
tracing's own immutability; no PyTorch runtime code is affected.

## Scope of this document

This file is the resume state for the direct-export spike. The
companion tool README at `tools/coreml_student/README.md` documents
the full student distillation pipeline, which remains the fallback
if the direct export is ever reopened and fails.
