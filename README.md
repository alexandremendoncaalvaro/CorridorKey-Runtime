# CorridorKey Runtime

A high-performance C++ inference runtime for the
[CorridorKey](https://github.com/nikopueringer/CorridorKey) neural green screen
keyer.

CorridorKey Runtime takes the trained GreenFormer model and makes it accessible
on consumer hardware as a single binary with zero Python dependencies.

The current release track is intentionally narrow: macOS 14+ on Apple Silicon
is the only release-gating platform in this phase. Windows and Linux remain
part of the architecture, but they are not treated as release-ready targets
yet.

## Features

- **macOS-first release track:** CoreML-first execution on Apple Silicon with
  mandatory CPU fallback
- **Portable CLI contract:** `info`, `doctor`, `benchmark`, `models`, and
  `presets` expose stable JSON; `process --json` emits NDJSON job events
- **Measured diagnostics:** synthetic and real-workload benchmarking with
  stage-level timings
- **Validated model catalog:** `int8_512` and `int8_768` are the packaged
  macOS defaults in this phase
- **VFX-grade output:** 16-bit EXR, proper sRGB/linear color math,
  premultiplied alpha
- **Video support:** FFmpeg pipeline with H.264-in-MP4 as the portable macOS
  default and VideoToolbox when available
- **Library + CLI:** core engine is a reusable C++ library; CLI is a thin
  interface layer

## Quick Start

### Prerequisites

Validated platform for this phase: macOS 14+ on Apple Silicon.
Other platforms are still part of the long-term architecture, but the current
documentation and acceptance gates are written around macOS.

- C++20 compiler (GCC 12+, Clang 16+, MSVC 17.4+)
- [CMake 3.28+](https://cmake.org/download/)
- [Ninja](https://ninja-build.org/) build system
- [vcpkg](https://github.com/microsoft/vcpkg) package manager

### 1. Install vcpkg

Skip this step if you already have vcpkg installed and `VCPKG_ROOT` set.

<details>
<summary>Linux / macOS</summary>

```bash
git clone https://github.com/microsoft/vcpkg.git ~/vcpkg
~/vcpkg/bootstrap-vcpkg.sh
export VCPKG_ROOT="$HOME/vcpkg"
```

Add the `export` line to your shell profile (`~/.bashrc`, `~/.zshrc`, etc.) for persistence.

</details>

<details>
<summary>Windows</summary>

```powershell
git clone https://github.com/microsoft/vcpkg.git %USERPROFILE%\vcpkg
%USERPROFILE%\vcpkg\bootstrap-vcpkg.bat
```

Add `VCPKG_ROOT` as a System Environment Variable pointing to `%USERPROFILE%\vcpkg`.

</details>

### 2. Build

```bash
git clone https://github.com/alexandremendoncaalvaro/CorridorKey-Runtime.git
cd CorridorKey-Runtime
cmake --preset release
cmake --build build/release --parallel
```

### 3. Add to PATH (optional)

The binary is at `build/release/src/cli/corridorkey`. To use `corridorkey`
from anywhere, add the build directory to your PATH:

```bash
export PATH="$(pwd)/build/release/src/cli:$PATH"
```

Add the line above (with the full absolute path) to your shell profile for
persistence. Alternatively, create a system-wide symlink (requires sudo):

```bash
sudo ln -s "$(pwd)/build/release/src/cli/corridorkey" /usr/local/bin/corridorkey
```

### Usage

If you haven't added the binary to your PATH, replace `corridorkey` with `./build/release/src/cli/corridorkey` in the commands below.

**Download model files** (downloads 512/768/1024 variants for the selected quality):
```bash
corridorkey download --variant int8
```

**View detected hardware, capabilities, and recommended defaults**:
```bash
corridorkey info --json
```

**Inspect validated models and presets**:
```bash
corridorkey models --json
corridorkey presets --json
```

**Run a synthetic benchmark and inspect stage timings**:
```bash
corridorkey benchmark --json --model models/corridorkey_int8_512.onnx --device cpu
```

**Run a real-workload benchmark against a source file**:
```bash
corridorkey benchmark --json --input input.mp4 --output benchmark_output.mp4 --model models/corridorkey_int8_768.onnx
```

**Process a single video**:
```bash
corridorkey process --json --input input.mp4 --alpha-hint hint.mp4 --output output.mp4 --model models/corridorkey_int8_768.onnx
```

**Process a directory of frames**:
```bash
corridorkey process --input ./Input/ --alpha-hint ./AlphaHint/ --output ./Output/ --model models/corridorkey_int8_768.onnx
```

**Process a single EXR/PNG frame**:
```bash
corridorkey process --input frame.exr --alpha-hint hint.png --output result.exr --model models/corridorkey_int8_512.onnx
```

**Use tiled inference for larger inputs**:
```bash
corridorkey process --input input_4k.mp4 --output output_4k.mp4 --model models/corridorkey_int8_768.onnx --tiled
```

<details>
<summary>Docker (NVIDIA GPU)</summary>

Requires [NVIDIA Container Toolkit](https://docs.nvidia.com/datacenter/cloud-native/container-toolkit/latest/install-guide.html).

```bash
docker compose build
docker compose up
```

Place your files in `./data/input` and `./data/hint` before running.

For an interactive shell:

```bash
docker compose run --rm corridorkey-gpu /bin/bash
```

</details>

### Output

```
Output/
  Matte/       # Alpha channel — EXR 16-bit linear
  FG/          # Foreground straight color — EXR 16-bit linear
  Processed/   # Premultiplied RGBA — EXR 16-bit linear
  Comp/        # Preview on checkerboard — PNG 8-bit sRGB
```

## Machine-Readable Interfaces

- `info`, `doctor`, `benchmark`, `models`, and `presets` return a single JSON
  document when `--json` is present.
- `benchmark --json` supports two modes:
  - `synthetic` for controlled throughput/latency checks without external I/O
  - `workload` for full pipeline measurement against a real image sequence or
    video input
- `benchmark --json` reports `stage_timings`, backend selection, and structured
  fallback details when they occur.
- `process --json` emits NDJSON events:
  - `job_started`
  - `backend_selected`
  - `progress`
  - `warning`
  - `artifact_written`
  - `completed`
  - `failed`
  - `cancelled`
- Terminal `process --json` events include aggregated stage timings for the
  full job.

## Current Scope

- **Zero-Python Runtime:** Standalone C++ binary with no external ML environment needed.
- **FFmpeg Integration:** Process `.mp4`/`.mov` files directly in RAM.
- **Hardware-Aware Tiers:** Adaptive resolution strategy based on detected memory profile.
- **Auto-Hinting Fallback:** Generates a rough guide matte when no manual alpha hint is provided.
- **VFX-Oriented I/O:** EXR + PNG outputs with sRGB/linear conversion and premultiplied output buffers.
- **Runtime Observability:** Stage-level timings for engine creation, warmup, inference, I/O, and full-job execution.

## Priority Direction

- Release-grade macOS hardening on Apple Silicon, including CoreML-first execution with CPU fallback.
- Portable macOS bundle workflows for third-party machines.
- Stable CLI JSON/NDJSON contracts for the future Tauri sidecar and GUI.
- GUI work only after macOS runtime quality and portability gates are passing.

## Platform Status

| Platform | Backends | Status | Notes |
|----------|----------|--------|-------|
| macOS 14+ Apple Silicon | CoreML, CPU | Release-gating | `auto` prefers CoreML and falls back to CPU with a structured reason |
| Windows | DirectML, CPU | Architectural target | Not release-gating in this phase |
| Linux | CPU | Architectural target | Not release-gating in this phase |

The application layer auto-detects available hardware and recommends the safest
validated configuration for the current machine. In this phase, `auto`
resolution maps to `512` on 8 GB-class Macs and `768` on 16 GB-class Macs.
Override with `--device`, `--resolution`, and `--model` when you need an
explicit path.

## Documentation

- [Technical Specification](docs/SPEC.md) — Architecture, design decisions,
  implementation phases
- [Architecture](docs/ARCHITECTURE.md) — Layering, directory map, bridge rules
- [Frontend](docs/FRONTEND.md) — GUI constraints and sidecar contract
- [Engineering Guidelines](docs/GUIDELINES.md) — Code standards, testing
  strategy, linting, git hooks

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for development setup, coding standards,
and how to submit changes.

## License

[CC BY-NC-SA 4.0](LICENSE) — Same license as the original CorridorKey project.

You may use this software to process commercial video. You may not repackage and
sell the software itself or offer it as a paid service. See
[LICENSE](LICENSE) for full terms.

## Credits

- [CorridorKey](https://github.com/nikopueringer/CorridorKey) by Niko Pueringer
  / Corridor Digital — the original neural green screen keyer
- [ONNX Runtime](https://onnxruntime.ai/) by Microsoft — cross-platform ML
  inference
- [OpenEXR](https://openexr.com/) by Academy Software Foundation — VFX image
  format
