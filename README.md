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
- [vcpkg](https://github.com/microsoft/vcpkg)

### Build

```bash
git clone https://github.com/<org>/CorridorKey-Runtime.git
cd CorridorKey-Runtime

cmake --preset release
cmake --build build/release --parallel
```

### Usage

#### Native CLI

```bash
# Download the model (first time only)
corridorkey download --variant int8

# Show detected hardware
corridorkey info

# Process a video
corridorkey process input.mp4 --alpha-hint hint.mp4 --output output.mp4

# Process an image sequence
corridorkey process ./Input/ --alpha-hint ./AlphaHint/ --output ./Output/

# Process a single frame
corridorkey process frame.exr --alpha-hint hint.png --output result.exr
```

#### Docker (NVIDIA GPU)

For isolated environments or cloud deployments, an NVIDIA-optimized Docker image is provided. This ensures perfect compatibility with CUDA and TensorRT without polluting your host system.

**Prerequisite:** [NVIDIA Container Toolkit](https://docs.nvidia.com/datacenter/cloud-native/container-toolkit/latest/install-guide.html) must be installed.

```bash
# 1. Build the GPU image (compiles the C++ runtime)
docker compose build

# 2. Run inference using the default mapped directories
# (Place your files in ./data/input and ./data/hint first)
docker compose up

# OR run an interactive shell inside the container
docker compose run --rm corridorkey-gpu /bin/bash
```

### Output

```
Output/
  Matte/       # Alpha channel — EXR 16-bit linear
  FG/          # Foreground straight color — EXR 16-bit linear
  Processed/   # Premultiplied RGBA — EXR 16-bit linear
  Comp/        # Preview on checkerboard — PNG 8-bit sRGB
```

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
