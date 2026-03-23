## Overview

Version 0.4.13 resolves critical initialization issues for DirectML users on Windows, expands TensorRT support to include NVIDIA RTX 20 Series (Turing) GPUs, and delivers the first macOS Apple Silicon release with MLX backend support for DaVinci Resolve.

## Changelog

### Added

- Expanded NVIDIA TensorRT support to include RTX 20 Series (Compute Capability 7.5).
- Implemented robust `device_index` mapping to prevent DirectML from defaulting to integrated graphics (iGPU) in multi-GPU systems.
- Centralized all ONNX Runtime execution provider identifiers into a single constants header, preventing string-matching bugs across platforms.
- Added diagnostic logging on macOS (writes to `~/Library/Logs/CorridorKey/ofx.log`), matching the Windows logging infrastructure.
- Added CoreML provider string validation to regression tests.
- Added backend validation to the macOS OFX packaging pipeline.
- Added macOS build and release pipeline scripts (`scripts/build.sh`, `scripts/release_pipeline_macos.sh`).

### Changed

- Reverted memory pattern optimizations (`DisableMemPattern`) for DirectML to align with Microsoft's official driver recommendations, preventing memory allocation panics.
- Adjusted ONNX Runtime graph optimization levels (`ORT_ENABLE_EXTENDED` instead of `ORT_ENABLE_ALL`) specifically for DirectML to prevent CPU-centric memory layout reordering that caused `E_INVALIDARG` MatMul crashes.
- Clarified fallback policies to ensure models cleanly fall back to CPU execution for unsupported nodes instead of aborting the session.

### Fixed

- Fixed an issue where users with RTX 2080 and other Turing GPUs were incorrectly forced onto the slower DirectML fallback path.
- Fixed a major initialization bug where DirectML would attempt to run heavy AI models on underpowered integrated graphics despite the UI detecting a dedicated GPU.
- Fixed `0x80070057 (The parameter is incorrect)` crashes during FP16 MatMul operations on AMD and Intel hardware.
- Fixed the OFX plugin on macOS incorrectly selecting the CoreML backend instead of MLX on Apple Silicon, which caused High (1024) and above quality modes to fail with missing artifact errors.
- Fixed symbol visibility exports for macOS dylib boundary, resolving linker errors when building the OFX plugin with `-fvisibility=hidden`.
- Fixed ONNX Runtime header include order conflict on macOS that caused `ONNXTensorElementDataType` redefinition errors when CoreML provider headers were included.

## Assets and Downloads

### Windows

- **NVIDIA RTX GPUs (Turing / RTX 20 Series or newer):** `CorridorKey_Resolve_v0.4.13_Windows_RTX_Installer.exe` provides maximum performance using the TensorRT execution provider.
- **Other Windows GPUs (GTX, AMD, Intel):** `CorridorKey_Resolve_v0.4.13_Windows_DirectML_Installer.exe` provides hardware acceleration across all DirectX 12 compatible GPUs.

### macOS (Apple Silicon)

- **DaVinci Resolve OFX Plugin:** `CorridorKey_Resolve_v0.4.13_macOS_AppleSilicon.dmg` contains a signed and notarized installer package.
- **Standalone CLI:** `CorridorKey_Runtime_v0.4.13_macOS_AppleSilicon.zip` is a portable bundle with MLX and CoreML backends.

## Installation

### Windows

1. Close DaVinci Resolve if it is running.
2. Run the downloaded installer `.exe` (Administrator privileges are requested automatically).
3. The installer will automatically overwrite any previous version.
4. Launch DaVinci Resolve. The plugin appears in the OpenFX Library.

### macOS

1. Close DaVinci Resolve if it is running.
2. Open the downloaded `.dmg` and run the `.pkg` installer.
3. If macOS blocks the installer, remove the quarantine attribute: `xattr -dr com.apple.quarantine <downloaded-file>`.
4. Launch DaVinci Resolve. The plugin appears in the OpenFX Library.

## Uninstallation

### Windows

Go to **Settings > Apps > Installed apps**, search for "CorridorKey Resolve OFX", and click Uninstall.

### macOS

Delete the bundle from `/Library/OFX/Plugins/CorridorKey.ofx.bundle`.
