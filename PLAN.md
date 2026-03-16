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

**Known Issues:**
- Output quality is visibly lower than source (details lost, darker than expected)
- No way to restore original source detail in opaque regions
- Despeckle is binary (on/off), no size threshold control
- No separate matte/foreground output modes
- Windows OFX plugin exists but has not been validated at the same level

## Phase 1 - Quality Parity with EZ-CorridorKey

Goal: match or exceed the visual quality of the Python standalone app.

### 1.1 Source Detail Restoration

The single biggest quality improvement. The ML model output loses fine texture
in fully opaque interior regions. EZ-CorridorKey solves this by blending
original source pixels back where the matte is confidently opaque and far from
edges.

**Approach:**
- Compute distance from alpha edge (alpha < threshold) for each pixel
- Where alpha is near-opaque (> 0.92) AND distance from edge is significant
  (> 8px) AND pixel has no green spill: blend original source RGB
- Use smooth blend ramp to avoid visible transition artifacts
- Implement in `src/post_process/` as a reusable utility

### 1.2 Despeckle Size Threshold

Replace the binary on/off toggle with a pixel-area threshold parameter.

**Approach:**
- Add `kParamDespeckleSize` as integer parameter (range 50-2000, default 400)
- Connected component analysis on the alpha channel
- Remove components smaller than threshold
- Apply morphological dilation + gaussian blur for smooth cleanup
- Keep the existing bool as an enable/disable, add the size as a sub-parameter

### 1.3 Refiner Scale Range

Extend refiner scale to allow disabling the refiner entirely.

**Approach:**
- Change range from [0.5, 2.0] to [0.0, 3.0]
- 0.0 = refiner disabled (use raw model output)
- This matches EZ-CorridorKey behavior and gives users more control

### 1.4 Despill Algorithm Improvement

Current despill is functional but the green limit calculation could be more
nuanced.

**Approach:**
- Verify current despill matches the luminance-preserving approach
- Green limit = (R+B)/2, spillage = max(G - limit, 0)
- Redistribute spill equally to R and B channels
- Ensure strength parameter blends smoothly between original and despilled

## Phase 2 - Output Control and Workflow

Goal: give compositors the control they expect from a professional keyer.

### 2.1 Output Mode Selector

Allow users to choose what the plugin outputs.

**Options:**
- **Processed (default):** RGBA with matte applied (current behavior)
- **Matte Only:** Alpha channel as grayscale (for manual compositing)
- **Foreground Only:** RGB with green removed, no alpha applied
- **Source + Matte:** Original RGB with generated alpha (no despill)

**Approach:**
- Add `kParamOutputMode` as choice parameter in `ofx_actions.cpp`
- Branch in `ofx_render.cpp` after engine processing to write the selected
  output variant

### 2.2 Alpha Edge Controls

Fine-tune matte edges without re-running inference.

**Parameters:**
- **Erode/Dilate** (range -10 to +10 px): shrink or grow the matte
- **Edge Softness** (0.0 to 5.0 px): gaussian blur on matte edges only
- **Black/White Point** (0.0 to 1.0 each): crush or lift matte levels

**Approach:**
- Implement as post-processing on the alpha channel after inference
- Pure pixel math in `src/post_process/`, no external dependencies

### 2.3 Color Correction Controls

Compensate for the slight color shift that keying introduces.

**Parameters:**
- **Brightness** (0.5 to 2.0, default 1.0): simple gain on RGB
- **Saturation** (0.0 to 2.0, default 1.0): saturation adjustment

**Approach:**
- Apply after despill, before final output write
- Operate in linear space for correctness

## Phase 3 - Platform Parity

### 3.1 Windows OFX Validation

- Validate the existing Windows build at the same quality level
- Test with TensorRT, CUDA, and DirectML backends
- Ensure installer and packaging match macOS quality

### 3.2 Cross-Platform Feature Sync

- Ensure all Phase 1-2 features work identically on Windows
- Unified parameter set, same default values, same output behavior

## Phase 4 - Advanced Features

These are longer-term items informed by EZ-CorridorKey capabilities and
community feedback.

### 4.1 Multiple Alpha Hint Strategies

EZ-CorridorKey supports GVM, BiRefNet, SAM2, and MatAnyone2. Evaluate which
alternative hint generators could improve results for non-person subjects.

### 4.2 Tiled Inference for Large Resolutions

For 4K+ frames on constrained hardware, process in overlapping tiles with
linear blend ramps at tile boundaries. EZ-CorridorKey uses 512x512 tiles with
128px overlap.

### 4.3 Temporal Consistency

For video sequences, explore frame-to-frame matte consistency to reduce
flickering. This could leverage SAM2-style temporal propagation or simpler
alpha temporal filtering.

## Reference

- **EZ-CorridorKey:** github.com/edenaion/EZ-CorridorKey (Python standalone,
  same AI model, community-driven)
- **Archived plans:** docs/archive/PLAN_product_direction.md,
  docs/archive/PLAN_OFX_MAC_v1.md
