# Project Architecture

This document defines the project structure, the purpose of every directory,
and the rules that govern where code lives. It is the single source of truth
for structural decisions. Any deviation must be discussed and approved in a
PR before it happens.

**See also:**
[SPEC.md](SPEC.md) — product scope and support philosophy |
[GUIDELINES.md](GUIDELINES.md) — code standards, testing, build rules

---

## 1. Architectural Philosophy

The project exists to ship CorridorKey inference as a native, distributable,
and integrable runtime for real hardware. The architecture enforces strict
separation between the Core (inference, math, I/O), the Application
(orchestration, OFX service, diagnostics), and the Interfaces (CLI, OFX
plugin).

**Key Principles:**

1. **Library First.** The engine is the product boundary. CLI and OFX plugin
   consume the same runtime rather than reimplementing behavior.
2. **Interface Segregation.** Each surface (CLI, OFX plugin) is a thin client
   over the App/Core contracts. Business logic never lives in an interface
   layer.
3. **Predictable Operations.** Diagnostics, fallback behavior, and error
   reporting are first-class concerns, not afterthoughts.
4. **Curated Platform Tracks.** Apple Silicon (MLX) and Windows TensorRT are
   the two officially supported execution paths. Other paths have explicit
   support designations. See [Support Matrix](../help/SUPPORT_MATRIX.md).
5. **Shared Runtime, Curated Artifacts.** The runtime contract is stable
   across product tracks. Model artifacts and backend adapters may differ by
   platform when required for predictable performance.

---

## 2. Structural Layers

### Layer 1: Core (`src/core`, `src/frame_io`, `src/post_process`)

The engine, math, and I/O capabilities.

- **Inference:** Backend adapters and session management for supported
  execution paths (MLX, TensorRT, DirectML, ONNX CPU).
- **Hardware:** Device detection and provider selection.
- **Video Pipeline:** FFmpeg integration for direct memory processing.
- **Math:** Color space conversion, despill, despeckle algorithms.
- **Hint Handling:** External hint ingestion and rough-matte fallback
  generation.

Rules:
- Must not depend on CLI, OFX plugin, or App layers.
- Must not print to stdout or stderr; use callbacks or result types.
- Must return rich error types (`Result<T>`).

### Layer 2: Application (`src/app`)

Orchestration of Core capabilities into coherent jobs and services.

- Job definitions and validation.
- Preset management and strategy selection (tiling vs. standard inference).
- Progress tracking, stage timing, and benchmark reporting.
- Structured diagnostics, capability reports, and model catalogs.
- OFX runtime service: out-of-process session brokering, IPC protocol, and
  session lifecycle management for the OFX plugin.

### Layer 3: Interfaces (`src/cli`, `src/plugins/ofx`)

User-facing surfaces. Each is a thin consumer of App/Core contracts.

- **CLI** (`src/cli`): Argument parsing, output formatting, command dispatch.
- **OFX Plugin** (`src/plugins/ofx`): OpenFX host integration, render
  callback, runtime client that communicates with the App-layer OFX service.

---

## 3. Directory Map

### Root (`/`)

Project-level configuration and documentation only.

| Entry | Purpose |
|-------|---------|
| `CMakeLists.txt` | Root build definition |
| `vcpkg.json` | Dependency manifest |
| `vcpkg-configuration.json` | Baseline pin for reproducible builds |
| `CMakePresets.json` | Authoritative build configurations |
| `README.md` | User-facing overview and installation |
| `CONTRIBUTING.md` | Developer onboarding and PR process |
| `AGENTS.md` | Machine-readable rule summary for AI tools |
| `CLAUDE.md` | Machine-readable rule summary for AI tools |

### `include/corridorkey/`

Public API headers only. These are the headers external consumers include.

```
include/corridorkey/
├── engine.hpp          Engine class — main entry point
├── types.hpp           Value types: Image, DeviceInfo, InferenceParams
├── frame_io.hpp        FrameIO interface
└── version.hpp         Version macros
```

### `src/`

All implementation code, organized by domain.

```
src/
├── app/                        Application orchestration layer
│   ├── job_orchestrator.cpp
│   ├── model_compiler.cpp
│   ├── ofx_runtime_protocol.cpp    OFX IPC wire protocol
│   ├── ofx_runtime_service.cpp     Out-of-process OFX runtime server
│   ├── ofx_session_broker.cpp      Session pool management for OFX
│   ├── runtime_contracts.cpp
│   ├── runtime_diagnostics.cpp
│   └── hardware_profile.hpp
│
├── cli/                        CLI application (thin consumer)
│   ├── main.cpp
│   ├── device_selection.hpp
│   └── process_paths.hpp
│
├── common/                     Shared internal utilities (no external deps)
│   ├── local_ipc.cpp           Local IPC transport abstractions
│   ├── shared_memory_transport.cpp  Shared-memory frame transport
│   ├── hardware_telemetry.hpp
│   ├── parallel_for.hpp
│   ├── runtime_paths.hpp
│   ├── srgb_lut.hpp
│   └── stage_profiler.hpp
│
├── core/                       Inference engine and device detection
│   ├── engine.cpp
│   ├── inference_session.cpp
│   ├── device_detection.cpp
│   ├── mlx_probe.cpp           macOS MLX backend probe
│   ├── mlx_session.cpp         MLX inference session
│   ├── windows_rtx_probe.cpp   Windows TensorRT/RTX backend probe
│   ├── session_cache_policy.hpp
│   ├── session_policy.hpp
│   └── tile_blend.hpp
│
├── frame_io/                   Image and video read/write
│   ├── frame_io.cpp
│   ├── video_io.cpp
│   ├── exr_io.cpp
│   └── png_io.cpp
│
├── gui/                        Tauri GUI (in-development, not yet released)
│
├── plugins/
│   └── ofx/                    OpenFX plugin
│       ├── ofx_plugin.cpp      OFX entry point and descriptor
│       ├── ofx_instance.cpp    Instance lifecycle
│       ├── ofx_render.cpp      Render callback
│       ├── ofx_actions.cpp     OFX action handlers
│       ├── ofx_runtime_client.cpp  IPC client to the App-layer service
│       ├── ofx_image_utils.cpp
│       └── ofx_logging.cpp
│
└── post_process/               Color math, despill, despeckle
    ├── color_utils.cpp
    └── despill.cpp
```

### `tests/`

All test code, organized by level.

```
tests/
├── unit/               Fast, isolated logic tests (Catch2, no GPU, no I/O)
├── integration/        Multi-module tests (file round-trips, no GPU)
├── e2e/                Full binary tests with real models and hardware
├── regression/         Bug reproduction tests
└── fixtures/           Reference files (< 1 MB total)
```

### `tools/`

Standalone auxiliary tools. Must not leak dependencies into the main build.

```
tools/
├── model_exporter/     Python scripts to export PyTorch models to ONNX
└── hint_generator/     Reference auto-hint implementation
```

---

## 4. OFX Runtime Architecture

The OFX plugin uses an out-of-process architecture to protect the host
application (DaVinci Resolve) from backend and VRAM failures.

**Crash Containment.** The runtime service (`ofx_runtime_service`) runs in a
separate process. ONNX session failures, VRAM exhaustion, and TensorRT
compilation errors are isolated from the host.

**Session Residency.** The session broker (`ofx_session_broker`) pools
initialized sessions by backend and model. Multiple OFX node instances share
a session pool rather than each triggering a full GPU warmup.

**Frame Transport.** High-bandwidth frames move between the plugin and service
via shared memory (`shared_memory_transport`). The IPC wire protocol
(`ofx_runtime_protocol`) is versioned and handles request/reply orchestration.

**Diagnostics Parity.** Fallback details, stage timings, and initialization
logs from the service are surfaced back to the plugin client and are
accessible through the host's log viewer.

---

## 5. Engineering Standards

**Zero-Copy Data Flow.** `std::span` (via the `Image` struct) passes data
between modules without copying. `ImageBuffer` owns allocations.

**SIMD Alignment.** All image buffers are 64-byte aligned for AVX-512 and
NEON compatibility.

**No Exceptions in the OFX Surface.** All `extern "C"` OFX entry points are
wrapped in a top-level `try/catch`. Exceptions are translated into OFX status
codes; none may escape to the host.

**No Heap Allocation in Per-Frame Paths.** Per-frame and per-pixel functions
in `src/post_process/` must not allocate. Scratch state is owned by the OFX
instance lifecycle and reused across frames.

**Result-Based Error Handling.** The library uses `std::expected<T, Error>`
throughout. `std::exit` and `abort` are never called from library code.

---

## 6. Adding New Code — Decision Tree

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

Is it OFX plugin logic?
  YES → src/plugins/ofx/
  NO ↓

Is it application orchestration or OFX service logic?
  YES → src/app/
  NO ↓

Is it CLI/command-line logic?
  YES → src/cli/
  NO ↓

Is it a shared internal utility with no external deps?
  YES → src/common/
  NO ↓

Is it a Python helper?
  YES → tools/
  NO ↓

Is it a test?
  Pure logic   → tests/unit/
  Pipeline I/O → tests/integration/
  Full binary  → tests/e2e/
  NO ↓

→ STOP. Discuss in an issue.
```
