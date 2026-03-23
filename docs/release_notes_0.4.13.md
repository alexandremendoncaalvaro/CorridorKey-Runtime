## Overview
Version 0.4.13 resolves critical initialization issues for DirectML users and expands TensorRT support to include NVIDIA RTX 20 Series (Turing) GPUs.

## Changelog
### Added
- Expanded NVIDIA TensorRT support to include RTX 20 Series (Compute Capability 7.5).
- Implemented robust `device_index` mapping to prevent DirectML from defaulting to integrated graphics (iGPU) in multi-GPU systems.

### Changed
- Reverted memory pattern optimizations (`DisableMemPattern`) for DirectML to align with Microsoft's official driver recommendations, preventing memory allocation panics.
- Adjusted ONNX Runtime graph optimization levels (`ORT_ENABLE_EXTENDED` instead of `ORT_ENABLE_ALL`) specifically for DirectML to prevent CPU-centric memory layout reordering that caused `E_INVALIDARG` MatMul crashes.
- Clarified fallback policies to ensure models cleanly fallback to CPU execution for unsupported nodes instead of aborting the session.

### Fixed
- Fixed an issue where users with RTX 2080 and other Turing GPUs were incorrectly forced onto the slower DirectML fallback path.
- Fixed a major initialization bug where DirectML would attempt to run heavy AI models on underpowered integrated graphics despite the UI detecting a dedicated GPU.
- Fixed `0x80070057 (The parameter is incorrect)` crashes during FP16 MatMul operations on AMD and Intel hardware.

## Assets & Downloads

### Windows
- **NVIDIA RTX GPUs (Turing / RTX 20 Series or newer):** Download `CorridorKey_Resolve_v0.4.13_Windows_RTX_Installer.exe`. This version provides maximum performance using the TensorRT execution provider.
- **Other Windows GPUs (GTX, AMD, Intel):** Download `CorridorKey_Resolve_v0.4.13_Windows_DirectML_Installer.exe`. This version provides hardware acceleration across all DirectX 12 compatible GPUs.

## Installation Instructions

1. Close DaVinci Resolve if it is running.
2. Run the downloaded installer `.exe` (Administrator privileges are requested automatically).
3. The installer will automatically overwrite any previous version.
4. Launch DaVinci Resolve. The plugin will be available in the OpenFX Library.

## Uninstallation
To remove the plugin entirely, go to **Windows Settings > Apps > Installed apps**, search for "CorridorKey Resolve OFX", and click Uninstall.