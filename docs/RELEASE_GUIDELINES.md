# Release Guidelines

This document outlines the standard operating procedure for versioning, building, and publishing releases of CorridorKey. It ensures consistency, reproducibility, and a clear, professional presentation to end users on GitHub.

## 1. Versioning Policy

CorridorKey follows Semantic Versioning (SemVer: `MAJOR.MINOR.PATCH`).

- **MAJOR**: Breaking changes (e.g., incompatible OFX parameter changes, removing supported GPUs, major structural rewrites).
- **MINOR**: New features added in a backwards-compatible manner (e.g., new AI models, new Quality drop-down options, performance optimizations).
- **PATCH**: Backwards-compatible bug fixes (e.g., resolving crashes, fixing UI flickering, updating default parameter values).

Every release **must** involve a `VERSION` bump in the root `CMakeLists.txt` reflecting the scope of the change.

## 2. Standardized Artifact Naming

To prevent user confusion and ensure transparency about the required hardware backend:

- All artifacts must include the version number and target OS in the filename.
- When an artifact is bound to a specific backend (like NVIDIA TensorRT versus Windows DirectML), that backend must be in the filename.
- Do not overload a generic name for a backend-specific build.

### Windows Installers
Generated via `scripts/package_ofx_installer_windows.ps1` and `scripts/package_runtime_installer_windows.ps1`.

- **TensorRT RTX (Primary):** `CorridorKey_Resolve_vX.Y.Z_Windows_RTX_Installer.exe`
- **DirectML (Fallback):** `CorridorKey_Resolve_vX.Y.Z_Windows_DirectML_Installer.exe`

### macOS Installers
- **Apple Silicon (CoreML/MLX):** `CorridorKey_Resolve_vX.Y.Z_macOS_Silicon_Installer.pkg`

### Release Archives
Raw `.zip` packages for manual installation must follow the same naming pattern.
- `CorridorKey_Resolve_vX.Y.Z_Windows_RTX.zip`
- `CorridorKey_Resolve_vX.Y.Z_Windows_DirectML.zip`

## 3. Build & Packaging Process

All releases must be built strictly through standardized scripts to eliminate manual errors and prevent crossing dependencies (e.g., packaging RTX DLLs into a DirectML installer).

### Windows Build Steps
Windows has two curated runtime roots and one canonical release entrypoint:

- `vendor\onnxruntime-windows-rtx` — curated TensorRT RTX runtime
- `vendor\onnxruntime-windows-dml` — curated DirectML runtime
- `scripts\release_pipeline_windows.ps1` — canonical Windows release pipeline

Do not use global ONNX Runtime installations or `vendor\onnxruntime-universal` as a fallback path. The release flow must resolve one of the curated runtime roots explicitly.

The supported Windows entrypoints are:
- `scripts\build.ps1` for normal development builds
- `scripts\prepare_windows_rtx_release.ps1` for staging the curated RTX runtime and assets
- `scripts\release_pipeline_windows.ps1` for full Windows release packaging

The supported repo-local runtime locations are only `vendor\onnxruntime-windows-rtx` and `vendor\onnxruntime-windows-dml`.

1. **Prepare the RTX runtime when needed:** build or refresh the curated RTX runtime and package inputs:
   ```powershell
   .\scripts\prepare_windows_rtx_release.ps1
   ```
2. **Run the canonical Windows release pipeline:**
   ```powershell
   .\scripts\release_pipeline_windows.ps1
   ```
3. **Use lower-level packaging scripts only for manual recovery or debugging:**
   ```powershell
   .\scripts\package_ofx_installer_windows.ps1 -ReleaseSuffix "RTX" -OrtRoot "vendor\onnxruntime-windows-rtx"
   .\scripts\package_ofx_installer_windows.ps1 -ReleaseSuffix "DirectML" -OrtRoot "vendor\onnxruntime-windows-dml"
   ```

*Note: Scripts are configured to validate paths. If `-OrtRoot` does not map to the exact expected curated runtime root, packaging aborts.*

### Windows Model Availability Policy

Windows build and release artifacts may be packaged with a partial model set
when some packaged artifacts are intentionally absent or temporarily
unavailable.

- Missing packaged models do not block bundle or installer generation by
  themselves.
- Packaging must emit explicit inventory and validation reports for every
  generated Windows distribution artifact.
- OFX bundles must include `model_inventory.json`.
- Windows OFX release folders must include `bundle_validation.json`.
- Missing packaged models must remain explicit warnings in packaging output and
  must surface as normal runtime/plugin errors when the missing quality is
  requested.
- Invalid packaged models that are present still fail validation. Partial model
  coverage is allowed; silently shipping broken artifacts is not.

## 4. GitHub Release Publishing

The GitHub release metadata requires a standardized title to maintain consistency across the page. Follow these patterns based on the build type and platform:

### CorridorKey Resolve OFX (Plugin)
- **Windows Only:** `CorridorKey Resolve OFX vX.Y.Z (Windows)`
- **macOS Only:** `CorridorKey Resolve OFX vX.Y.Z (macOS) - Apple Silicon`
- **Both Platforms:** `CorridorKey Resolve OFX vX.Y.Z (Windows & macOS)`

### CorridorKey Runtime (CLI/Daemon)
- **Windows Only:** `CorridorKey Runtime vX.Y.Z (Windows)`
- **macOS Only:** `CorridorKey Runtime vX.Y.Z (macOS)`
- **Both Platforms:** `CorridorKey Runtime vX.Y.Z (Windows & macOS)`

Use the exact template below for every release description:


```markdown
## Overview
[A concise 1-2 sentence description of what this release introduces, e.g., "Version 0.4.5 resolves the DaVinci Resolve UI flickering issue on systems missing alpha hints and clarifies dropdown options."]

## Changelog
### Added
- [Feature A]
- [Feature B]

### Changed
- [Modification A]

### Fixed
- [Bugfix A]

## Assets & Downloads

### Windows
- **NVIDIA RTX GPUs (Ampere / RTX 30 Series or newer):** Download `CorridorKey_Resolve_vX.Y.Z_Windows_RTX_Installer.exe`. This version provides maximum performance using the TensorRT execution provider.
- **Other Windows GPUs (RTX 20 Series, AMD, Intel):** Download `CorridorKey_Resolve_vX.Y.Z_Windows_DirectML_Installer.exe`. This version provides hardware acceleration across all DirectX 12 compatible GPUs.

### macOS
- Include this section only when the release contains a macOS installer.
- **Apple Silicon (M-Series):** Download `CorridorKey_Resolve_vX.Y.Z_macOS_Silicon_Installer.pkg`.

## Installation Instructions

1. Close DaVinci Resolve if it is running.
2. Run the downloaded installer `.exe` (Administrator privileges are requested automatically).
3. The installer will automatically overwrite any previous version.
4. Launch DaVinci Resolve. The plugin will be available in the OpenFX Library.

## Uninstallation
To remove the plugin entirely, go to **Windows Settings > Apps > Installed apps**, search for "CorridorKey Resolve OFX", and click Uninstall.

## Known Issues
- [List any currently tracked critical issues the user might face, e.g., "The OpenVINO backend is currently disabled on Intel integrated graphics due to memory constraints."]
- [If there are no known issues, omit this section entirely.]
```
