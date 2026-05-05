# Core ML Student Distillation

This tool distills the CorridorKey GreenFormer matting teacher (Hiera-based vision transformer) into a compact, Apple-Neural-Engine-friendly student network, then exports the trained student to Core ML MLProgram FP16 for deployment on macOS.

## Why distillation instead of direct Core ML export

The GreenFormer teacher uses a Hiera backbone whose window attention produces 5D tensors that coremltools 8.x refuses to convert. Both `torch.jit.trace` and `torch.export` hit the same `TypeError: only 0-dimensional arrays can be converted to Python scalars` inside the MIL frontend when it encounters `aten::Int` on a non-scalar tensor. Apple's own ANE-transformer reference stack (`apple/ml-ane-transformers`, `apple/ml-vision-transformers-ane`) does not cover the Hiera topology. Even when community forks of SAM2 (which also uses a Hiera encoder) succeed in producing a Core ML package, measurements on an M3 MacBook Pro show the package is not ANE-resident in practice.

Running the GreenFormer on MLX-on-GPU inside DaVinci Resolve was measured at 5.5 seconds per frame on Mac Mini M4 with sigma ~5 seconds, a 20x slowdown over the synthetic baseline because DaVinci's Metal workload time-slices ahead of long MLX command buffers.

A distilled student with an all-convolution body plus a small recurrent decoder (MobileNetV3-Large + LR-ASPP + ConvGRU) solves both problems at once:

- Every op in the graph is ANE-native. The Apple M4 ANE executes 1x1 and 3x3 convolutions at roughly 3x the throughput of matmul, and ConvGRU is a small op on the scale of the ANE's 32 MB SRAM working set.
- Core ML export is a solved, published path. The Robust Video Matting project ships an official `coreml` branch with a working export script plus pre-built FP16 and INT8 `.mlmodel` artifacts at 1280x720 and 1920x1080.
- The teacher supplies dense per-pixel supervision on unlabeled video, so training data requirements collapse relative to a from-scratch matting model.

## Directory layout

```
tools/coreml_student/
  pyproject.toml           uv-managed dependency pin; torch==2.5.1 for coremltools compatibility
  README.md                this file
  src/coreml_student/      importable Python package
    teacher.py             load and wrap the GreenFormer .pth checkpoint
    student.py             RVM-class MattingNetwork student with stateful ConvGRU
    data.py                video decoding, teacher pseudo-label generation, augmentation
    loss.py                L1 + pyramid Laplacian + temporal consistency, per the RVM recipe
    train.py               training entry point
    export.py              Core ML MLProgram export of the trained student
    validate_parity.py     quality comparison of student output vs teacher on a holdout
  tests/                   pytest unit tests
  configs/                 YAML training configs
```

## Setup

```
cd tools/coreml_student
uv sync
```

`uv` installs into a local `.venv`. Activate it or prefix commands with `uv run`.

## Teacher and student checkpoints

The teacher checkpoint (`models/CorridorKey.pth` or `models/CorridorKey_v1.0.pth`) is the upstream training weight published at [`nikopueringer/CorridorKey_v1.0`](https://huggingface.co/nikopueringer/CorridorKey_v1.0). Place it under `models/` before invoking `teacher.py`. Trained student checkpoints stay local to the experiment workspace.

## Workflow

1. **Validate teacher load** — `uv run python -m coreml_student.teacher --probe` loads the `.pth`, runs a single forward pass at 512x512, prints output tensor shapes.
2. **Generate training dataset** — `uv run python -m coreml_student.data --make-pseudo-labels` runs the teacher over an input video folder and caches alpha and foreground predictions to disk as the distillation ground truth.
3. **Train the student** — `uv run python -m coreml_student.train --config configs/default.yaml`.
4. **Validate parity** — `uv run python -m coreml_student.validate_parity --ckpt <student_ckpt>` prints SAD, MSE, Grad, Conn, and dtSSD against the teacher over a held-out clip.
5. **Export Core ML** — `uv run python -m coreml_student.export --ckpt <student_ckpt> --resolutions 720 1080` writes one `.mlpackage` per resolution targeting `CPU_AND_NE`.

Each step is an independent entry point so the pipeline can be resumed after a crash and intermediate artifacts can be audited.

## Design decisions

### Why the student's hidden state is a Core ML stateful buffer

Core ML 8 (iOS 18 / macOS 15) adds native support for per-session stateful buffers registered via `register_buffer` plus `coremltools.StateType` on export. The ConvGRU hidden state stays on the ANE across consecutive frame calls, with no host round-trip. The runtime side in `src/core/coreml_session.*` calls `MLModel.prediction(from:usingState:)`.

### Why we target `MLComputeUnits.cpuAndNeuralEngine` and not `.all`

Apple's own documentation for `cpuAndNeuralEngine` states that it is the right choice "when an app uses the GPU for other computation." DaVinci Resolve uses the Metal GPU continuously for scopes, previews, and its own Magic Mask. Scheduling the matting inference onto `.all` introduces compute-unit switching overhead and contends with the host's Metal traffic. Community measurements on RVM Core ML confirm that `.cpuAndGPU` sometimes beats `.all` on Apple Silicon, and that `.all` on M1 Max intermittently trips Neural Engine errors. `.cpuAndNeuralEngine` gives predictable ANE-only execution.

### Why FP16 first and quantization later

FP16 is the ANE's native precision. Every ANE weight dequantizes to FP16 before compute, so INT8 weights save disk and memory but not latency. Mixed-bit palettization and activation quantization are packaged as a follow-up pass only after FP16 quality is validated.

## Scope

This tool is the distillation pipeline only. Runtime consumption of the exported `.mlpackage` lives in `src/core/coreml_session.*` and is wired into the runtime via the `Backend::CoreML` branch in `src/core/inference_session.cpp`. Training this student and shipping a bundle is a multi-slice effort tracked as `0.7.5-5a` through `0.7.5-6` in the optimization track document.
