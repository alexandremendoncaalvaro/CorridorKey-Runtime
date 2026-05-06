# TorchScript Exporter

Exports CorridorKey PyTorch checkpoints into dynamic TorchScript artifacts for
the Windows RTX TorchTRT path.

## When To Use

Use the dynamic exporter when a Windows RTX checkpoint or the PyTorch runtime
stack changes. The Windows RTX model packs consume one dynamic file per screen
color:

- `corridorkey_dynamic_green_fp16.ts`
- `corridorkey_dynamic_blue_fp16.ts`

The exported file is loaded by the C++ TorchTRT runtime path. Runtime
resolution is selected by the caller and is not encoded in the filename.

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

## Upload Target

The dynamic artifacts belong under the Hugging Face runtime model repo at:

```text
torchtrt/dynamic-green/corridorkey_dynamic_green_fp16.ts
torchtrt/dynamic-blue/corridorkey_dynamic_blue_fp16.ts
```

Regenerate `scripts/installer/distribution_manifest.json` after upload so the
installer records the artifact URL, SHA256, size, and readiness state from the
canonical repository.
