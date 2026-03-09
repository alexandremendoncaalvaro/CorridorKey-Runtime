# CorridorKey Runtime — Technical Specification

> **See also:**
> [GUIDELINES.md](GUIDELINES.md) — code standards, testing, linting |
> [ARCHITECTURE.md](ARCHITECTURE.md) — project structure, dependency layers

---

## 1. Vision

A high-performance, cross-platform C++ inference runtime for the
[CorridorKey](https://github.com/nikopueringer/CorridorKey) neural green screen
keyer. Takes the trained GreenFormer model and makes it
accessible on consumer hardware — from laptops with integrated graphics to
workstations with high-end GPUs.

### 1.1 What This Is

- A **C++ inference engine** that runs the existing CorridorKey model
- Optimized for **multiple hardware tiers** via quantization and adaptive resolution
- A single binary with **zero Python dependencies**
- Designed as a **library with CLI**, ready for future GUI integration

### 1.2 What This Is NOT

- Not a retraining or new model architecture
- Not a port of GVM or VideoMaMa (alpha hints come from external sources)
- Not a replacement for the original Python project (complementary)

### 1.3 Goals

| Goal | Metric |
|------|--------|
| Run on 8GB unified memory (MacBook Air M1) | Process 512² frames without crash |
| Run on 10GB GPU (RTX 3080) | Process 1024-2048² frames |
| Single binary distribution | No Python, no pip, no venv |
| Startup time | < 2 seconds to first frame |
| Quality parity with original | < 2% deviation on reference test set |
| Video support | Decode/encode MP4/MOV directly |

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
│                  ONNX Runtime 1.24+                        │
│   ┌──────────┬───────────┬──────────┬──────────────────┐ │
│   │ CoreML   │ TensorRT  │ DirectML │ CPU              │ │
│   │ EP       │ RTX EP    │ EP       │ EP               │ │
│   │ (macOS)  │ (NVIDIA)  │ (Win DX12│ (universal)      │ │
│   │          │           │  any GPU)│                  │ │
│   └──────────┴───────────┴──────────┴──────────────────┘ │
└──────────────────────────────────────────────────────────┘
```

### 2.2 Library-First Design

The core is a **static/shared library** (`libcorridorkey`) with a clean C++ API.
The CLI is a thin consumer of this library. This enables:

- Future GUI frontends (Qt, native macOS, Electron, etc.)
- Integration into other C++ applications (Nuke plugins, DaVinci Resolve, etc.)
- FFI bindings (Python, Rust, Go) if desired later

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
Supports hardware-accelerated decode where available (VideoToolbox on macOS,
NVDEC on NVIDIA).

#### 2.3.2 InferenceEngine

Wraps ONNX Runtime session management and execution.

Responsibilities:
- Load ONNX model (FP32, FP16, or INT8)
- Auto-detect best execution provider
- Handle resolution scaling (input → model resolution → output)
- Manage ONNX Runtime session options and optimization levels

Key design decisions:
- **One model format (ONNX)**, multiple execution providers
- **Resolution is adaptive**: query available memory at startup, pick the highest
  resolution that fits (512², 768², 1024², 1536², 2048²)
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
| `corridorkey_fp32.onnx` | ~300MB | Reference/validation | 100% (baseline) |
| `corridorkey_fp16.onnx` | ~150MB | GPUs with FP16 (RTX 3080, Apple Silicon) | ~99.5% |
| `corridorkey_int8.onnx` | ~75MB | Universal (CPU, low-VRAM GPU) | ~98-99% |

All variants hosted on HuggingFace. CLI auto-downloads on first run.

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
HIGH   │ 10+ GB dedicated   │ FP16      │ 1024-2048² │ 2-5 fps
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
│    EP: TensorRT RTX | Model: FP16 | Res: 1024-2048²            │
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

### 4.3 Resolution Selection Hierarchy

To ensure both ease of use and professional control, the runtime selects the processing resolution based on this strict hierarchy:

1. **User Override:** If `InferenceParams::target_resolution` is set (via CLI `--resolution`), it takes absolute precedence.
2. **Hardware Recommendation:** If target is `0` (Auto), the runtime queries available memory and selects the highest "safe" resolution (512, 768, 1024, 2048) for the detected Tier.
3. **Model Native:** If hardware detection is unavailable, the runtime falls back to the native resolution defined in the ONNX model metadata.

If the input image dimensions differ from the selected target resolution, the engine performs high-performance bilinear scaling before inference and scales the results back if necessary.

---

## 5. Tech Stack

### 5.1 Core Dependencies

| Component | Library | Version | Source |
|-----------|---------|---------|--------|
| Language | C++20 | (no modules) | — |
| Build | CMake | 3.28+ | — |
| Package manager | vcpkg | latest | manifest mode |
| ML inference | ONNX Runtime | 1.24+ | vcpkg |
| EXR I/O | OpenEXR + Imath | 3.4+ | vcpkg |
| PNG I/O (8-bit) | stb_image | latest | header-only, vendored |
| PNG I/O (16-bit) | libpng | 1.6+ | vcpkg |
| Video I/O | FFmpeg | 7.x | vcpkg |
| CLI | cxxopts | 3.x | vcpkg |
| Testing | Catch2 | 3.x | vcpkg |

### 5.2 Platform Matrix

| Platform | Compiler | EP Available |
|----------|----------|-------------|
| macOS 14+ (ARM64) | Apple Clang 15+ | CoreML, CPU |
| Windows 11 (x86_64) | MSVC 17.4+ | TensorRT RTX, CUDA, DirectML, CPU |
| Linux (x86_64) | GCC 12+ / Clang 16+ | TensorRT RTX, CUDA, CPU |
| Linux (ARM64) | GCC 12+ | CPU |

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
│   ├── workflows/                    # CI/CD pipelines
│   ├── PULL_REQUEST_TEMPLATE.md
│   └── ISSUE_TEMPLATE/
│       ├── bug_report.md
│       └── feature_request.md
│
├── cmake/                            # CMake modules and helpers
│
├── docs/
│   ├── SPEC.md                       # This document
│   └── GUIDELINES.md                 # Engineering standards
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
├── scripts/                          # Model export & optimization (Python)
│   ├── export_onnx.py
│   ├── optimize_model.py
│   ├── quantize_model.py
│   └── validate_model.py
│
└── vendor/                           # Vendored header-only libs
    ├── stb_image.h
    ├── stb_image_write.h
    └── CLI11.hpp
```

---

## 6. CLI Interface

### 6.1 Commands

```bash
# Process a video file
corridorkey process input.mp4 --alpha-hint hint.mp4 --output output.mp4

# Process an image sequence
corridorkey process ./Input/ --alpha-hint ./AlphaHint/ --output ./Output/

# Process a single frame
corridorkey process frame.exr --alpha-hint hint.png --output result.exr

# Show detected hardware and recommended settings
corridorkey info

# Download model files
corridorkey download [--variant fp32|fp16|int8|all]
```

### 6.2 Common Flags

```
--device <auto|cpu|cuda|coreml|directml>   Override device selection
--resolution <auto|512|768|1024|2048>       Override processing resolution
--model <path>                              Custom model path
--despill <0.0-10.0>                        Green spill removal (default: 1.0)
--no-despeckle                              Disable morphological cleanup
--despeckle-size <pixels>                   Cleanup threshold (default: 400)
--input-linear                              Input is linear (not sRGB)
--output-format <exr|png|mp4>               Output format override
--quality <int8|fp16|fp32>                  Model quality level
--verbose                                   Detailed logging
--benchmark                                 Print timing per frame
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

---

## 7. Implementation Phases

### Phase 0: Model Validation (Machine B — RTX 3080)

**Goal:** Prove the model exports and produces correct results.

Tasks:
- [x] Export GreenFormer to ONNX (dynamo=True, fallback to legacy)
- [ ] Validate ONNX output vs PyTorch (max error < 1e-5 per pixel)
- [x] Run onnxsim + transformer optimizer
- [ ] Quantize to FP16 and INT8
- [ ] Measure quality loss on 10+ reference frames
- [ ] Benchmark inference time (PyTorch vs ONNX FP32 vs FP16 vs INT8)

**Deliverable:** Validated ONNX models (FP32, FP16, INT8) + quality report.

**Why Machine B:** Only machine with enough VRAM (10GB) to load the PyTorch
model + ONNX Runtime simultaneously for comparison.

### Phase 1: C++ Skeleton (Machine A — Mac Mini M4)

**Goal:** Minimal working pipeline — load model, process one frame, save output.

Tasks:
- [x] CMake + vcpkg setup, builds on macOS
- [x] ONNX Runtime integration, load INT8 model
- [x] Minimal FrameIO (read PNG, write PNG)
- [x] InferenceEngine: load model, run single frame
- [x] Verify output matches Python reference
- [ ] Cross-compile check on Linux (Machine C or D)

**Deliverable:** Binary that processes one PNG frame + alpha hint → PNG output.

### Phase 2: Complete Pipeline (Machine A, tested on all)

**Goal:** Full feature parity with original CorridorKey inference.

Tasks:
- [x] PostProcess: port color_utils.py (sRGB, despill, despeckle, premultiply)
- [x] FrameIO: EXR read/write (OpenEXR 3.4)
- [x] FrameIO: Video decode/encode (FFmpeg)
- [x] Image sequence processing (directory of frames)
- [x] Auto-detection: hardware, tier, resolution, model variant
- [x] CLI: all commands and flags
- [x] Output structure (Matte/, FG/, Processed/, Comp/)
- [ ] Unit tests for all modules
- [ ] Test on all 6 machines

**Deliverable:** Feature-complete CLI binary.

### Phase 3: Optimization (All machines)

**Goal:** Maximize performance per platform.

Tasks:
- [ ] TensorRT RTX EP integration + cache (Machine B)
- [ ] CoreML EP tuning (Machines A, E, F)
- [ ] DirectML EP testing (Machine B or C with Windows)
- [ ] Multi-threaded frame pipeline (decode → infer → postprocess → encode)
- [ ] Memory profiling and optimization
- [ ] Resolution auto-scaling refinement
- [ ] Benchmark suite across all machines

**Deliverable:** Performance-optimized binary + benchmark report.

### Phase 4: Distribution

**Goal:** Easy to install, easy to use.

Tasks:
- [ ] Pre-built binaries (GitHub Releases) for macOS ARM64, Windows x64, Linux x64
- [ ] Model hosting on HuggingFace with auto-download
- [ ] README with installation and usage guide
- [ ] CI/CD (GitHub Actions): build + test on all platforms
- [ ] Homebrew formula (macOS), optional

**Deliverable:** Public release on GitHub.

---

## 8. Quality Assurance

Full testing strategy, static analysis configuration, pre-flight checks, and
git workflow are defined in **[GUIDELINES.md](GUIDELINES.md)**.

Summary of what's covered there:

### 8.1 Test Pyramid

| Level | Scope | Runs when |
|-------|-------|-----------|
| **Unit** | Single function/class, no I/O, no GPU | Every commit (pre-push hook) |
| **Regression** | Specific bug reproductions, never deleted | Every commit (pre-push hook) |
| **Integration** | Multi-module, real files, no GPU | Every push (pre-push hook + CI) |
| **E2E** | Full binary, real model, real hardware | Nightly CI + release |
| **Benchmarks** | Performance tracking, regression detection | Nightly CI |

### 8.2 Static Analysis & Pre-flight

| Gate | Tools | When |
|------|-------|------|
| **pre-commit** | clang-format, clang-tidy (changed files), file hygiene | Before every commit |
| **pre-push** | Full build, unit tests, integration tests, cppcheck | Before every push |
| **CI** | All of the above + cross-platform build + IWYU | Every PR |

### 8.3 Acceptance Criteria

| Criterion | Threshold |
|-----------|-----------|
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

## 9. Open Questions

Items to resolve during implementation:

1. **Hiera ONNX export** — Will dynamo export work, or do we need the legacy
   path? Only knowable by trying (Phase 0).

2. **INT8 quality on this specific model** — The 0.5-1.5% estimate is for
   general ViTs. The GreenFormer has a CNN refiner that may be more or less
   sensitive. Needs empirical testing (Phase 0).

3. **Apple Neural Engine utilization** — CoreML EP may or may not route ops to
   the ANE. Needs profiling on actual hardware (Phase 3).

4. **FFmpeg licensing** — FFmpeg with x264/x265 has GPL implications. May need
   to limit to LGPL-compatible codecs or use system FFmpeg.

5. **Alpha hint generation** — Should we include a simple traditional chroma
   key (color threshold) as a built-in alpha hint generator? Low effort, high
   usability gain. Deferred to post-Phase 2.

6. **Model auto-download** — HuggingFace Hub CLI vs custom HTTP download?
   Needs to work without Python.

---

## 10. References

- [CorridorKey Original](https://github.com/nikopueringer/CorridorKey)
- [ONNX Runtime 1.24](https://github.com/microsoft/onnxruntime/releases)
- [ONNX Runtime Execution Providers](https://onnxruntime.ai/docs/execution-providers/)
- [TensorRT RTX EP](https://onnxruntime.ai/docs/execution-providers/TensorRTRTX-ExecutionProvider.html)
- [CoreML EP](https://onnxruntime.ai/docs/execution-providers/CoreML-ExecutionProvider.html)
- [ONNX Runtime Quantization](https://onnxruntime.ai/docs/performance/model-optimizations/quantization.html)
- [OpenEXR 3.4](https://openexr.com/en/latest/news.html)
- [vcpkg](https://github.com/microsoft/vcpkg)
