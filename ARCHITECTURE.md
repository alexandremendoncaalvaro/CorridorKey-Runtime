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
(orchestration, OFX service, diagnostics), and the Interfaces (CLI, OFX
plugin for DaVinci Resolve and Foundry Nuke, and Tauri desktop GUI).

**Key Principles:**

1. **Library First.** The engine is the product boundary. CLI, OFX plugin,
   and Tauri GUI all consume the same runtime rather than reimplementing
   behavior.
2. **Interface Segregation.** Each surface (CLI, OFX plugin, Tauri GUI) is
   a thin client over the App/Core contracts. Business logic never lives in
   an interface layer.
3. **Predictable Operations.** Diagnostics, fallback behavior, and error
   reporting are first-class concerns, not afterthoughts.
4. **Curated Platform Tracks.** The official product tracks are Apple Silicon
   through MLX and Windows RTX through curated dynamic `.ts` artifacts loaded
   by the LibTorch/TorchTRT runtime on NVIDIA RTX 30 series and newer. Windows DirectML and Linux RTX (CUDA EP via ONNX
   Runtime) are
   explicit experimental product tracks. Other provider hooks present in the
   core runtime do not become support claims unless they are packaged and
   validated. See [Support Matrix](help/SUPPORT_MATRIX.md).
5. **Shared Runtime, Curated Artifacts.** The runtime contract is stable
   across product tracks. Model artifacts and backend adapters may differ by
   platform when required for predictable performance.

## 1.1 Track Selection Contract

The runtime keeps host, backend, and model selection separate so new product
tracks can be added without cross-wiring unrelated paths.

- Host surfaces (CLI, OFX, GUI) collect user intent and call the App layer.
- The App layer resolves product track, screen color, model pack, and
  diagnostics.
- The Core layer owns backend-specific sessions and converts artifact type
  into a concrete inference adapter.
- The Windows RTX track resolves `green` and `blue` to one dynamic artifact
  each. `Blue-Green Channel Swap` resolves to the green artifact with
  blue-plate canonicalization only when that mode is selected explicitly.
- ONNX provider hooks belong to non-Windows-RTX tracks and reference
  workflows. They are not shipped, probed, or selected by the Windows RTX
  product path.

---

## 2. Structural Layers

### Layer 1: Core (`src/core`, `src/frame_io`, `src/post_process`)

The engine, math, and I/O capabilities.

- **Inference:** Backend adapters and session management for the official MLX
  and LibTorch/TorchTRT product paths, the experimental DirectML and Linux CUDA EP
  tracks, and other internal provider hooks used for diagnostics, bring-up, or
  future packaging work.
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
- OFX runtime service: out-of-process session brokering, IPC protocol, and
  session lifecycle management for the OFX plugin.
- Product-track policy: artifact selection, compatibility rules, and support
  behavior shared by CLI, OFX, and the Tauri GUI.

### Layer 3: Interfaces (`src/cli`, `src/plugins/ofx`, `src/gui`)

User-facing surfaces. Each is a thin consumer of App/Core contracts.

- **CLI** (`src/cli`): Argument parsing, output formatting, command dispatch.
  Ships inside the OFX installer alongside `CorridorKey.ofx` and is registered
  on the system `PATH` so it is callable from any terminal after the OFX
  install. Also available standalone in the portable runtime bundle that
  feeds the Tauri GUI installer.
- **OFX Plugin** (`src/plugins/ofx`): OpenFX host integration, render
  callback, runtime client that communicates with the App-layer OFX service.
  Host-agnostic at the OFX 1.4 contract level; Resolve and Nuke 17 are the
  validated hosts today, with per-host workarounds gated behind explicit
  branches that never regress the path the other host depends on.
- **Tauri GUI** (`src/gui`): Tauri 2 + React desktop application. Distributed
  as a separate desktop installer that embeds its own copy of the runtime
  payload (the staged output of `package_windows.ps1`); does not require the
  OFX bundle to be installed.

---

## 3. Directory Map

### Root (`/`)

Project-level configuration, project documentation, and top-level domain
directories only.

| Entry | Purpose |
|-------|---------|
| `ARCHITECTURE.md` | Source structure and dependency rules |
| `DESIGN.md` | Frontend product and interface design rules |
| `WORKFLOW.md` | Agent workflow contract |
| `CMakeLists.txt` | Root build definition |
| `vcpkg.json` | Dependency manifest |
| `vcpkg-configuration.json` | Baseline pin for reproducible builds |
| `CMakePresets.json` | Authoritative build configurations |
| `README.md` | User-facing overview and installation |
| `CONTRIBUTING.md` | Developer onboarding and PR process |
| `AGENTS.md` | Machine-readable rule summary for AI tools |
| `CLAUDE.md` | Machine-readable rule summary for AI tools |
| `.agents/` | Codex-facing agentic skills and agent definitions |
| `.claude/` | Claude Code-facing agentic skills and agents |
| `doc/` | ADR and task records |
| `docs/` | Supporting project documentation |
| `help/` | User-facing help and support docs |

### `doc/`

Agentic project records live under `doc/` so architecture and task decisions
are discoverable by both human maintainers and coding agents.

```text
doc/
|-- adr/       Architecture Decision Records: NNNN-<slug>.md
`-- tasks/     Agentic task records: NNNN-<slug>.md
```

ADR status values are `proposed`, `accepted`, `deprecated`, or
`superseded by ADR-NNNN`.

### Agent Targets

```text
.claude/
|-- skills/agentic-*/SKILL.md
`-- agents/fresh-context-reviewer.md

.agents/
`-- skills/agentic-*/{SKILL.md, agents/openai.yaml}
```

`.claude/settings.local.json` and `.claude/worktrees/` are local state and are
not repository records.

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
|   |-- ofx_runtime_protocol.cpp    OFX IPC wire protocol
|   |-- ofx_runtime_service.cpp     Out-of-process OFX runtime server
|   |-- ofx_session_broker.cpp      Session pool management for OFX
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
|   |-- windows_rtx_probe.cpp   Windows RTX backend probe
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
|   `-- ofx/                    OpenFX plugin
|       |-- ofx_plugin.cpp      OFX entry point and descriptor
|       |-- ofx_instance.cpp    Instance lifecycle
|       |-- ofx_render.cpp      Render callback
|       |-- ofx_actions.cpp     OFX action handlers
|       |-- ofx_runtime_client.cpp  IPC client to the App-layer service
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
|-- browser_poc/        Browser-based proof of concept and export surface
|-- hint_generator/     Python helper for generating alpha hints
|-- model_exporter/     Python scripts to export PyTorch models to ONNX
|-- torchtrt_compiler/  Python scripts to export dynamic RTX `.ts` artifacts
|-- torchtrt_runner/    C++ smoke test that loads a .ts engine standalone
`-- coreml_student/     Apple Neural Engine distillation toolkit
```

### `scripts/installer/`

Canonical Windows RTX installer authoring (Inno Setup 6). The wrapper stages
and validates the OFX bundle, then builds the user-facing setup executable
from the template in this directory. See `docs/RELEASE_GUIDELINES.md`
"Modern installer (Inno Setup)" for the operator workflow.

```text
scripts/installer/
|-- corridorkey.iss.template       Inno Setup source consumed by ISCC
|-- build_installer.ps1            Driver: expands template, runs ISCC
|-- build_distribution_manifest.py Regenerates manifest from HF state
|-- distribution_manifest.json     Pack URLs + SHA256 (committed)
`-- stage_offline_payload.ps1      Downloads packs into _offline_payload/
```

---

## 4. OFX Runtime Architecture

The OFX plugin uses an out-of-process architecture to protect the host
application (DaVinci Resolve) from backend and VRAM failures.

**Crash Containment.** The runtime service (`ofx_runtime_service`) runs in a
separate process. ONNX session failures, TorchTRT load failures, first-run
shape compilation, and VRAM exhaustion are isolated from the host.

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

## 6. Active ADRs

- [ADR-0001: Agentic Repository Layout](doc/adr/0001-agentic-repository-layout.md)

## 7. Adding New Code - Decision Tree

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

Is it application orchestration or OFX service logic?
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
