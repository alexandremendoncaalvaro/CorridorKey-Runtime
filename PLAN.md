# CorridorKey OFX Plugin - Development Plan

## Business Context

The OFX plugin is the primary delivery surface for CorridorKey. The first
functional macOS Apple Silicon build is live (resolve-v0.1.4-mac), with basic
keying, despill, despeckle, and refiner controls. The immediate goal is to
close the quality gap with the community Python app (EZ-CorridorKey) and
establish the plugin as a production-ready tool inside DaVinci Resolve.

## Current State

**Shipped (v0.2.0-mac):**
- Functional OFX plugin for DaVinci Resolve on macOS Apple Silicon
- MLX backend with CPU/CoreML fallback
- Notarized DMG with PKG installer (system-wide / per-user)
- Smart resolution selection (512/768/1024 Auto based on input size)
- Lanczos4 output upscaling for maximum edge detail
- Source detail restoration with soft green falloff
- Alpha Hint external matte input
- Output Mode selector (Processed, Matte Only, Foreground Only, Source+Matte)
- Alpha Edge Controls (Erode/Dilate, Softness, Black/White Point)
- Color Correction (Brightness, Saturation)
- Correct color space pipeline (sRGB in, linear premultiplied out, sRGB conversion in write)

## Phase 1 - Quality Parity with EZ-CorridorKey

Goal: match or exceed the visual quality of the Python standalone app.

- [x] **1.1 Source Detail Restoration** -- blend original source pixels back
  into opaque interior regions using chamfer distance transform. Avoids green
  spill contamination with per-pixel check. Smooth blend ramp at edges.
  (`src/post_process/restore_source.cpp`)
- [x] **1.2 Despeckle Size Threshold** -- expose existing engine parameter as
  integer slider (50-2000px, default 400) in OFX panel. Replaces binary toggle.
- [x] **1.3 Refiner Scale Range** -- extended from [0.5, 2.0] to [0.0, 3.0].
  Setting 0.0 disables the CNN edge refiner entirely.
- [x] **1.4 Despill Algorithm** -- verified: already matches EZ-CorridorKey
  (green limit = (R+B)/2, redistribute spill to R and B).

## Phase 1.5 - Core Workflow (community-driven, high priority)

Features requested by the community and critical for real-world compositing.

- [x] **1.5.1 Alpha Hint Input Clip** -- add a second optional OFX input clip
  ("Alpha Hint") so users can feed an external matte from another Fusion node
  (keyer, roto, painted mask) instead of the auto-generated rough matte.
  If not connected, falls back to `generate_rough_matte` as today.
- [ ] **1.5.2 Color Space Auto-Detection** -- OFX 1.5 colour management
  extensions exist but DaVinci Resolve does not implement them. Possible
  heuristic: detect linear footage by checking if float pixel values
  exceed 1.0. For now, the "Input Is Linear" checkbox remains the
  reliable manual fallback.

## Phase 2 - Resolution and Quality Control

Goal: proper 4K support and user control over quality/speed tradeoff.

Research finding (2026-03-15): All reference implementations (original
CorridorKey, EZ-CorridorKey, CorridorKey-Engine) process at 2048x2048 on
CUDA and 1024x1024 on MPS/Apple Silicon. No repo does native-resolution
inference. All downscale to model resolution, process, then Lanczos4
upscale. Tiling is only applied to the CNN refiner (never the backbone),
with 512px tiles and 128px overlap. Our 1024 bridge already matches what
EZ-CorridorKey achieves on Apple Silicon.

- [x] **2.1 Quality Mode Parameter** -- choice parameter exposing inference
  resolution: Auto (default), Preview (512), Standard (768), High (1024).
  Auto selects based on input resolution. Changing quality recreates the
  engine with the appropriate MLX bridge or ONNX model. The MLX safetensors
  pack supports dynamic bridge compilation up to 2048px.
- [x] **2.2 Smart Resolution Selection** -- Auto mode selects the optimal
  MLX bridge based on input size (<=1000px: 512, <=2000px: 768, >2000px:
  1024). Single-pass inference with no tiling overhead. Engine recreated
  only when target resolution changes.
- [x] **2.3 Lanczos4 Output Upscaling** -- replaced bilinear upscaling of
  inference results with separable Lanczos4 interpolation when resizing
  back to source resolution. All reference repos use Lanczos4 for this
  step. Bilinear retained for downscaling in `fit_pad` where it is
  appropriate. (`src/post_process/color_utils.cpp`)

## Phase 3 - Output Control and Workflow

Goal: give compositors the control they expect from a professional keyer.

- [x] **3.1 Output Mode Selector** -- choice parameter: Processed (default),
  Matte Only, Foreground Only, Source + Matte.
- [x] **3.2 Alpha Edge Controls** -- Erode/Dilate (-10 to +10 px), Edge
  Softness (0-5 px), Black/White Point (0-1 each). Post-inference alpha
  manipulation, no re-run needed.
- [x] **3.3 Color Correction** -- Brightness (0.5-2.0) and Saturation (0-2.0)
  controls applied to foreground in linear space after inference. Uses
  Rec. 709 luminance weights for saturation. (`src/plugins/ofx/ofx_render.cpp`)
- [x] **3.4 Color Pipeline Audit** -- fixed double-gamma bug in all output
  modes when alpha edge controls or color correction were active. The
  foreground (sRGB from model) was being premultiplied without linearization,
  then `write_output_image` applied `to_srgb()` again, causing gray banding
  at transparency edges. Now all output paths convert FG to linear before
  premultiplication, matching the reference repos exactly.

## Phase 4 - Platform Parity (NVIDIA RTX)

Goal: ensure the Windows version matches macOS quality and performance.

- [x] **4.1 TensorRT-RTX Integration** -- fully implemented with FP16 support.
- [x] **4.2 FP16 Input/Output Conversion** -- hardware-accelerated half-precision
  handling for TensorRT engines.
- [x] **4.3 MSVC Compilation & Vendor ORT** -- stabilized build pipeline with
  Visual Studio 2022 and managed ONNX Runtime dependencies.
- [x] **4.4 Portable Installer** -- relative path installation script for
  professional distribution.

## Phase 5 - Universal Windows AI (AMD, Intel, Qualcomm)

Goal: target the March 2026 Windows AI stack for maximum compatibility.

- [ ] **5.1 Windows AI Platform (WinML) Backend** -- migrate from manual
  backend selection to the Windows ML orchestrator. This allows the OS to
  automatically route to the NPU on Copilot+ PCs or the GPU on AMD/Intel.
- [ ] **5.2 NPU-First Strategy (QNN & OpenVINO 2026.0)** -- optimize models for
  the Snapdragon X2 Elite (Qualcomm QNN) and Intel Core Ultra (OpenVINO)
  NPUs to offload the GPU during video rendering.
- [ ] **5.3 DirectML Fallback (Legacy Compatibility)** -- maintain DirectML for
  pre-2024 AMD/NVIDIA hardware where NPUs are unavailable.
- [ ] **5.4 Blue Screen Support** -- channel-swap approach (swap B and G
  channels before inference, swap back after). Requires adapting despill
  and rough matte generation to work on the blue channel. No model
  retraining needed.

## Phase 6 - Performance & Workflow Refinement

- [ ] **6.1 GPU/NPU Zero-Copy Pipeline** -- implement direct memory link between
  the AI backend and DaVinci Resolve's GPU buffers to eliminate CPU staging.
- [ ] **6.2 INT8 Quantization** -- utilize 2026-era quantization tools (Olive)
  to achieve 2x speedup on NPUs with minimal quality loss.
- [ ] **6.3 Temporal Consistency (Video-Native)** -- frame-to-frame matte
  consistency to reduce flickering.
- [ ] **6.4 2048px Windows Engine** -- expand Windows support to match the
  2048px macOS bridge.

## Reference

- **Original CorridorKey:** github.com/nikopueringer/CorridorKey (creator's
  repo, GreenFormer model)
- **EZ-CorridorKey:** github.com/edenaion/EZ-CorridorKey (Python standalone,
  same AI model, community-driven)
- **CorridorKey-Engine:** github.com/99oblivius/CorridorKey-Engine (Python
  engine with optimizations, FlashAttention, deferred DMA)
- **Archived plans:** docs/archive/PLAN_product_direction.md,
  docs/archive/PLAN_OFX_MAC_v1.md
