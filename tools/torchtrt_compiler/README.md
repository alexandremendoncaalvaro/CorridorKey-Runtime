# Windows RTX Model Exporters

This directory owns the Windows RTX PyTorch-family export tools. The tools are
split by artifact type so the release flow does not confuse dynamic
TorchScript fallback artifacts with true Torch-TensorRT engines.

## When To Use

Use `compile_dynamic_torchscript.py` when a Windows RTX checkpoint or the
PyTorch runtime stack changes and the selected artifact is the dynamic
TorchScript fallback. The Windows RTX model packs currently consume one dynamic
TorchScript file per screen color:

- `corridorkey_dynamic_green_fp16.ts`
- `corridorkey_dynamic_blue_fp16.ts`

The exported file is loaded by the C++ LibTorch-backed RTX runtime path.
Runtime resolution is selected by the caller and is not encoded in the
filename.

Use `compile_torchtrt.py` only for fixed-resolution diagnostic
Torch-TensorRT engines. Those artifacts contain serialized TensorRT engine
markers and carry a trailing resolution token in the filename.

## Setup

```powershell
cd tools/torchtrt_compiler
uv sync
```

The Python package pins must match the runtime DLL stack staged into
`vendor/torchtrt-windows/` by the Windows pipeline.

## Run

```powershell
uv run python compile_dynamic_torchscript.py `
  --repo-path C:\Dev\CorridorKey-Engine `
  --checkpoint C:\Dev\CorridorKey-Runtime\models\CorridorKey.pth `
  --output C:\Dev\CorridorKey-Runtime\models\corridorkey_dynamic_green_fp16.ts `
  --precision fp16
```

The exporter writes dynamic `.ts` files into the selected output directory.
Validation should load the same file from C++ at multiple runtime resolutions
before it is promoted into the model pack.

The dynamic exporter does not produce a TensorRT engine. A true dynamic
Torch-TensorRT candidate must be exported through `torch.export` with explicit
shape constraints, compiled by `torch_tensorrt.dynamo.compile`, loaded in C++,
and benchmarked before it can replace this fallback.

## Upload Target

The dynamic artifacts belong under the Hugging Face runtime model repo at:

```text
torchtrt/dynamic-green/corridorkey_dynamic_green_fp16.ts
torchtrt/dynamic-blue/corridorkey_dynamic_blue_fp16.ts
```

Regenerate `scripts/installer/distribution_manifest.json` after upload so the
installer records the artifact URL, SHA256, size, and readiness state from the
canonical repository.
