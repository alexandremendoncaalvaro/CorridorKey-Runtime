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
