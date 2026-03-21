# CorridorKey Runtime

CorridorKey Runtime is a production-oriented native engine for neural green screen keying. It packages the CorridorKey model execution into a high-performance C++ runtime, delivering predictable, hardware-accelerated inference without the friction of Python virtual environments. 

This repository provides the core CLI and a production-grade **OpenFX Plugin** for DaVinci Resolve.

![CorridorKey OFX Plugin](./assets/ofx_example.gif)

## Key Features

- **Native Execution:** A single-binary C++ workflow with zero Python dependencies.
- **Hardware Accelerated:** Curated GPU backends for maximum performance:
  - **macOS:** Native Apple Silicon acceleration via MLX.
  - **Windows:** Dedicated TensorRT (RTX) and DirectML (AMD/Intel) tracks.
- **DaVinci Resolve OFX:** A fully integrated plugin that isolates backend and VRAM failures, compiling models on-the-fly for real-time scrubbing.
- **Production Built:** Predictable stage-level diagnostics, stable machine-readable contracts (JSON/NDJSON), and advanced alpha hinting capabilities for professional compositing.

## Downloads & Installation

We provide pre-packaged releases for standard use cases. See the [Releases](https://github.com/alexandremendoncaalvaro/CorridorKey-Runtime/releases) page for the latest downloads.

### DaVinci Resolve OFX Plugin

The project ships with a fully integrated OpenFX Plugin for Windows and macOS.

#### Windows
1. Download the latest `.exe` installer for your hardware:
   - **RTX Version:** For NVIDIA Ampere / RTX 30 Series or newer (Maximum performance via TensorRT).
   - **DirectML Version:** For AMD, Intel, or older NVIDIA GPUs (Hardware acceleration via DirectX 12).
2. Run the installer as Administrator (DaVinci Resolve must be closed).
3. Open DaVinci Resolve, navigate to the **Color** or **Fusion** page, and search for "CorridorKey" in the OpenFX Library.
4. Drag and drop the node onto your clip. *Note: TensorRT compilation on the first frame may take 10-30 seconds.*

#### macOS (Apple Silicon)
1. Download the latest `.pkg` Apple Silicon installer from the Releases page.
2. Run the installer package (DaVinci Resolve must be closed).
3. Open DaVinci Resolve, navigate to the **Color** or **Fusion** page, and search for "CorridorKey".
4. The plugin automatically uses the MLX-accelerated path for fast inference on M-series chips.

### Portable CLI (macOS & Windows)

If you prefer the command line or want to integrate the engine into your own pipeline, download the portable `.zip` or `.dmg` releases.

## CLI Usage

The runtime operates through a clean CLI contract. If `corridorkey` is in your `PATH`:

**Check your hardware capability:**
```bash
corridorkey doctor
```

**Process a single video using the hardware-aware defaults:**
```bash
corridorkey process input.mp4 output.mp4
```

**Process a 4K video using the maximum quality preset:**
```bash
corridorkey process input_4k.mp4 output_4k.mp4 --preset max
```

**Process with an external Alpha Hint for high-fidelity control:**
```bash
corridorkey process input.mp4 output.mp4 --alpha-hint hint.mp4
```

*For structured pipeline integration, append `--json` to receive NDJSON event streams.*

## Compiling from Source

For developers who wish to build the engine from source.

### Prerequisites

- **C++20 compiler:** Visual Studio 2022 (v17.4+), Apple Clang 15+, or GCC 12+/Clang 16+
- [CMake 3.28+](https://cmake.org/download/)
- [Ninja](https://ninja-build.org/) build system
- [vcpkg](https://github.com/microsoft/vcpkg) package manager

### Build Flow

1. **Install vcpkg** (Skip if `VCPKG_ROOT` is set):
   - **Unix:** `git clone https://github.com/microsoft/vcpkg.git ~/vcpkg && ~/vcpkg/bootstrap-vcpkg.sh && export VCPKG_ROOT="$HOME/vcpkg"`
   - **Windows:** Run `.\scripts\setup_windows.ps1`
2. **Configure and Build:**

```bash
git clone https://github.com/alexandremendoncaalvaro/CorridorKey-Runtime.git
cd CorridorKey-Runtime
cmake --preset release
cmake --build build/release --parallel
```

## Documentation

For deep technical insights, architecture guidelines, and engineering standards:

- [Technical Specification](docs/SPEC.md) — Product direction, delivery phases, and IPC contracts.
- [Architecture](docs/ARCHITECTURE.md) — Library-first structure and dependency rules.
- [Frontend](docs/FRONTEND.md) — GUI constraints, UI/UX specifications, and tech stack.
- [Engineering Guidelines](docs/GUIDELINES.md) — Code standards, testing strategy, and project rules.
- [Release Guidelines](docs/RELEASE_GUIDELINES.md) — Standard operating procedure for building and releasing.

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for development setup, coding standards, and how to submit pull requests. Planners and maintainers should review the [PLAN.md](docs/PLAN.md) checklist for ongoing workstreams.

## License

[CC BY-NC-SA 4.0](LICENSE) — Same license as the original CorridorKey project.

You may use this software to process commercial video. You may not repackage and sell the software itself or offer it as a paid service. See [LICENSE](LICENSE) for full terms.

## Acknowledgements & Credits

This runtime is built on the shoulders of these incredible projects:

- **Original CorridorKey:** [github.com/nikopueringer/CorridorKey](https://github.com/nikopueringer/CorridorKey)
- **EZ-CorridorKey:** [github.com/edenaion/EZ-CorridorKey](https://github.com/edenaion/EZ-CorridorKey)
- **CorridorKey-Engine:** [github.com/99oblivius/CorridorKey-Engine](https://github.com/99oblivius/CorridorKey-Engine)
- **ONNX Runtime** by Microsoft — model execution infrastructure.
- **OpenEXR** by Academy Software Foundation — VFX image format support.
