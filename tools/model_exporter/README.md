# Model Exporter Tool

This is an isolated Python tool designed to convert the original PyTorch model (`.pth`) from the [CorridorKey Python repository](https://github.com/nikopueringer/CorridorKey) into an optimized `.onnx` model suitable for the C++ Runtime.

By keeping this in a separate `tools/` directory, the main C++ runtime remains completely decoupled from Python and its dependencies.

## Prerequisites

- [uv](https://docs.astral.sh/uv/) (Python package manager)
- The original `CorridorKey` repository cloned alongside this one.
- The original PyTorch weights downloaded (`CorridorKey_v1.0.pth`).

## Usage

1. Navigate to this tool's directory:
   ```bash
   cd tools/model_exporter
   ```

2. Run the export script via `uv` (it will automatically install dependencies in an isolated `.venv`):
   ```bash
   # Assuming the original Python repo is cloned at ../../../CorridorKey
   # and the weights are inside it at CorridorKeyModule/checkpoints/CorridorKey.pth
   uv run python export_onnx.py \
       --ckpt ../../../CorridorKey/CorridorKeyModule/checkpoints/CorridorKey.pth \
       --out ../../models/corridorkey_fp32.onnx \
       --repo-path ../../../CorridorKey
   ```

## Arguments

- `--ckpt`: Path to the input PyTorch weights file (`.pth`).
- `--out`: Path where the resulting ONNX file should be saved.
- `--repo-path`: Path to the root of the original Python repository. We need this to import the PyTorch architecture definitions dynamically without duplicating their code here. Defaults to `../../../CorridorKey` (which assumes both repositories share the same parent directory).

## INT8 Decision Program

The Windows RTX product path stays on ONNX Runtime + TensorRT RTX EP with FP16
as the official baseline. GPU INT8 is evaluated separately through the decision
program in `int8_decision_program.py`.

That runner compares:

- CorridorKey CLI `tensorrt + fp16` as the official baseline
- CorridorKey CLI `cpu + int8` as the fallback baseline
- Torch-TensorRT GPU INT8 as the candidate path

The decision report enforces the current product gate:

- GPU INT8 must beat the official FP16 path by at least `1.8x` steady-state
- the visual corpus must cover `hair`, `fine_edge`, `motion_blur`,
  `transparency`, and `spill`
- numeric drift is recorded, but manual visual review is still required before
  promoting the candidate into product code

Template corpus manifest:

- `int8_visual_corpus_template.json`

Example:

```bash
uv run python int8_decision_program.py \
    --corridorkey-cli ../../build/release/src/cli/corridorkey.exe \
    --models-dir ../../models \
    --checkpoint ../../../CorridorKey/CorridorKeyModule/checkpoints/CorridorKey.pth \
    --repo-path ../../../CorridorKey \
    --corpus-manifest ./int8_visual_corpus_template.json \
    --output ../../dist/int8_decision_report.json
```

`Torch-TensorRT` is intentionally not wired into the runtime shipping path yet.
If it is missing from this Python environment, the decision program reports
that explicitly instead of silently changing product behavior.
