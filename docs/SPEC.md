# CorridorKey Runtime — Technical Specification

> **See also:**
> [GUIDELINES.md](GUIDELINES.md) — code standards, testing, linting |
> [ARCHITECTURE.md](ARCHITECTURE.md) — project structure, dependency layers

---

## 1. Vision

A production-oriented native C++ runtime for the
[CorridorKey](https://github.com/nikopueringer/CorridorKey) neural green screen
keyer. The project packages model execution, diagnostics, validated model
catalogs, and stable machine-readable contracts into a runtime that can be
distributed, benchmarked, automated, and embedded on real hardware.

The product direction is explicit:

- **macOS first** to close runtime quality, diagnostics, and portable
  distribution on Apple Silicon
- **Windows RTX next** as the next product track for predictable deployment on
  consumer NVIDIA hardware
- **GUI and integration surfaces after that**, all consuming the same
  library-first runtime

The project is architecture-ready beyond those tracks, but it does not market
all platforms and providers as equally validated.

### 1.1 What This Is

- A **native runtime engine** around the existing CorridorKey model
- A **single-binary operational surface** with diagnostics, benchmark, and
  stable JSON/NDJSON contracts
- A **library-first product boundary** that supports CLI, future GUI, sidecar,
  plugin, and pipeline integrations
- A hardware-aware runtime with **curated provider tracks**, not a flat promise
  of parity across every backend

### 1.2 What This Is NOT

- Not a retraining or new model architecture
- Not a generic multi-backend framework with equal support for every provider
- Not a reimplementation of a full Python workflow inside this repository
- Not a first-party alpha-hint authoring suite in this phase

### 1.3 Intended Users

- **Technical local operators** who want native execution without Python
- **Windows RTX operators** who want predictable consumer-GPU deployment next
- **Integrators** who need a reusable engine for apps, plugins, or pipelines

### 1.4 Goals

| Goal | Metric |
|------|--------|
| Native local execution | No Python, no pip, no venv |
| macOS production runtime | Corpus passes on 8 GB and 16 GB Apple Silicon tiers |
| Windows RTX next | Product track is defined around TensorRT RTX and CPU fallback |
| Operational predictability | `doctor`, `benchmark`, and `process --json` stay stable |
| Quality parity with original model behavior | < 2% deviation on reference test set |
| Video support | Decode/encode MP4/MOV directly with hardware-aware defaults |

---

## 2. Architecture

### 2.1 High-Level Design

```
┌──────────────────────────────────────────────────────────┐
│                       CLI (cxxopts)                        │
│              args, config, progress, output                │
├──────────────────────────────────────────────────────────┤
│                    Public C++ API (libcorridorkey)         │
│   ┌─────────────┬──────────────┬────────────────────┐    │
│   │  FrameIO    │  Inference   │  PostProcess        │    │
│   │             │  Engine      │                     │    │
│   │ - EXR R/W   │ - load model │ - despill           │    │
│   │ - PNG R/W   │ - run frame  │ - despeckle         │    │
│   │ - Video R/W │ - auto-tier  │ - sRGB ↔ linear     │    │
│   │             │              │ - premultiply        │    │
│   └─────────────┴──────────────┴────────────────────┘    │
├──────────────────────────────────────────────────────────┤
│                     ONNX Runtime                           │
│   ┌──────────┬───────────┬──────────┬──────────────────┐ │
│   │ CoreML   │ TensorRT  │ CPU      │ Secondary        │ │
│   │ EP       │ RTX EP    │ EP       │ Windows paths    │ │
│   │ (macOS   │ (Windows  │ (compat. │ DirectML / WinML)│ │
│   │  now)    │  RTX next)│  layer)  │                  │ │
│   └──────────┴───────────┴──────────┴──────────────────┘ │
└──────────────────────────────────────────────────────────┘
```

### 2.2 Library-First Design

The core is a **static/shared library** (`libcorridorkey`) with a clean C++ API.
The CLI is a thin consumer of this library. This enables:

- Future GUI frontends (Qt, native macOS, Electron, etc.)
- Integration into other C++ applications (Nuke plugins, DaVinci Resolve, etc.)
- FFI bindings (Python, Rust, Go) if desired later

Library-first is a product decision, not just a code organization preference.
It is what allows the runtime to serve CLI automation today while staying ready
for sidecars, GUIs, plugins, and pipeline embedding later without duplicating
business logic.

```cpp
// Public API sketch (frontend-agnostic, thread-safe, robust)
namespace corridorkey {

enum class ErrorCode {
    Success = 0,
    ModelLoadFailed,
    InferenceFailed,
    IoError,
    InvalidParameters,
    Cancelled
};

struct Error {
    ErrorCode code;
    std::string message;
};

// Result type for robust error handling
template<typename T>
using Result = std::expected<T, Error>;

struct DeviceInfo {
    std::string name;
    int64_t available_memory_mb;
    Backend backend;
};

struct InferenceParams {
    int target_resolution = 0;
    float despill_strength = 1.0f;
    bool auto_despeckle = true;
    int despeckle_size = 400;
    float refiner_scale = 1.0f;
    bool input_is_linear = false;
};

// Progress callback for long-running tasks
using ProgressCallback = std::function<bool(float progress, const std::string& status)>;

class Engine {
public:
    // Factory method for creating the engine (RAII)
    static Result<std::unique_ptr<Engine>> create(const std::filesystem::path& model_path,
                                                 DeviceInfo device = auto_detect());

    ~Engine();

    // Core inference on raw memory buffers
    Result<FrameResult> process_frame(const Image& rgb, const Image& alpha_hint,
                                     const InferenceParams& params = {});

    // Batch processing with progress reporting and cancellation
    Result<void> process_sequence(const std::vector<std::filesystem::path>& inputs,
                                 const std::vector<std::filesystem::path>& alpha_hints,
                                 const std::filesystem::path& output_dir,
                                 ProgressCallback on_progress = nullptr);

    int recommended_resolution() const;

private:
    class Impl; // PIMPL pattern
    std::unique_ptr<Impl> m_impl;

    Engine(); // Private constructor used by factory
};

DeviceInfo auto_detect();
std::vector<DeviceInfo> list_devices();

}  // namespace corridorkey
```

### 2.3 Module Breakdown

#### 2.3.1 FrameIO

Responsible for all image and video decoding/encoding.

| Format | Library | Read | Write | Notes |
|--------|---------|------|-------|-------|
| EXR 16-bit half | OpenEXR 3.4 | Yes | Yes | Primary VFX format |
| EXR 32-bit float | OpenEXR 3.4 | Yes | Yes | High-precision input |
| PNG 8-bit | stb_image | Yes | Yes | Preview/comp output |
| PNG 16-bit | libpng | Yes | Yes | High-quality stills |
| MP4/MOV/AVI/MKV | FFmpeg 7.x | Yes | Yes | Video decode/encode |

Video I/O uses FFmpeg's C API with a thin RAII wrapper (~300 lines).
On macOS, the portable default output path is H.264 in MP4, using
VideoToolbox encoding when available and falling back to software encoders
without changing the user-facing workflow.

#### 2.3.2 InferenceEngine

Wraps ONNX Runtime session management and execution.

Responsibilities:
- Load ONNX model (FP32, FP16, or INT8)
- Auto-detect best execution provider
- Handle resolution scaling (input → model resolution → output)
- Manage ONNX Runtime session options and optimization levels

Key design decisions:
- **One model format (ONNX)**, with execution-provider tracks curated by phase
- **Resolution is adaptive**: query available memory at startup and select among
  currently supported targets (512², 768², 1024²)
- **Warm-up run**: first frame triggers JIT compilation (TensorRT RTX EP caches
  this for subsequent runs)

#### 2.3.3 PostProcess

Port of `CorridorKeyModule/core/color_utils.py` to C++.

Functions:
- `srgb_to_linear(float)` / `linear_to_srgb(float)` — piecewise sRGB transfer
- `premultiply(rgba)` / `unpremultiply(rgba)`
- `despill(rgb, alpha, strength)` — luminance-preserving green spill removal
- `despeckle(alpha, size)` — morphological connected-component cleanup
- `composite_over_checker(rgba)` — preview generation

All operations work on raw float buffers. SIMD-friendly layout (SOA or
interleaved, TBD based on profiling).

---

## 3. Model Pipeline

### 3.1 Export from PyTorch

```
GreenFormer (PyTorch, .pth)
    │
    ├─ Primary: torch.onnx.export(dynamo=True, opset=17)
    │  (handles SDPA and modern ops better)
    │
    └─ Fallback: torch.onnx.export(opset=17)
       with torch.nn.attention.sdpa_kernel(MATH)
       (if Hiera's windowed attention breaks dynamo export)
    │
    ▼
ONNX FP32 (~300MB)
```

### 3.2 Optimization Pipeline

```
ONNX FP32
    ↓ onnxsim (graph simplification, constant folding)
ONNX FP32 simplified
    ↓ onnxruntime.transformers.optimizer(model_type="vit")
    ↓ (attention fusion, LayerNorm fusion, GELU fusion,
    ↓  skip-connection fusion, embed-LayerNorm fusion)
ONNX FP32 optimized
    ↓ quantize_static(QDQ format, per_channel=True,
    ↓                  exclude=[LayerNorm], 200 calibration frames)
ONNX INT8 QDQ (~75MB)
    ↓ optional: float16 conversion for GPU targets
ONNX FP16 (~150MB)
```

### 3.3 Model Variants Shipped

| Variant | Size | Target | Quality |
|---------|------|--------|---------|
| `corridorkey_fp32_512.onnx` / `corridorkey_fp32_768.onnx` / `corridorkey_fp32_1024.onnx` | ~276-297MB | Reference/validation | 100% (baseline) |
| `corridorkey_fp16_512.onnx` / `corridorkey_fp16_768.onnx` / `corridorkey_fp16_1024.onnx` | ~138-149MB | GPUs with FP16 support | ~99.5% |
| `corridorkey_int8_512.onnx` / `corridorkey_int8_768.onnx` / `corridorkey_int8_1024.onnx` | ~76-97MB | Universal (CPU, low-VRAM GPU) | ~98-99% |

Model files are downloaded explicitly through `corridorkey download`.

### 3.4 Hiera-Specific Concerns

The GreenFormer uses Hiera (from timm) as backbone. Known ONNX export challenges:

- **Windowed attention** with `torch.roll` → large ONNX subgraphs
- **Mask unit attention** with dynamic slicing → may fail with dynamo export
- **4-channel input patch** (RGB + alpha hint) → custom patch embedding

Mitigation:
1. Try dynamo export first (cleaner graph)
2. Fall back to legacy export with SDPA disabled
3. If both fail, trace with `torch.jit.trace` and export from trace
4. Validate output numerically against PyTorch (max absolute error < 1e-5)

---

## 4. Hardware Tiers

### 4.1 Tier Definitions

```
Tier   │ VRAM/RAM Available │ Model     │ Resolution │ Expected FPS
───────┼────────────────────┼───────────┼────────────┼─────────────
HIGH   │ 10+ GB dedicated   │ FP16      │ 1024²      │ 2-5 fps
MEDIUM │ 12-16 GB unified   │ INT8      │ 512-768²   │ 1-3 fps
LOW    │ 6-8 GB             │ INT8      │ 512²       │ 0.5-2 fps
CPU    │ 16+ GB RAM         │ INT8      │ 512²       │ 0.1-0.5 fps
```

### 4.2 Hardware Map (Development & Test)

```
┌─────────────────────────────────────────────────────────────────┐
│ A: Mac Mini M4 (16GB)          → MEDIUM tier                    │
│    EP: CoreML | Model: INT8 | Res: 512-768²                    │
│    Role: Primary development machine                            │
├─────────────────────────────────────────────────────────────────┤
│ B: PC RTX 3080 (10GB VRAM, 32GB RAM) → HIGH tier               │
│    EP: TensorRT RTX | Model: FP16 | Res: 1024²                 │
│    Role: NVIDIA testing, model export & quantization            │
├─────────────────────────────────────────────────────────────────┤
│ C: PC RX 480 (8GB, Bluefin Linux) → CPU tier                   │
│    EP: CPU (ROCm/MIGraphX unsupported for Polaris)              │
│    Model: INT8 | Res: 512²                                      │
│    Role: CPU-only Linux testing                                 │
├─────────────────────────────────────────────────────────────────┤
│ D: Laptop Dell (Intel UHD 620, Pop!OS) → CPU tier               │
│    EP: CPU | Model: INT8 | Res: 512²                            │
│    Role: Worst-case hardware testing                            │
├─────────────────────────────────────────────────────────────────┤
│ E: MacBook M1 Pro (16GB) → MEDIUM tier                          │
│    EP: CoreML | Model: INT8 | Res: 512-768²                    │
│    Role: Older Apple Silicon testing                            │
├─────────────────────────────────────────────────────────────────┤
│ F: MacBook Air M1 (8GB) → LOW tier (FLOOR TEST)                │
│    EP: CoreML | Model: INT8 | Res: 512²                        │
│    Role: If it runs here, it runs anywhere                      │
└─────────────────────────────────────────────────────────────────┘
```

### 4.3 Product Delivery Sequence

The product does not advance as a generic cross-platform matrix. It advances as
curated delivery tracks.

1. **Current release gate:** macOS 14+ on Apple Silicon.
   - CoreML is the primary accelerated path.
   - CPU is the mandatory compatibility fallback.
   - `int8_512` and `int8_768` are the only model variants considered validated
     and packageable for the portable macOS bundle.
   - Inputs larger than the selected model resolution must use tiling before
     GUI work begins.
2. **Next product track:** Windows 11 on NVIDIA RTX hardware.
   - TensorRT RTX is the intended primary Windows path.
   - CPU remains the mandatory compatibility fallback.
   - Windows delivery is judged by installation predictability, diagnostics,
     cache behavior, and validated hardware tiers, not by broad backend count.
3. **Later integration surfaces:** GUI, sidecar, plugin, and pipeline embedding
   consume the same library-first runtime once the current and next tracks are
   stable.
4. **Later architectural targets:** Linux and secondary provider paths stay
   architecture-ready until the first two product tracks are validated.

### 4.4 Validation Corpus

Release validation uses a fixed corpus already stored in the repository.

| Asset | Expected mode |
|------|---------------|
| `assets/corridor.png` | smoke/simple path |
| first 20 frames from `assets/image_samples/thelikelyandunfortunatedemiseofyourgpu/` | standard inference |
| `assets/video_samples/greenscreen_1769569137.mp4` | standard inference |
| `assets/video_samples/greenscreen_1769569320.mp4` | standard inference |
| `assets/video_samples/100745-video-2160.mp4` | tiled high-resolution |
| `assets/video_samples/mixkit-girl-dancing-with-her-earphones-on-a-green-background-28306-4k.mp4` | stress/performance run |

### 4.5 Resolution Selection Hierarchy

To ensure both ease of use and professional control, the runtime selects the processing resolution based on this strict hierarchy:

1. **User Override:** If `InferenceParams::target_resolution` is set (via CLI `--resolution`), it takes absolute precedence.
2. **Hardware Recommendation:** If target is `0` (Auto), the application layer maps the selected device profile to a safe resolution. On release-gated macOS systems that means `512` for 8 GB-class machines and `768` for 16 GB-class machines.
3. **Core Fallback:** If no profile is available, the core session uses a conservative fallback resolution (`512`).

If the input image dimensions differ from the selected target resolution, the engine performs high-performance bilinear scaling before inference and scales the results back if necessary.

---

## 5. Tech Stack

### 5.1 Core Dependencies

| Component | Library | Version | Source |
|-----------|---------|---------|--------|
| Language | C++20 | (no modules) | — |
| Build | CMake | 3.28+ | — |
| Package manager | vcpkg | latest | manifest mode |
| ML inference | ONNX Runtime | vendored 1.16.3 on macOS, vcpkg on non-macOS | mixed |
| EXR I/O | OpenEXR + Imath | 3.4+ | vcpkg |
| PNG I/O (8-bit) | stb_image | latest | header-only, vendored |
| PNG I/O (16-bit) | libpng | 1.6+ | vcpkg |
| Video I/O | FFmpeg | 8.x | vcpkg |
| CLI | cxxopts | 3.x | vcpkg |
| Testing | Catch2 | 3.x | vcpkg |

### 5.2 Platform Matrix

| Platform | Compiler | Primary provider path | Product status |
|----------|----------|-----------------------|----------------|
| macOS 14+ (ARM64) | Apple Clang 15+ | CoreML, CPU fallback | Current release gate |
| Windows 11 + NVIDIA RTX (x86_64) | MSVC 17.4+ | TensorRT RTX, CPU fallback | Next product track |
| Windows 11 generic (x86_64) | MSVC 17.4+ | CPU; DirectML/WinML secondary | Exploratory, not the primary Windows product path |
| Linux (x86_64) | GCC 12+ / Clang 16+ | CPU/CUDA architecture-ready | Deferred until macOS + Windows RTX are stable |
| Linux (ARM64) | GCC 12+ | CPU | Deferred |

Provider support is curated by phase. The product does not promise equivalent
validation across all ONNX Runtime execution providers.
For Windows, DirectML and WinML remain secondary paths in this specification;
they are not the primary product promise for the next delivery track.

### 5.3 Project Layout

```
CorridorKey-Runtime/
├── CMakeLists.txt                    # Root build config
├── CMakePresets.json                 # Cross-platform build presets
├── vcpkg.json                        # Dependency manifest
├── .clang-format                     # Code formatting rules
├── .clang-tidy                       # Static analysis config
├── .pre-commit-config.yaml           # Git hook definitions
├── .gitignore
├── LICENSE                           # CC BY-NC-SA 4.0
├── README.md                         # User-facing documentation
├── CONTRIBUTING.md                   # Developer onboarding
│
├── .github/
│   ├── workflows/                    # CI/CD workflows (optional, may be empty)
│   ├── PULL_REQUEST_TEMPLATE.md
│   └── ISSUE_TEMPLATE/
│       ├── bug_report.md
│       └── feature_request.md
│
├── cmake/                            # CMake modules and helpers
│
├── docs/
│   ├── SPEC.md                       # This document
│   ├── GUIDELINES.md                 # Engineering standards
│   ├── ARCHITECTURE.md               # Structural rules
│   └── brainstorm/                   # Exploratory notes (non-normative)
│
├── include/
│   └── corridorkey/                  # Public API headers
│       ├── engine.hpp                # Engine class
│       ├── types.hpp                 # Image, DeviceInfo, InferenceParams
│       ├── frame_io.hpp              # FrameIO interface
│       └── version.hpp               # Version macros
│
├── src/
│   ├── cli/
│   │   └── main.cpp                  # CLI entry point (thin)
│   ├── core/
│   │   ├── engine.cpp                # Engine implementation
│   │   ├── inference_session.cpp     # ONNX Runtime wrapper
│   │   └── device_detection.cpp      # Hardware detection + tier
│   ├── frame_io/
│   │   ├── exr_io.cpp               # OpenEXR read/write
│   │   ├── png_io.cpp               # PNG read/write
│   │   └── video_io.cpp             # FFmpeg decode/encode
│   └── post_process/
│       ├── color_utils.cpp           # sRGB, linear, premultiply
│       ├── despill.cpp               # Green spill removal
│       └── despeckle.cpp            # Morphological cleanup
│
├── tests/
│   ├── unit/                         # Fast, no I/O, no GPU
│   ├── integration/                  # Multi-module, real files
│   ├── e2e/                          # Full binary, real model
│   └── fixtures/                     # Reference files for tests
│
├── models/                           # ONNX models (gitignored, downloaded)
│
├── scripts/                          # Project automation scripts (hooks, packaging, checks)
│
├── tools/
│   └── model_exporter/               # Model export & optimization (Python)
│       ├── export_onnx.py
│       ├── optimize_model.py
│       └── quantize_model.py
│
└── vendor/                           # Vendored third-party assets
    ├── stb_image.h
    ├── stb_image_write.h
    └── onnxruntime/
```

---

## 6. CLI Interface

### 6.1 Commands

```bash
# Process a video file
corridorkey process --input input.mp4 --alpha-hint hint.mp4 --output output.mp4 --model models/corridorkey_int8_768.onnx

# Process an image sequence
corridorkey process --input ./Input/ --alpha-hint ./AlphaHint/ --output ./Output/ --model models/corridorkey_int8_768.onnx

# Process a single frame
corridorkey process --input frame.exr --alpha-hint hint.png --output result.exr --model models/corridorkey_int8_512.onnx

# Show detected hardware and recommended settings
corridorkey info

# Download model files (512/768/1024 for selected variant)
corridorkey download [--variant fp32|fp16|int8|all]

# Run diagnostics over available devices and model files
corridorkey doctor [--model models/corridorkey_int8_512.onnx]

# Run a synthetic benchmark using a specific model
corridorkey benchmark --model models/corridorkey_int8_512.onnx

# Run a real-workload benchmark against a source file
corridorkey benchmark --json --input input.mp4 --output benchmark_output.mp4 --model models/corridorkey_int8_768.onnx

# Inspect the stable model and preset catalogs
corridorkey models
corridorkey presets

# Consume the stable structured contracts
corridorkey info --json
corridorkey process --json --input input.mp4 --output output.mp4 --model models/corridorkey_int8_768.onnx
```

### 6.2 Common Flags

```
--device <auto|cpu|cuda|coreml|dml|index>   Override device selection
--resolution <0|512|768|1024>               Override processing resolution
--model <path>                              Custom model path
--despill <0.0-1.0>                         Green spill removal (default: 1.0)
--no-despeckle                              Disable morphological cleanup
--batch-size <int>                          Number of frames per inference call
--tiled                                     Enable tiled inference for large frames
--json                                      Emit structured JSON output
```

### 6.3 Output Structure (Image Sequence Mode)

```
Output/
├── Matte/          # Alpha channel, EXR 16-bit linear
├── FG/             # Foreground straight color, EXR 16-bit linear
├── Processed/      # Premultiplied RGBA, EXR 16-bit linear
└── Comp/           # Preview on checkerboard, PNG 8-bit sRGB
```

Matches the original CorridorKey output structure for compatibility.

### 6.4 Structured CLI Contract

- `info`, `doctor`, `benchmark`, `models`, and `presets` return one JSON
  document when `--json` is requested.
- `process --json` emits NDJSON events in this order as needed:
  - `job_started`
  - `backend_selected`
  - `progress`
  - `warning`
  - `artifact_written`
  - `completed`
  - `failed`
  - `cancelled`
- `backend_selected` includes the chosen backend and carries structured fallback
  information when `auto` or an explicit backend request drops to CPU.
- Terminal job events (`completed`, `failed`, `cancelled`) include aggregated
  stage timings so CLI automation and the future GUI can inspect where time was
  spent.
- `benchmark --json` supports:
  - `synthetic` mode for model-only throughput/latency checks
  - `workload` mode for full pipeline measurement on a real input
- `benchmark --json` reports `stage_timings` for engine creation, warmup,
  per-frame inference, tiling, decode, encode, and full benchmark duration.

### 6.5 Operational Predictability

Operational predictability is part of the product surface.

- `doctor` verifies runtime health, available devices, model presence, and
  default bundle expectations.
- `benchmark` provides reproducible synthetic and workload-based measurements.
- `models` and `presets` expose the validated catalogs instead of expecting
  callers to hard-code product assumptions.
- Hardware tiers are explicit and stable enough that `auto` behavior is
  explainable per platform track.
- Stage-level timings are emitted so users and future GUIs can identify
  bottlenecks without reverse-engineering log text.

### 6.6 AlphaHint Strategy

AlphaHint is treated as an integration contract, not as a hidden internal
detail.

- Supported hint inputs are:
  - a single hint frame for a single image
  - a directory of hint frames for a directory or image sequence
  - a hint video aligned with an input video
- If no hint is provided, the runtime generates a rough matte internally.
- External hints remain the preferred path when the user needs higher-quality
  or more controlled results.
- Rough matte fallback exists for smoke tests, low-friction usage, and simpler
  cases, accepting that it may trade quality and control for convenience.
- First-party hint authoring is out of scope for this phase.

---

## 7. Implementation Scope and Priorities

### 7.1 Implemented Scope

The current codebase already provides:

- CMake + vcpkg project setup with debug/release presets.
- ONNX Runtime integration with execution-provider selection paths.
- Frame I/O for EXR, PNG, and video decode/encode through FFmpeg.
- Full processing pipeline for single frames, image sequences, and video.
- Post-processing stack (sRGB/linear conversion, despill, despeckle, compositing).
- Hardware-aware resolution strategy in the application layer.
- Stable runtime capability, model catalog, and preset catalog contracts.
- CLI commands for processing, diagnostics, benchmarking, catalogs, and model download.
- Structured JSON/NDJSON output for automation and the future sidecar bridge.
- Stage-level timing telemetry for synthetic benchmarks and real workloads.
- Unit, integration, e2e, and regression test targets.

### 7.2 Priority Work Queue

Near-term priorities are:

1. Close the macOS production-runtime gates around fallback, tiling, observability, and portable distribution.
2. Use the validation corpus and benchmark telemetry to tune quality vs throughput on the current Apple Silicon track.
3. Define and document the Windows RTX product track around TensorRT RTX, CPU fallback, installation expectations, and validated tiers.
4. Keep the bridge-facing JSON/NDJSON contract stable so future GUI and integration surfaces can rely on the same runtime behavior.
5. Expand regression coverage for provider fallback, hint handling, tiling, packaging, and hardware-tier policy.

---

## 8. Quality Assurance

Full testing strategy, static analysis configuration, pre-flight checks, and
git workflow are defined in **[GUIDELINES.md](GUIDELINES.md)**.

Summary of what's covered there:

### 8.1 Test Pyramid

| Level | Scope | Runs when |
|-------|-------|-----------|
| **Unit** | Single function/class, no I/O, no GPU | Every push (pre-push hook) |
| **Regression** | Specific bug reproductions, never deleted | Every push (pre-push hook) |
| **Integration** | Multi-module, real files, no GPU | Every push (pre-push hook) |
| **E2E** | Full binary, real model, real hardware | On demand / release validation |
| **Benchmarks** | Performance tracking, regression detection | On demand |

### 8.2 Static Analysis & Pre-flight

| Gate | Tools | When |
|------|-------|------|
| **pre-commit** | clang-format, file hygiene | Before every commit |
| **pre-push** | Full build, unit tests, integration tests | Before every push |
| **CI** | Optional cross-platform build and test workflows | When configured |

### 8.3 Acceptance Criteria

| Criterion | Threshold |
|-----------|-----------|
| macOS corpus completes without crash | 100% |
| CoreML auto-selection falls back to CPU with structured reason | 100% |
| Portable bundle runs `info`, `doctor`, `models`, and `presets` outside the build tree | 100% |
| `benchmark --json` and terminal `process --json` events expose stable stage timings | 100% |
| Tiled 4K runs complete without visible seam artifacts in validation review | Required |
| Pixel-level accuracy vs Python (FP32 model) | max abs error < 1e-4 |
| Pixel-level accuracy vs Python (FP16 model) | max abs error < 1e-2 |
| Pixel-level accuracy vs Python (INT8 model) | max abs error < 5e-2 |
| No crashes on any of the 6 test machines | 100% |
| Video processing completes without frame drops | 100% |
| Memory usage stays within tier limits | No OOM |
| Performance regression vs previous release | < 20% slowdown |

### 8.4 Code Quality Standards

- Clean Architecture with strict dependency direction
- SOLID principles enforced through design and linting
- Object Calisthenics (adapted for C++) as guiding constraints
- clang-tidy with WarningsAsErrors for critical checks
- Cognitive complexity threshold: 15 per function
- Max file size guidelines: ~200 lines .cpp, ~100 lines .hpp

---

## 9. Product Roadmap

1. **Phase 1 — macOS production runtime:** validate CoreML-first execution,
   mandatory CPU fallback, tiling behavior, structured diagnostics, benchmark
   telemetry, and portable bundle acceptance on Apple Silicon.
2. **Phase 2 — Windows RTX track:** define and implement the TensorRT RTX-based
   Windows product path with CPU fallback, installation guidance, validated
   tiers, and equivalent diagnostics.
3. **Phase 3 — integration surfaces:** keep the sidecar contract stable and
   build GUI, plugin, and pipeline-facing surfaces as thin consumers of the
   same runtime.
4. **Phase 4 — broader platform expansion:** extend validation to Linux and
   secondary provider paths only after the macOS and Windows RTX tracks are
   stable.

---

## 10. References

- [CorridorKey Original](https://github.com/nikopueringer/CorridorKey)
- [ONNX Runtime Releases](https://github.com/microsoft/onnxruntime/releases)
- [ONNX Runtime Install Docs](https://onnxruntime.ai/docs/install/)
- [ONNX Runtime Execution Providers](https://onnxruntime.ai/docs/execution-providers/)
- [ONNX Runtime EP Build Matrix](https://onnxruntime.ai/docs/build/eps.html)
- [TensorRT RTX EP](https://onnxruntime.ai/docs/execution-providers/TensorRTRTX-ExecutionProvider.html)
- [CoreML EP](https://onnxruntime.ai/docs/execution-providers/CoreML-ExecutionProvider.html)
- [ONNX Runtime Quantization](https://onnxruntime.ai/docs/performance/model-optimizations/quantization.html)
- [OpenEXR 3.4](https://openexr.com/en/latest/news.html)
- [vcpkg](https://github.com/microsoft/vcpkg)
