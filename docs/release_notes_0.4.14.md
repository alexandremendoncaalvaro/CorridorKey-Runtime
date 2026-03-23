## Overview

Version 0.4.14 delivers a major OFX parameter panel overhaul with a workflow-driven layout, two new post-processing controls (Spill Method and Matte Gamma), improved defaults, and parameter interdependency (dependent controls disable automatically). The despill engine is extended with three algorithms. Alpha gamma correction is implemented with a LUT to avoid expensive math in the pixel loop.

## Changelog

### Added

- Added Spill Method parameter with three algorithms: Average (default), Double Limit, and Neutral. Average redistributes removed green equally to R and B. Double Limit uses the higher of R or B as the suppression ceiling, producing a less aggressive result. Neutral fills proportionally to scene gray to avoid magenta shift on dark pixels.
- Added Matte Gamma parameter (0.1–10.0, default 1.0) for non-linear alpha curve adjustment. Values above 1.0 recover semi-transparent areas (hair, smoke); values below 1.0 tighten the matte.
- Added parameter interdependency: Tile Overlap disables when Tiling is off, Min Region Size disables when Despeckle is off, Detail Mask Shrink and Detail Mask Feather disable when Core Detail Recovery is off.

### Changed

- Reorganized OFX parameter panel to follow the compositing workflow: Key Setup → Matte → Edge and Spill → Output → Performance → Status → Advanced.
- Renamed parameters for clarity: Source Passthrough → Core Detail Recovery, Edge Erode → Detail Mask Shrink, Edge Blur → Detail Mask Feather, Despeckle Size → Min Region Size, Alpha Erode/Dilate → Matte Shrink/Grow, Alpha Edge Softness → Matte Edge Blur, Alpha Black Point → Matte Clip Black, Alpha White Point → Matte Clip White. OFX parameter identifiers are unchanged, preserving saved projects.
- Renamed quality labels: Preview (512) → Draft (512). No behavior change.
- Renamed quantization labels: FP16 → Full Precision, INT8 → Compact. No behavior change.
- Collapsed Runtime Status group by default and moved it to the bottom of the panel.
- Removed "Processed Note" info label.
- Changed Quality Mode default from Preview to Auto.
- Changed Source Passthrough (now Core Detail Recovery) default from off to on, improving interior detail on first use.
- Changed Input Color Space default from Linear to sRGB to match the most common DaVinci Resolve Color page workflow.

### Fixed

- Fixed integration test for OFX C-API exception boundary: plugin path was hardcoded as `./CorridorKey.ofx`, causing the test to fail when run from any directory other than the OFX build output. Path is now injected via a CMake generator expression at build time.
- Fixed narrowing conversion compile errors in `preset_catalog()` caused by the `InferenceParams` struct gaining the `spill_method` field without updating the eight positional aggregate initializers.
- Fixed `sync_dependent_params` and `set_param_enabled` being called before their definitions in `ofx_instance.cpp`, causing undeclared identifier errors under strict compilation.

## Assets and Downloads

### macOS (Apple Silicon)

- **DaVinci Resolve OFX Plugin:** `CorridorKey_Resolve_v0.4.14_macOS_AppleSilicon.dmg` contains a signed and notarized installer package.
- **Standalone CLI:** `CorridorKey_Runtime_v0.4.14_macOS_AppleSilicon.zip` is a portable bundle with MLX and CoreML backends.

## Installation

### macOS

1. Close DaVinci Resolve if it is running.
2. Open the downloaded `.dmg` and run the `.pkg` installer.
3. If macOS blocks the installer, remove the quarantine attribute: `xattr -dr com.apple.quarantine <downloaded-file>`.
4. Launch DaVinci Resolve. The plugin appears in the OpenFX Library.

## Uninstallation

### macOS

Delete the bundle from `/Library/OFX/Plugins/CorridorKey.ofx.bundle`.
