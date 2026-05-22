# Project Architecture

This document defines the project structure, the purpose of every directory,
and the rules that govern where code lives. It is the single source of truth
for structural decisions. Any deviation must be discussed and approved in a
PR before it happens.

**See also:**
[SPEC.md](docs/SPEC.md) - product scope and support philosophy |
[GUIDELINES.md](docs/GUIDELINES.md) - code standards, testing, build rules

---

## 1. Architectural Philosophy

The project exists to ship CorridorKey inference as a native, distributable,
and integrable runtime for real hardware. The architecture enforces strict
separation between the Core (inference, math, I/O), the Application
(orchestration, host-plugin services, diagnostics), and the Interfaces (CLI,
OFX plugin for DaVinci Resolve and Foundry Nuke, Adobe plugins for After
Effects and Premiere, and Tauri desktop GUI).

**Key Principles:**

1. **Library First.** The engine is the product boundary. CLI, OFX plugin,
   Adobe plugins, and Tauri GUI all consume the same runtime rather than
   reimplementing behavior.
2. **Interface Segregation.** Each surface (CLI, OFX plugin, Adobe plugins,
   Tauri GUI) is a thin client over the App/Core contracts. Business logic
   never lives in an interface layer.
3. **Predictable Operations.** Diagnostics, fallback behavior, and error
   reporting are first-class concerns, not afterthoughts.
4. **Curated Platform Tracks.** The official product tracks are Apple Silicon
   through MLX and Windows RTX through ONNX Runtime TensorRT RTX EP for green
   and the dynamic TorchScript path for blue on NVIDIA RTX 30 series and
   newer. Windows DirectML and Linux RTX (CUDA EP via ONNX Runtime) are
   explicit experimental product tracks. Other provider hooks present in the
   core runtime do not become support claims unless they are packaged and
   validated. See [Support Matrix](help/SUPPORT_MATRIX.md).
5. **Shared Runtime, Curated Artifacts.** The runtime contract is stable
   across product tracks. Model artifacts and backend adapters may differ by
   platform when required for predictable performance.

---

## 2. Structural Layers

### Layer 1: Core (`src/core`, `src/frame_io`, `src/post_process`)

The engine, math, and I/O capabilities.

- **Inference:** Backend adapters and session management for the official MLX,
  TensorRT RTX EP, and dynamic TorchScript product paths, the experimental
  DirectML and Linux CUDA EP tracks, and other internal provider hooks used for
  diagnostics, bring-up, or future packaging work.
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
- Preset management and strategy selection.
- Progress tracking, stage timing, and benchmark reporting.
- Structured diagnostics, capability reports, and model catalogs.
- Host-plugin runtime service: out-of-process session brokering, IPC
  protocol, and session lifecycle management for interactive host plugins.
- Product-track policy: artifact selection, compatibility rules, and support
  behavior shared by CLI, OFX, Adobe plugins, and the Tauri GUI.

### Layer 3: Interfaces (`src/cli`, `src/plugins/ofx`, `src/plugins/adobe`, `src/gui`)

User-facing surfaces. Each is a thin consumer of App/Core contracts.

- **CLI** (`src/cli`): Argument parsing, output formatting, command dispatch.
  Ships inside the OFX installer alongside `CorridorKey.ofx` and is registered
  on the system `PATH` so it is callable from any terminal after the OFX
  install. Also available standalone in the portable runtime bundle that
  feeds the Tauri GUI installer.
- **OFX Plugin** (`src/plugins/ofx`): OpenFX host integration, render
  callback, and adapter code that communicates with the App-layer host plugin
  runtime service.
  Host-agnostic at the OFX 1.4 contract level; Resolve and Nuke 17 are the
  validated hosts today, with per-host workarounds gated behind explicit
  branches that never regress the path the other host depends on.
- **Adobe Plugins** (`src/plugins/adobe`): Adobe After Effects SDK effect
  integration for After Effects and Premiere. Adobe SDK entrypoints,
  PiPL/effect metadata, host parameter mapping, pixel-format handling, and
  host logging live here. Shared inference, model selection, diagnostics,
  runtime policy, and frame transport stay in App/Core or shared common
  infrastructure.
- **Tauri GUI** (`src/gui`): Tauri 2 + React desktop application. Distributed
  as a separate desktop installer that embeds its own copy of the runtime
  payload (the staged output of `package_windows.ps1`); does not require the
  OFX bundle to be installed.

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

```text
include/corridorkey/
|-- engine.hpp          Engine class - main entry point
|-- types.hpp           Value types: Image, DeviceInfo, InferenceParams
|-- frame_io.hpp        FrameIO interface
`-- version.hpp         Version macros
```

### `src/`

All implementation code, organized by domain.

```text
src/
|-- app/                        Application orchestration layer
|   |-- job_orchestrator.cpp
|   |-- model_compiler.cpp
|   |-- host_plugin_runtime_client.cpp      IPC client used by host plugin adapters
|   |-- host_plugin_runtime_protocol.cpp    Host plugin IPC wire protocol
|   |-- host_plugin_runtime_service.cpp     Out-of-process host plugin runtime server
|   |-- host_plugin_session_broker.cpp      Session pool management for host plugins
|   |-- runtime_contracts.cpp
|   |-- runtime_diagnostics.cpp
|   `-- hardware_profile.hpp
|
|-- cli/                        CLI application (thin consumer)
|   |-- main.cpp
|   |-- device_selection.hpp
|   `-- process_paths.hpp
|
|-- common/                     Shared internal utilities (no external deps)
|   |-- local_ipc.cpp           Local IPC transport abstractions
|   |-- shared_memory_transport.cpp  Shared-memory frame transport
|   |-- hardware_telemetry.hpp
|   |-- parallel_for.hpp
|   |-- runtime_paths.hpp
|   |-- srgb_lut.hpp
|   `-- stage_profiler.hpp
|
|-- core/                       Inference engine and device detection
|   |-- engine.cpp
|   |-- inference_session.cpp
|   |-- device_detection.cpp
|   |-- mlx_probe.cpp           macOS MLX backend probe
|   |-- mlx_session.cpp         MLX inference session
|   |-- windows_rtx_probe.cpp   Windows TensorRT RTX backend probe
|   |-- session_cache_policy.hpp
|   |-- session_policy.hpp
|   `-- tile_blend.hpp
|
|-- frame_io/                   Image and video read/write
|   |-- frame_io.cpp
|   |-- video_io.cpp
|   |-- exr_io.cpp
|   `-- png_io.cpp
|
|-- gui/                        Tauri GUI (in-development, not yet released)
|
|-- plugins/
|   |-- adobe/                  Adobe After Effects SDK effect plugin
|   |   |-- CMakeLists.txt      Adobe plugin target and PiPL generation rules
|   |   |-- adobe_effect.cpp    Adobe effect entry point and selector dispatch
|   |   |-- adobe_effect_metadata.hpp.in Generated effect metadata constants
|   |   `-- corridorkey_adobe.r.in PiPL resource definition template
|   |
|   `-- ofx/                    OpenFX plugin
|       |-- ofx_plugin.cpp      OFX entry point and descriptor
|       |-- ofx_instance.cpp    Instance lifecycle
|       |-- ofx_render.cpp      Render callback
|       |-- ofx_actions.cpp     OFX action handlers
|       |-- ofx_image_utils.cpp
|       `-- ofx_logging.cpp
|
`-- post_process/               Color math, despill, despeckle
    |-- color_utils.cpp
    `-- despill.cpp
```

### `tests/`

All test code, organized by level.

```text
tests/
|-- unit/               Fast, isolated logic tests (Catch2, no GPU, no I/O)
|-- integration/        Multi-module tests (file round-trips, no GPU)
|-- e2e/                Full binary tests with real models and hardware
|-- regression/         Bug reproduction tests
`-- fixtures/           Reference files (< 1 MB total)
```

### `tools/`

Standalone auxiliary tools. Must not leak dependencies into the main build.

```text
tools/
|-- browser_poc/        Vite + TypeScript browser-side keying proof of concept
|-- coreml_student/     Apple Neural Engine distillation toolkit
|-- hint_generator/     Python OpenCV utility for rough chroma alpha hints
|-- model_exporter/     Python scripts to export PyTorch models to ONNX
|-- torchtrt_compiler/  Python scripts to export dynamic blue TorchScript artifacts
`-- torchtrt_runner/    C++ smoke test that loads a .ts engine standalone
```

### `scripts/installer/`

Modern Windows installer authoring (Inno Setup 6). The canonical Windows
release wrapper emits the legacy NSIS installer when no modern flavor is
selected, and emits the selected Inno Setup installer when
`-Flavor online|offline` is passed. See `docs/RELEASE_GUIDELINES.md`
"Modern installer (Inno Setup)" for the operator workflow and the
authoritative flavor matrix.

```text
scripts/installer/
|-- corridorkey.iss.template       Inno Setup source consumed by ISCC
|-- build_installer.ps1            Driver: expands template, runs ISCC
|-- build_distribution_manifest.py Regenerates manifest from HF state
|-- distribution_manifest.json     Pack URLs + SHA256 (committed)
`-- stage_offline_payload.ps1      Downloads packs into _offline_payload/
```

---

## 4. Host Plugin Runtime Architecture

Interactive host plugins use an out-of-process architecture to protect host
applications from backend and VRAM failures.

**Crash Containment.** The runtime service (`host_plugin_runtime_service`) runs in a
separate process. ONNX session failures, VRAM exhaustion, and TensorRT RTX
compilation errors are isolated from DaVinci Resolve, Foundry Nuke, After
Effects, and Premiere.

**Session Residency.** The session broker (`host_plugin_session_broker`) pools
initialized sessions by backend and model. Multiple host plugin instances
share a session pool rather than each triggering a full GPU warmup.

**Frame Transport.** High-bandwidth frames move between the plugin and service
via shared memory (`shared_memory_transport`). The IPC wire protocol
(`host_plugin_runtime_protocol`) is versioned and handles request/reply orchestration.

**Diagnostics Parity.** Fallback details, stage timings, and initialization
logs from the service are surfaced back to the plugin client and are
accessible through the host's log viewer or effect UI.

---

## 5. Engineering Standards

**Public API Surface.** Public headers live exclusively in
`include/corridorkey/`. External library types (`OrtSession`, `Imf::*`,
`AVFrame`, CUDA / NPP / LibTorch types, host-specific OFX types, host-specific
Adobe SDK types) never appear in public headers - they are wrapped in `src/`.

**PIMPL for ABI Stability.** Main classes such as `Engine` use the PIMPL
pattern so implementation details can change without breaking the public ABI.

**Symbol Visibility Hidden by Default.** Symbols are hidden by default; only
the API surface is exported through the `CORRIDORKEY_API` macro.

**Zero-Copy Data Flow.** `std::span` (via the `Image` struct) passes data
between modules without copying. `ImageBuffer` owns allocations. Do not use
`std::vector<float>` for large pixel data.

**SIMD Alignment.** All image buffers are 64-byte aligned for AVX-512 and
NEON compatibility.

**No Exceptions in Host Plugin Surfaces.** All `extern "C"` OFX and Adobe SDK
entry points are wrapped in a top-level `try/catch`. Exceptions are translated
into host status codes or host-visible errors; none may escape to the host.

**No Heap Allocation in Per-Frame Paths.** Per-frame and per-pixel functions
in `src/post_process/` must not allocate. Scratch state is owned by the OFX
instance lifecycle and reused across frames.

**Result-Based Error Handling.** The library uses `std::expected<T, Error>`
throughout. `std::exit` and `abort` are never called from library code.

**No New Top-Level or `src/` Directories Without an Architecture Update.**
Any new top-level directory or `src/` subdirectory must be reflected in this
document in the same change, including its purpose and dependency rules.

---

## 6. Adding New Code - Decision Tree

```text
Is it a public API type or function?
  YES -> include/corridorkey/
  NO  v

Is it inference logic or hardware management?
  YES -> src/core/
  NO  v

Is it file I/O (video/image)?
  YES -> src/frame_io/
  NO  v

Is it pixel math (color, filters)?
  YES -> src/post_process/
  NO  v

Is it OFX plugin logic?
  YES -> src/plugins/ofx/
  NO  v

Is it Adobe host plugin logic?
  YES -> src/plugins/adobe/
  NO  v

Is it application orchestration or host-plugin service logic?
  YES -> src/app/
  NO  v

Is it CLI/command-line logic?
  YES -> src/cli/
  NO  v

Is it a shared internal utility with no external deps?
  YES -> src/common/
  NO  v

Is it a Python helper?
  YES -> tools/
  NO  v

Is it a test?
  Pure logic   -> tests/unit/
  Pipeline I/O -> tests/integration/
  Full binary  -> tests/e2e/
  NO           v

-> STOP. Discuss in an issue.
```

---

## 7. Active ADRs

Accepted architecture decision records that bind code in this repository.
Superseded or proposed ADRs are not listed here.

| ADR | Decision |
|-----|----------|
| [ADR-0001](doc/adr/0001-agentic-repository-layout.md) | Agentic repository layout: `doc/adr/`, `doc/tasks/`, `.claude/skills/`, `.agents/skills/`, fresh-context reviewer. |
| [ADR-0002](doc/adr/0002-measure-torchtrt-stream-boundaries.md) | TorchTRT stream-boundary telemetry separates model replay from input/output stream work. |
| [ADR-0003](doc/adr/0003-run-torchtrt-input-prep-on-torch-current-stream.md) | TorchTRT input prep runs on Torch's current stream to avoid cross-stream readiness waits. |
| [ADR-0004](doc/adr/0004-own-torchtrt-work-stream.md) | TorchTRT owns a dedicated work stream guarded against host-side serialization. |
| [ADR-0005](doc/adr/0005-default-ofx-torchtrt-cuda-graph-off.md) | OFX TorchTRT defaults to CUDA Graph capture off; graph capture is opt-in. |
| [ADR-0006](doc/adr/0006-expose-dedicated-ofx-nodes.md) | One OFX bundle exposes two dedicated descriptors: legacy Green (`com.corridorkey.resolve`) and new Blue (`com.corridorkey.resolve.blue`, label `CorridorKey Blue`). Both identifier strings and the Blue label are persisted product contracts. |
| [ADR-0007](doc/adr/0007-add-adobe-host-plugins.md) | Adobe After Effects and Premiere plugins are first-class Interface-layer adapters over shared App/Core runtime contracts. |

