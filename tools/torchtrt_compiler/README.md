# Torch-TensorRT Compiler

Compiles `nikopueringer/CorridorKeyBlue_1.0` checkpoints into the
`.ts` engines the Windows RTX blue model pack consumes.

## When to use

After bumping the upstream blue checkpoint or the torch / torch-tensorrt
/ tensorrt pin, the four blue rungs the runtime catalog references must
be regenerated:

- `corridorkey_blue_torchtrt_fp16_512.ts`
- `corridorkey_blue_torchtrt_fp16_1024.ts`
- `corridorkey_blue_torchtrt_fp32_1536.ts`
- `corridorkey_blue_torchtrt_fp32_2048.ts`

The 2048 FP32 rung does not compile on cards with less than ~16 GB VRAM
(TensorRT builder peak is 5-10x the final engine size; the final engine
alone is ~825 MB). On RTX 3080 / 4070 / lower-tier cards, run this
script for the 512 / 1024 / 1536 rungs and stage the 2048 rung on a
cloud GPU with sufficient VRAM.

## Setup

```
cd tools/torchtrt_compiler
uv sync
```

`uv` materialises a venv pinned to `torch==2.8.0+cu128`,
`torch-tensorrt==2.8.0`, and `tensorrt-cu12==10.12.0.36`. These pins
must match the runtime DLL stack staged into `vendor/torchtrt-windows/`
by `scripts/windows.ps1 -Task prepare-torchtrt`. A version mismatch at
load time raises `Unknown builtin op: tensorrt::execute_engine` or
returns silent NaN inference.

## Run

```
uv run python compile_torchtrt.py
```

Defaults:
- Checkpoint downloaded from `nikopueringer/CorridorKeyBlue_1.0` into
  `models/CorridorKeyBlue_1.0.pth` if not already present.
- Compiles FP16 for 512 / 1024 and FP32 for 1536 / 2048.
- Writes outputs into `models/`.

To compile only a subset (e.g. skip 2048 on a 10 GB card):

```
uv run python compile_torchtrt.py --resolutions-fp32 1536
```

To force recompile when the file already exists:

```
uv run python compile_torchtrt.py --force
```

## Why blue uses .ts and not ONNX

The blue checkpoint's FP16 ONNX produces all-NaN inference output on
the TensorRT and CUDA execution providers. See
`docs/OPTIMIZATION_MEASUREMENTS.md` "Blue dedicated baselines" for the
full diagnostic record. Torch-TensorRT compiles the blue weights
directly into a TensorRT engine, bypassing the ONNX importer that fails
for blue.

## Why FP32 at 1536 / 2048

FP16 trace-time conversion of the blue weights NaNs out at 1536 and
2048. Forcing `enabled_precisions={torch.float32}` for those rungs
produces finite output at the cost of a larger engine on disk. Green
ladder is unaffected (green is FP16 across the full ladder via the
ONNX -> TensorRT-RTX EP path).

## Upload to HF after compile

The compiled `.ts` files belong under
`alexandrealvaro/CorridorKey/torchtrt/{fp16-blue,fp32-blue}/`. Upload
via the `hf` CLI (the same one that hosts the canonical model repo):

```
hf upload alexandrealvaro/CorridorKey \
    models/corridorkey_blue_torchtrt_fp16_512.ts \
    torchtrt/fp16-blue/corridorkey_blue_torchtrt_fp16_512.ts
```
