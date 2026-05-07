# Windows RTX Model Exporters

This directory owns the Windows RTX PyTorch-family export tools. The tools are
split by artifact type so the release flow does not confuse dynamic
TorchScript artifacts with true Torch-TensorRT engines.

## When To Use

Use `compile_dynamic_torchscript.py` when a Windows RTX checkpoint or the
PyTorch runtime stack changes and the selected artifact is the dynamic
TorchScript product path. The exporter writes one dynamic TorchScript file per
screen color:

- `corridorkey_dynamic_green_fp16.ts`
- `corridorkey_dynamic_blue_fp16.ts`

The exported file is loaded by the C++ LibTorch-backed RTX runtime path.
Runtime resolution is selected by the caller and is not encoded in the
filename.

Use `compile_dynamic_torchtrt.py` when producing a true dynamic
Torch-TensorRT artifact. The artifact is still one `.ts` file per screen
color, but it embeds the source positional embedding as TorchScript extra data
and expects the C++ runtime to pass a cached positional grid as the second
forward input.

`compile_dynamic_torchtrt.py` also owns diagnostic compile controls for the
dynamic engine path. `--torch-executed-op` keeps named ATen operators in
PyTorch inside the serialized hybrid graph, `--use-fp32-acc` requests FP32
accumulation for eligible matmul layers, and `--dryrun` asks Torch-TensorRT to
report partitioning without saving an artifact. The pinned Torch-TensorRT 2.8
Dynamo compiler reports module fallback as unimplemented, so
`--torch-executed-module` is rejected by this toolchain.

Validate every promoted artifact from C++ at every runtime resolution the
product contract exposes. Green FP16 has validated through `2048` on the local
RTX 3080. Blue FP16 validates at `512` and `1024` but returns NaN at higher
runtime resolutions. Blue FP32 validates through `1536` on the local RTX 3080;
do not publish it under the blue product filename until the exposed resolution
contract is matched by a finite C++ runner baseline.

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

```powershell
uv run python compile_dynamic_torchtrt.py `
  --repo-path C:\Dev\CorridorKey-Engine `
  --checkpoint C:\Dev\CorridorKey-Runtime\models\CorridorKey.pth `
  --output C:\Dev\CorridorKey-Runtime\models\corridorkey_dynamic_green_fp16.ts `
  --precision fp16 `
  --min-resolution 512 `
  --opt-resolution 1024 `
  --max-resolution 2048
```

Each exporter writes dynamic `.ts` files into the selected output directory.
Validation should load the same file from C++ at multiple runtime resolutions
before it is promoted into the model pack.

## Upload Target

The dynamic artifacts belong under the Hugging Face runtime model repo at:

```text
torchtrt/dynamic-green/corridorkey_dynamic_green_fp16.ts
torchtrt/dynamic-blue/corridorkey_dynamic_blue_fp16.ts
```

Regenerate `scripts/installer/distribution_manifest.json` after upload so the
installer records the artifact URL, SHA256, size, and readiness state from the
canonical repository.
