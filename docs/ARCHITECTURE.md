# Project Architecture

> This document defines the project structure, the purpose of every directory,
> and the rules that govern where code lives. It is the **single source of
> truth** for structural decisions. Any deviation must be discussed and
> approved in a PR before it happens.
>
> **See also:**
> [SPEC.md](SPEC.md) — technical specification |
> [GUIDELINES.md](GUIDELINES.md) — code standards, testing, linting

---

## 1. Architectural Philosophy

This project exists to ship CorridorKey inference as a **native, distributable,
and integrable runtime** for real hardware. The architecture is designed around
predictable operations, low-friction distribution, and reusable surfaces rather
than around a generic multi-backend abstraction.

We follow a modular architecture that strictly separates the **Core** (business
logic) from the **Application** (orchestration) and **Interfaces** (CLI, GUI,
sidecar, plugin hosts, and similar surfaces).

**Key Principles:**

1.  **Production Runtime First:** Native execution, diagnostics, and
    distribution quality matter more than broad backend claims.
2.  **Library First:** The engine is the product boundary. CLI, GUI, sidecar,
    plugin, and pipeline integrations consume the same runtime instead of
    reimplementing behavior.
3.  **CLI As Operational Contract:** Any feature exposed to a future GUI or
    embedded surface must first exist in the CLI or public library contract.
4.  **Curated Platform Tracks:** macOS Apple Silicon is the current release
    gate. Windows RTX is the next product track. Other platform paths stay
    architecture-ready until validated.
5.  **Shared Runtime, Curated Artifacts:** The runtime contract stays stable
    across product tracks, while model artifacts and backend adapters may
    diverge by platform when that is necessary for performance and
    predictability.
6.  **Bridge, Not Duplication:** User interfaces consume the runtime through
    stable contracts. Business logic stays in Core/App.

---

## 2. Structural Layers

This project follows the **Pitchfork layout** (separated variant) adapted for
our needs, combined with **Clean Architecture** layering.

### Layer 1: Core (`src/core`, `src/frame_io`, `src/post_process`)
**Responsibility:** The engine, math, and I/O capabilities.
*   **Inference:** Backend adapters and session management for the currently
    approved product tracks.
*   **Hardware:** Device detection and provider selection within the current product tracks.
*   **Video Pipeline:** FFmpeg integration for direct memory processing.
*   **Math:** Color space conversion, despill, despeckle algorithms.
*   **Hint Handling:** External hint ingestion plus rough-matte fallback generation.

**Rules:**
*   Must not depend on CLI or App layers.
*   Must not print to stdout/stderr (use callbacks/results).
*   Must return rich error types (`Result<T>`).
*   Must treat model artifacts as platform-curated inputs rather than assuming a
    single serialized model format is optimal everywhere.

### Layer 2: Application (`src/app`)
**Responsibility:** Orchestration of the Core into coherent jobs.
*   Job definitions and validation.
*   Preset management.
*   Progress tracking and reporting.
*   Stage timing aggregation and benchmark reporting.
*   Structured diagnostics, capabilities, and model/preset catalogs.
*   High-level strategy selection (e.g., Tiling vs Standard inference).
*   Platform-track selection between compatible model packs and fallback paths.

### Layer 3: Interfaces (`src/cli`)
**Responsibility:** Interacting with the user.
*   **CLI:** The current primary interface. Parses arguments, configures the engine, and formats output.
*   **Bridge contract:** JSON commands and NDJSON job events, including fallback diagnostics and stage timings, that a future Tauri sidecar can consume without parsing human-readable text.
*   Additional interfaces (GUI, sidecar adapters, plugin hosts, pipeline embeddings) must remain thin clients over the same App/Core contracts.

---

## 3. Directory Map

### Root (`/`)

Configuration and project-level documentation only.

| File | Purpose | Rule |
|------|---------|------|
| `CMakeLists.txt` | Root build definition | Defines the library and links subdirectories. |
| `vcpkg.json` | Dependency manifest | Every dependency has a `$comment` explaining why it exists. |
| `README.md` | User-facing documentation | Installation, usage, hardware support. |
| `CONTRIBUTING.md` | Developer onboarding | Setup, standards summary, PR process. |

### `include/corridorkey/`

**Public API headers only.** These are the headers that external consumers of
the library would include.

```
include/corridorkey/
├── engine.hpp          Engine class — the main entry point
├── types.hpp           Value types: Image, DeviceInfo, InferenceParams, runtime contracts
├── frame_io.hpp        FrameIO interface
└── version.hpp         Version macros
```

### `src/`

All implementation code. Organized by domain.

```
src/
├── app/                Application orchestration layer
│   ├── job_orchestrator.cpp
│   └── runtime_contracts.cpp
├── cli/                CLI application (thin consumer of the library)
│   └── main.cpp
├── common/             Shared internal utilities (no external deps)
│   ├── srgb_lut.hpp
│   └── stage_profiler.hpp
├── core/               Inference engine, device detection
│   ├── engine.cpp
│   ├── inference_session.cpp
│   └── device_detection.cpp
├── frame_io/           Image and video read/write (FFmpeg, OpenEXR)
│   ├── video_io.cpp
│   └── exr_io.cpp
└── post_process/       Color math, despill, despeckle
    ├── color_utils.cpp
    └── despill.cpp
```

### `tests/`

All test code, organized by test level (pyramid).

```
tests/
├── unit/               Fast, isolated logic tests (Catch2)
├── integration/        Multi-module tests (Roundtrip video/image)
├── e2e/                Full binary tests with real models
├── regression/         Bug reproduction tests
└── fixtures/           Reference files
```

### `tools/`

Isolated auxiliary tools (Python, Shell).

```
tools/
├── model_exporter/     Python scripts to export PyTorch models to ONNX
│   ├── export_onnx.py
│   ├── optimize_model.py
│   └── quantize_model.py
└── hint_generator/     Reference auto-hint implementation
```

**Rule:** Tools are standalone and must not leak dependencies into the main C++ build.

---

## 4. Engineering Standards (2026)

1. **Zero-Copy Data Flow:** We use `std::span` (via the `Image` struct) to pass data between modules without copying.
2. **SIMD Alignment:** All image buffers are 64-byte aligned for AVX-512/NEON efficiency.
3. **Data-Oriented Design:** We prioritize linear memory access patterns for cache efficiency.
4. **No Exceptions in Core:** We use `Result<T>` (std::expected style) for predictable error handling in the library path.

---

## 5. Adding New Code — Decision Tree

```
Is it a public API type or function?
  YES → include/corridorkey/
  NO ↓

Is it inference logic or hardware management?
  YES → src/core/
  NO ↓

Is it file I/O (video/image)?
  YES → src/frame_io/
  NO ↓

Is it pixel math (color, filters)?
  YES → src/post_process/
  NO ↓

Is it CLI/Interface logic?
  YES → src/cli/
  NO ↓

Is it a Python helper?
  YES → tools/
  NO ↓

Is it a test?
  Pure logic   → tests/unit/
  Pipeline I/O → tests/integration/
  NO ↓

→ STOP. Discuss in an issue.
```
