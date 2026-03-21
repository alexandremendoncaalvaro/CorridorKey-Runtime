## Overview
Version 0.4.6 enforces zero-allocation post-processing loops for maximum memory safety and introduces robust C++ exception boundaries to prevent native OFX plugin crashes inside DaVinci Resolve.

## Changelog
### Added
- Native try-catch boundaries across the entire OpenFX API layer to gracefully report `kOfxStatFailed` instead of crashing the host application.

### Changed
- Refactored `despeckle`, `alpha_edge`, `gaussian_blur`, and `source_passthrough` to eliminate per-frame dynamic memory allocations using persistent state objects.
- Corrected Windows installer naming conventions to properly delineate RTX from DirectML packages.

### Fixed
- Addressed multiple compilation errors and `std::span` struct instantiation bugs inside the MSVC build pipeline.

## Assets & Downloads

### Windows
- **NVIDIA RTX GPUs (Ampere / RTX 30 Series or newer):** Download `CorridorKey_Resolve_v0.4.6_Windows_RTX_Installer.exe`. This version provides maximum performance using the TensorRT execution provider.
- **Other Windows GPUs (RTX 20 Series, AMD, Intel):** Download `CorridorKey_Resolve_v0.4.6_Windows_DirectML_Installer.exe`. This version provides hardware acceleration across all DirectX 12 compatible GPUs.

### macOS
- **Apple Silicon (M-Series):** [Coming soon]

## Installation Instructions

1. Close DaVinci Resolve if it is running.
2. Run the downloaded installer `.exe` (Administrator privileges are requested automatically).
3. The installer will automatically overwrite any previous version.
4. Launch DaVinci Resolve. The plugin will be available in the OpenFX Library.

## Uninstallation
To remove the plugin entirely, go to **Windows Settings > Apps > Installed apps**, search for "CorridorKey Resolve OFX", and click Uninstall.
