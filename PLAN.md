# CorridorKey OFX Plugin - Development Plan

## Business Context

The OFX plugin is the primary delivery surface for CorridorKey. The first
functional macOS Apple Silicon build is live (resolve-v0.1.4-mac), with basic
keying, despill, despeckle, and refiner controls. The immediate goal is to
close the quality gap with the community Python app (EZ-CorridorKey) and
establish the plugin as a production-ready tool inside DaVinci Resolve.

## Current State

**Shipped (resolve-v0.1.4-mac):**
- Functional OFX plugin for DaVinci Resolve on macOS Apple Silicon
- MLX backend with CPU/CoreML fallback
- Notarized DMG with PKG installer (system-wide / per-user)
- Parameters: Despill Strength, Auto Despeckle, Refiner Scale, Input Is Linear
- Correct color space pipeline (sRGB in, premultiplied sRGB out)

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
- [ ] **1.5.2 Color Space Auto-Detection** -- query Resolve clip properties
  to detect whether input is linear or sRGB instead of relying on a manual
  checkbox. Addresses the reported "EXR ingested as linear even if sRGB
  selected" issue.

## Phase 2 - Resolution and Quality Control

Goal: proper 4K support and user control over quality/speed tradeoff.

- [x] **2.1 Quality Mode Parameter** -- choice parameter exposing inference
  resolution: Auto (default), Preview (512), Standard (768), High (1024).
  Auto selects based on input resolution. Changing quality recreates the
  engine with the appropriate MLX bridge or ONNX model. The MLX safetensors
  pack supports dynamic bridge compilation up to 2048px.
- [x] **2.2 Enable Tiling in OFX** -- pass `enable_tiling = true` and
  appropriate `tile_padding` to InferenceParams. Critical for 4K+ inputs
  where the model resolution is smaller than the frame. Tiling engine
  already exists (`run_tiled`), just needs to be wired into the plugin.
- [ ] **2.3 Preview Scale** -- optional downscale factor (1/2, 1/4, 1/8)
  applied before inference for fast interactive scrubbing. Upscales result
  to output resolution. Independent of Quality Mode. Enables fast timeline
  navigation even at high quality settings.

## Phase 3 - Output Control and Workflow

Goal: give compositors the control they expect from a professional keyer.

- [ ] **3.1 Output Mode Selector** -- choice parameter: Processed (default),
  Matte Only, Foreground Only, Source + Matte.
- [ ] **3.2 Alpha Edge Controls** -- Erode/Dilate (-10 to +10 px), Edge
  Softness (0-5 px), Black/White Point (0-1 each). Post-inference alpha
  manipulation, no re-run needed.
- [ ] **3.3 Color Correction** -- Brightness (0.5-2.0) and Saturation (0-2.0)
  controls applied after despill in linear space.

## Phase 4 - Platform Parity

- [ ] **4.1 Windows OFX Validation** -- validate at the same quality level
  with TensorRT, CUDA, and DirectML backends.
- [ ] **4.2 Cross-Platform Feature Sync** -- ensure all features work
  identically on Windows with unified parameter set.

## Phase 5 - Advanced Features

Longer-term items informed by EZ-CorridorKey capabilities and community
feedback.

- [ ] **5.1 Blue Screen Support** -- channel-swap approach (swap B and G
  channels before inference, swap back after). Requires adapting despill
  and rough matte generation to work on the blue channel. No model
  retraining needed.
- [ ] **5.2 Multiple Alpha Hint Strategies** -- evaluate BiRefNet, SAM2,
  MatAnyone2 as alternative hint generators for non-person subjects.
  Also enables arbitrary background colors without retraining.
- [ ] **5.3 Tiled Inference Enhancements** -- advanced tiling with adaptive
  tile sizes, resolution-aware overlap, and quality metrics.
- [ ] **5.4 Temporal Consistency** -- frame-to-frame matte consistency to
  reduce flickering in video sequences.

## Reference

- **EZ-CorridorKey:** github.com/edenaion/EZ-CorridorKey (Python standalone,
  same AI model, community-driven)
- **Archived plans:** docs/archive/PLAN_product_direction.md,
  docs/archive/PLAN_OFX_MAC_v1.md
