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

This project aims to be a **robust, portable, and usable runtime** for high-quality green screen keying on consumer hardware. We follow a modular architecture that strictly separates the **Core** (business logic) from the **Application** (orchestration) and **Interfaces** (CLI, TUI, etc.).

**Key Principles:**

1.  **Core Isolation:** Logic for inference, video processing, and math must never know about the user interface.
2.  **CLI First:** The CLI is the primary contract. Any feature exposed to a future GUI must first exist in the CLI.
3.  **Progressive Complexity:** We build solid foundations (Core -> App -> CLI) before adding fancy interfaces (TUI -> GUI).
4.  **Real Modularization:** Components must be reusable. The core engine should be linkable into other C++ applications (like Blender or OBS plugins) without carrying CLI baggage.

---

## 2. Structural Layers

This project follows the **Pitchfork layout** (separated variant) adapted for
our needs, combined with **Clean Architecture** layering.

### Layer 1: Core (`src/core`, `src/frame_io`, `src/post_process`)
**Responsibility:** The engine, math, and I/O capabilities.
*   **Inference:** ONNX Runtime wrapper, session management.
*   **Hardware:** Device detection, backend selection (CPU/GPU/NPU).
*   **Video Pipeline:** FFmpeg integration for direct memory processing.
*   **Math:** Color space conversion, despill, despeckle algorithms.
*   **Auto-Hinting:** Heuristic alpha generation.

**Rules:**
*   Must not depend on CLI or App layers.
*   Must not print to stdout/stderr (use callbacks/results).
*   Must return rich error types (`Result<T>`).

### Layer 2: Application (`src/app` - *Planned*)
**Responsibility:** Orchestration of the Core into coherent jobs.
*   Job definitions and validation.
*   Preset management.
*   Progress tracking and reporting.
*   High-level strategy selection (e.g., Tiling vs Standard inference).

### Layer 3: Interfaces (`src/cli`, `src/tui` - *Planned*)
**Responsibility:** Interacting with the user.
*   **CLI:** The current primary interface. Parses arguments, configures the engine, and formats output.
*   **TUI:** Future terminal UI for interactive use.
*   **GUI:** Future graphical interface.

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
├── types.hpp           Value types: Image, DeviceInfo, InferenceParams
├── frame_io.hpp        FrameIO interface
└── version.hpp         Version macros
```

### `src/`

All implementation code. Organized by domain.

```
src/
├── cli/                CLI application (thin consumer of the library)
│   └── main.cpp
├── common/             Shared internal utilities (no external deps)
│   └── srgb_lut.hpp
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
