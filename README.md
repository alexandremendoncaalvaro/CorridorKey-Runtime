# CorridorKey Runtime

CorridorKey Runtime is a native C++ engine for neural green screen keying. It
packages the CorridorKey model into a distributable, hardware-accelerated
runtime with no Python dependency.

The project delivers two user surfaces:

- **CLI** (`corridorkey`) - a single-binary command-line tool for batch and
  pipeline use.
- **OFX Plugin** - an OpenFX plugin for DaVinci Resolve on Windows and macOS.

![CorridorKey OFX Plugin](./assets/ofx_example.gif)

## Installation

Pre-packaged releases are available on the
[Releases](https://github.com/alexandremendoncaalvaro/CorridorKey-Runtime/releases)
page. Download the package that matches your platform and product track.

For supported hardware configurations and explicit support status per platform,
see [help/SUPPORT_MATRIX.md](help/SUPPORT_MATRIX.md).

### OFX Plugin - macOS (Apple Silicon)

1. Download the `.pkg` Apple Silicon installer.
2. Run the installer with DaVinci Resolve closed.
3. Open DaVinci Resolve 20, go to the Color or Fusion page, and search for
   "CorridorKey" in the OpenFX Library.
4. Drag the node onto your clip. The plugin uses the MLX-accelerated path
   automatically on M-series chips.

### OFX Plugin - Windows

1. Download the `.exe` installer for your hardware path:
   - **Windows RTX** - official Windows RTX installer for NVIDIA RTX 30 series
     and newer. This track ships the public FP16 quality ladder:
     **Draft (512)**, **High (1024)**, **Ultra (1536)**, and
     **Maximum (2048)**. `Auto` respects the safe VRAM ceiling for the active
     GPU tier, while manual fixed quality may attempt a higher packaged rung
     with explicit runtime fallback if it fails.
   - **DirectML package** - experimental Windows track for DirectX 12 GPUs
     outside the official RTX path. This track is not broadly validated across
     AMD, Intel, or RTX 20 series hardware and should only appear in releases
     when it is published intentionally.
2. Run the installer as Administrator with DaVinci Resolve closed.
3. Open DaVinci Resolve 20, go to the Color or Fusion page, and search for
   "CorridorKey" in the OpenFX Library.
4. Drag the node onto your clip. TensorRT RTX compilation on the first frame
   may take 10-30 seconds.

For plugin discovery issues, version mismatches, or unsupported hardware
behavior, see [help/TROUBLESHOOTING.md](help/TROUBLESHOOTING.md).

### CLI

Download the portable runtime release for your platform.

- On macOS bundles and source builds, use `corridorkey`
- In the Windows portable runtime bundle, use `ck-engine.exe`

## CLI Usage

The examples below use the macOS and source-build command name `corridorkey`.
In the Windows portable runtime bundle, replace it with `ck-engine.exe`.

**Check hardware capability:**
```bash
corridorkey doctor
```

**Process a video with hardware-aware defaults:**
```bash
corridorkey process input.mp4 output.mp4
```

**Process with a specific preset:**
```bash
corridorkey process input.mp4 output.mp4 --preset max
```

**Process with an external Alpha Hint:**
```bash
corridorkey process input.mp4 output.mp4 --alpha-hint hint.mp4
```

**Compare the Windows RTX shipping path against the experimental max-performance mode:**
```bash
corridorkey benchmark --preset balanced --engine official
corridorkey benchmark --preset balanced --engine max-performance
```

Append `--json` to any command to receive NDJSON event streams for pipeline
integration. On Windows RTX, `--engine official` keeps the supported ORT
TensorRT path, while `--engine max-performance` enables the experimental
comparison mode on the same packaged ladder.

## Building from Source

### Prerequisites

- C++20 compiler: Visual Studio 2022 (v17.4+), Apple Clang 15+, or GCC 12+
- CMake 3.28+
- Ninja
- vcpkg with `VCPKG_ROOT` set

### Build

```bash
git clone https://github.com/alexandremendoncaalvaro/CorridorKey-Runtime.git
cd CorridorKey-Runtime

# Fetch models from Hugging Face Hub (required for inference and E2E tests)
.\scripts\fetch_models.ps1            # Windows (default: windows-rtx)
# or: ./scripts/fetch_models.ps1 -Profile all   # All platforms

export VCPKG_ROOT="$HOME/vcpkg"
cmake --preset release
cmake --build --preset release
```

On Windows, use `.\scripts\windows.ps1 -Task build -Preset release` for local
builds and `.\scripts\windows.ps1 -Task release -Version X.Y.Z` for official
Windows release packaging. That canonical release command emits the official
`Windows RTX` installer by default. Publish the experimental `DirectML` track
only when you request it explicitly with
`-Track dml` or `-Track all`.
Lower-level Windows scripts exist only as internal delegates for debugging the
wrapper itself. If you invoke CMake directly, activate the MSVC developer
environment first. Windows distribution artifacts include
`model_inventory.json` and `bundle_validation.json` when packaging succeeds
with a partial model set, so missing packaged models are explicit and do not
silently change runtime behavior.

For local Windows workflow, the canonical wrapper exposes three different
levels of operation:

- `.\scripts\windows.ps1 -Task build -Preset release`
  - build only
- `.\scripts\windows.ps1 -Task package-ofx -Version X.Y.Z -Track rtx`
  - package the `Windows RTX` installer from an already certified Windows RTX
    model set
- `.\scripts\windows.ps1 -Task certify-rtx-artifacts -Version X.Y.Z`
  - certify an already existing Windows RTX model set and write the artifact
    manifest without regenerating the `.onnx` files from the checkpoint
- `.\scripts\windows.ps1 -Task regen-rtx-release -Version X.Y.Z`
  - regenerate ONNX artifacts from the checkpoint, certify the RTX ladder,
    write the artifact manifest, and then package the `Windows RTX`
    installer

`package-ofx` for Windows RTX is intentionally strict. It no longer accepts a
raw `models\` folder by itself. The command requires a certified
`artifact_manifest.json` that matches the packaged RTX model and `*_ctx.onnx`
files exactly. If you only have stale or manually copied models, use
`certify-rtx-artifacts` or `regen-rtx-release` first.

## Documentation

### User Help

- [OFX Panel Guide](help/OFX_PANEL_GUIDE.md) - practical control-by-control
  guide for CorridorKey inside Resolve.
- [Resolve Tutorials](help/OFX_RESOLVE_TUTORIALS.md) - step-by-step workflows
  for getting a usable key and diagnosing common issues.
- [Support Matrix](help/SUPPORT_MATRIX.md) - official support status by
  platform, hardware, and Resolve version.
- [Troubleshooting](help/TROUBLESHOOTING.md) - practical guide for plugin
  discovery, hardware selection, first-run behavior, and bug reporting.

### Development Docs

- [Technical Specification](docs/SPEC.md) - product scope and support
  philosophy.
- [Architecture](docs/ARCHITECTURE.md) - source structure and dependency
  rules.
- [Engineering Guidelines](docs/GUIDELINES.md) - code standards, testing
  strategy, and build rules.
- [Release Guidelines](docs/RELEASE_GUIDELINES.md) - release build and
  packaging procedure.

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for development setup and PR process.

## License

[CC BY-NC-SA 4.0](LICENSE)

You may use this software to process commercial video. You may not repackage
or sell the software itself or offer it as a paid service.

## Acknowledgements

- **Original CorridorKey:** [github.com/nikopueringer/CorridorKey](https://github.com/nikopueringer/CorridorKey)
- **EZ-CorridorKey:** [github.com/edenaion/EZ-CorridorKey](https://github.com/edenaion/EZ-CorridorKey)
- **CorridorKey-Engine:** [github.com/99oblivius/CorridorKey-Engine](https://github.com/99oblivius/CorridorKey-Engine)
- **ONNX Runtime** by Microsoft
- **OpenEXR** by Academy Software Foundation
