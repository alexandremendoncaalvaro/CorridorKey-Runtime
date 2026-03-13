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
  distribution on Apple Silicon.
- **Windows Universal GPU next** as the current product track for predictable
  deployment on NVIDIA (RTX/GTX), AMD, and Intel hardware.
- **GUI and integration surfaces after that**, all consuming the same
  library-first runtime.

The project is architecture-ready beyond those tracks, but it does not market
all platforms and providers as equally validated.

The runtime contract is shared across those tracks. The model artifact is not.
Each product track may ship and validate a different converted model pack when
that is required to extract predictable performance from the target hardware.

### 1.1 What This Is

- A **native runtime engine** around the existing CorridorKey model.
- A **single-binary operational surface** with diagnostics, benchmark, and
  stable JSON/NDJSON contracts.
- A **library-first product boundary** that supports CLI, future GUI, sidecar,
  plugin, and pipeline integrations.
- A hardware-aware runtime with **curated provider tracks**, delivering
  predictable performance across diverse GPU tiers.
- A **platform-specific model-pack strategy** where Apple Silicon, Universal
  GPU (RTX/GTX/AMD), and CPU fallback use optimized converted artifacts under
  the same runtime contract.

### 1.2 What This Is NOT

- Not a retraining or new model architecture.
- Not a generic multi-backend framework; we prioritize a few high-quality,
  validated paths (TensorRT, CUDA, DirectML, MLX).
- Not a race for the "fastest benchmark" in isolation; performance is measured
  on end-to-end production reliability and stability.
- Not a promise of broad support without evidence; every hardware tier must
  pass explicit validation in the `doctor` tool.
- Not a replacement for the original project, but a focused contribution
  targeting native deployment and integration.

### 1.3 Intended Users

- **Technical local operators** who want native execution without Python.
- **Windows Universal GPU operators** who want predictable consumer-GPU
  deployment across NVIDIA, AMD, and Intel.
- **Integrators** who need a reusable engine for apps, plugins, or pipelines.

### 1.4 Goals

| Goal | Metric |
|------|--------|
| Native local execution | No Python, no pip, no venv |
| macOS production runtime | Corpus passes on 8 GB and 16 GB Apple Silicon tiers with an Apple-specific accelerated artifact path |
| Windows Universal GPU next | Product track covers TensorRT RTX, CUDA, and DirectML with CPU fallback |
| Operational predictability | `doctor`, `benchmark`, and `process --json` stay stable |
| Platform-specific optimization | Separate model packs per product track without changing the runtime contract |
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
│              Execution backends and model packs              │
│   ┌────────────┬───────────────┬──────────┬──────────────┐ │
│   │ Apple      │ Windows Univ. │ CPU      │ Secondary    │ │
│   │ model pack │ model pack    │ baseline │ exploratory  │ │
│   │ (MLX or    │ (ONNX / TRT / │ (ONNX)   │ paths        │ │
│   │ Core ML)   │ CUDA / DML)   │          │              │ │
│   └────────────┴───────────────┴──────────┴──────────────┘ │
└──────────────────────────────────────────────────────────┘
```

... [Rest of the SPEC remains technical, just ensuring headers and Tiers match] ...

## 4. Hardware Tiers

### 4.1 Tier Definitions

The runtime categorizes hardware into operational tiers to deliver predictable
performance.

| Tier | Target Hardware | Backend | Status |
|:---|:---|:---|:---|
| **Tier 1 (Pro)** | NVIDIA RTX 20xx/30xx/40xx | TensorRT | **Validated** |
| **Tier 2 (Plus)** | NVIDIA GTX 10xx/16xx | CUDA | **Integrated** |
| **Tier 3 (Base)** | AMD RX, Intel Arc, iGPU | DirectML | **Integrated** |
| **Fallback** | Any CPU (AVX2+) | ONNX CPU | **Baseline** |

### 4.3 Product Delivery Sequence

1. **Current release gate: macOS 14+ on Apple Silicon.**
   - MLX-first execution for high-performance native inference.
2. **Next product track: Windows 11 (Universal GPU).**
   - Primary path: **TensorRT RTX** for maximum NVIDIA performance.
   - Integrated support: **CUDA** for broad NVIDIA support and **DirectML** for
     AMD/Intel.
   - Distribution includes curated AI runtimes and optimized model packs.
3. **Later integration surfaces:** GUI, sidecar, plugin, and pipeline embedding.
