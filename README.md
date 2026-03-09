# CorridorKey Runtime

A high-performance, cross-platform C++ inference runtime for the
[CorridorKey](https://github.com/nikopueringer/CorridorKey) neural green screen
keyer.

CorridorKey Runtime takes the trained GreenFormer model and makes it accessible
on consumer hardware — from 8GB laptops to workstations with dedicated GPUs —
as a single binary with zero Python dependencies.

## Features

- **Cross-platform:** macOS (Apple Silicon), Windows, Linux
- **Adaptive hardware tiers:** Automatically selects resolution and model
  quality based on available memory
- **Multiple backends:** CoreML (macOS), TensorRT RTX (NVIDIA), DirectML
  (Windows), CPU (universal) — all via ONNX Runtime
- **VFX-grade output:** 16-bit EXR, proper sRGB/linear color math, premultiplied
  alpha
- **Video support:** Direct MP4/MOV processing via FFmpeg
- **Library + CLI:** Core engine is a reusable C++ library; CLI is a thin
  frontend

## Quick Start

### Prerequisites

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

**Download the model** (int8 is recommended for most hardware):
```bash
corridorkey download --variant int8
```

**View detected hardware and capabilities**:
```bash
corridorkey info
```

**Process a single video**:
```bash
corridorkey process input.mp4 --alpha-hint hint.mp4 --output output.mp4
```

**Process a directory of frames**:
```bash
corridorkey process ./Input/ --alpha-hint ./AlphaHint/ --output ./Output/
```

**Process a single EXR/PNG frame**:
```bash
corridorkey process frame.exr --alpha-hint hint.png --output result.exr
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

## Project Status & Roadmap

### ✅ Done
- **Zero-Python Runtime:** Standalone C++ binary with no external ML environment needed.
- **FFmpeg Integration:** Process `.mp4`/`.mov` files directly in RAM.
- **Hardware-Aware Tiers:** Adaptive resolution (512px to 1024px) based on detected RAM/VRAM.
- **Auto-Hinting:** Generates a guide matte internally if no manual alpha hint is provided.
- **VFX-Grade I/O:** Support for 16-bit linear EXR and proper sRGB/Linear color math.

### 🛠 In Progress / Planned
- [ ] **CoreML & TensorRT:** Native GPU/Neural Engine acceleration (currently running on CPU fallback).
- [ ] **Tiling Inference:** Support for 4K/8K processing on low-VRAM GPUs by segmenting frames.
- [ ] **Model Auto-Download:** Automatic model fetching from HuggingFace via CLI.
- [ ] **Unit Testing:** Comprehensive test suite for all post-processing math.
- [ ] **GUI Interface:** Simple drag-and-drop tool for non-CLI users.

## Hardware Support

| Tier | Example Hardware | Model | Resolution | Expected FPS |
|------|-----------------|-------|------------|-------------|
| High | RTX 3080+ (10GB+) | FP16 | 1024-2048px | 2-5 fps |
| Medium | MacBook M1 Pro (16GB) | INT8 | 512-768px | 1-3 fps |
| Low | MacBook Air M1 (8GB) | INT8 | 512px | 0.5-2 fps |
| CPU | Any x86/ARM (16GB+) | INT8 | 512px | 0.1-0.5 fps |

The runtime auto-detects your hardware and selects the best configuration.
Override with `--device`, `--resolution`, and `--quality` flags.

## Documentation

- [Technical Specification](docs/SPEC.md) — Architecture, design decisions,
  implementation phases
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
