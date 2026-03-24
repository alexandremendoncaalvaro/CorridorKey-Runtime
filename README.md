# CorridorKey Runtime

CorridorKey Runtime is a native C++ engine for neural green screen keying. It
packages the CorridorKey model into a distributable, hardware-accelerated
runtime with no Python dependency.

The project delivers two user surfaces:

- **CLI** (`corridorkey`) — a single-binary command-line tool for batch and
  pipeline use.
- **OFX Plugin** — an OpenFX plugin for DaVinci Resolve on Windows and macOS.

![CorridorKey OFX Plugin](./assets/ofx_example.gif)

## Installation

Pre-packaged releases are available on the
[Releases](https://github.com/alexandremendoncaalvaro/CorridorKey-Runtime/releases)
page. Download the package that matches your platform and hardware path.

For supported hardware configurations and explicit support status per platform,
see [help/SUPPORT_MATRIX.md](help/SUPPORT_MATRIX.md).

### OFX Plugin — macOS (Apple Silicon)

1. Download the `.pkg` Apple Silicon installer.
2. Run the installer with DaVinci Resolve closed.
3. Open DaVinci Resolve 20, go to the Color or Fusion page, and search for
   "CorridorKey" in the OpenFX Library.
4. Drag the node onto your clip. The plugin uses the MLX-accelerated path
   automatically on M-series chips.

### OFX Plugin — Windows

1. Download the `.exe` installer for your hardware path:
   - **TensorRT package** — for NVIDIA Ampere (RTX 30 series) and newer.
   - **DirectML package** — for Intel iGPU and other DirectX 12 hardware.
2. Run the installer as Administrator with DaVinci Resolve closed.
3. Open DaVinci Resolve 20, go to the Color or Fusion page, and search for
   "CorridorKey" in the OpenFX Library.
4. Drag the node onto your clip. TensorRT compilation on the first frame may
   take 10–30 seconds.

For plugin discovery issues, version mismatches, or unsupported hardware
fallback behavior, see [help/TROUBLESHOOTING.md](help/TROUBLESHOOTING.md).

### CLI

Download the portable `.zip` (Windows) or `.dmg` (macOS) release and place
`corridorkey` on your `PATH`.

## CLI Usage

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

Append `--json` to any command to receive NDJSON event streams for pipeline
integration.

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
cmake --preset release
cmake --build build/release --parallel
```

On Windows, run CMake from the x64 Native Tools Command Prompt for VS 2022.

## Documentation

### User Help

- [OFX Panel Guide](help/OFX_PANEL_GUIDE.md) — practical control-by-control
  guide for CorridorKey inside Resolve.
- [Resolve Tutorials](help/OFX_RESOLVE_TUTORIALS.md) — step-by-step workflows
  for getting a usable key and diagnosing common issues.
- [Support Matrix](help/SUPPORT_MATRIX.md) — official support status by
  platform, hardware, and Resolve version.
- [Troubleshooting](help/TROUBLESHOOTING.md) — practical guide for plugin
  discovery, hardware fallback, first-run behavior, and bug reporting.

### Development Docs

- [Technical Specification](docs/SPEC.md) — product scope and support
  philosophy.
- [Architecture](docs/ARCHITECTURE.md) — source structure and dependency
  rules.
- [Engineering Guidelines](docs/GUIDELINES.md) — code standards, testing
  strategy, and build rules.
- [Release Guidelines](docs/RELEASE_GUIDELINES.md) — release build and
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
