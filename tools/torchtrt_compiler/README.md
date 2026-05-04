# TorchScript Exporter

Exports CorridorKey PyTorch checkpoints into dynamic TorchScript artifacts for
the Windows RTX TorchScript path.

## When To Use

Use the dynamic exporter when the blue checkpoint or the PyTorch runtime stack
changes. The Windows RTX blue model pack consumes one file:

- `corridorkey_dynamic_blue_fp16.ts`

The green model stays on the optimized ONNX TensorRT RTX EP ladder. Dynamic
green exports are useful for comparison, but they are not the product default
because the measured ONNX green path remains faster.

## Setup

```powershell
cd tools/torchtrt_compiler
uv sync
```

The Python package pins must match the runtime DLL stack staged into
`vendor/torchtrt-windows/` by the Windows pipeline.

## Run

```powershell
uv run python compile_dynamic_torchscript.py --variant blue --precision fp16
```

The exporter writes dynamic `.ts` files into the selected output directory.
Validation should load the same file from C++ at multiple runtime resolutions
before it is promoted into the model pack.

## Upload Target

The dynamic blue artifact belongs under the Hugging Face runtime model repo at:

```text
torchtrt/dynamic-blue/corridorkey_dynamic_blue_fp16.ts
```

Regenerate `scripts/installer/distribution_manifest.json` after upload so the
installer records the artifact URL, SHA256, size, and readiness state from the
canonical repository.
