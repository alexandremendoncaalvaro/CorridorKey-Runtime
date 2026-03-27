# CorridorKey Runtime - Technical Specification

This document defines the current product scope, support philosophy, and
runtime architecture of CorridorKey Runtime. It answers what the product is,
why it exists, and what it explicitly includes and excludes.

**See also:**
[Support Matrix](../help/SUPPORT_MATRIX.md) - explicit support status by
platform and hardware |
[ARCHITECTURE.md](ARCHITECTURE.md) - source structure and dependency rules |
[GUIDELINES.md](GUIDELINES.md) - code standards and build rules

---

## 1. Product Definition

### 1.1 What This Is

CorridorKey Runtime is a native C++ runtime for the
[CorridorKey](https://github.com/nikopueringer/CorridorKey) neural green screen
model. It exists to eliminate the Python dependency from model execution and
to deliver a distributable, hardware-accelerated inference engine for
professional video production workflows.

The product currently provides:

- A **CLI** (`corridorkey`) for direct command-line and pipeline use.
- An **OFX plugin** for DaVinci Resolve on Windows and macOS, backed by an
  out-of-process runtime service.

Both surfaces consume the same underlying library. Logic is never duplicated
between them.

### 1.2 What This Is Not

- Not a retraining framework or new model architecture.
- Not a generic multi-backend AI serving system.
- Not a broadly supported cross-platform engine; platform and hardware support
  is curated and explicitly designated. See
  [Support Matrix](../help/SUPPORT_MATRIX.md).
- Not a replacement for the original CorridorKey project; it is a focused
  native runtime and integration layer for deployment.

### 1.3 Intended Users

- **Local operators** who want native execution without Python or virtual
  environments.
- **Color graders and compositors** using DaVinci Resolve on officially
  supported hardware.
- **Pipeline integrators** who need a stable CLI or library surface for
  automated workflows.

---

## 2. Support Philosophy

Hardware and platform support is classified by one of four explicit
designations:

| Designation | Meaning |
|-------------|---------|
| **Officially supported** | Validated on this hardware/platform. Releases are tested against it. Bug reports are accepted and prioritized. |
| **Best-effort** | Known to work in most cases, but not systematically validated. Known limitations exist. Bug reports are accepted but not guaranteed to be resolved. |
| **Experimental** | Partially integrated. Known errors exist in practice. Not recommended for production use. Bug reports are accepted for tracking purposes only. |
| **Unsupported** | Not integrated or known to be broken. No bug reports accepted. |

Vague claims such as "works on most hardware" or "compatible with" are not
used. Every hardware path and host version has an explicit designation.

Support is defined by packaged and validated product tracks. A backend present
in the core runtime for probing, diagnostics, or future integration does not
become a support claim unless it is distributed and validated as a product
track.

The complete support table is in [Support Matrix](../help/SUPPORT_MATRIX.md).

---

## 3. Runtime Architecture

### 3.1 Layer Overview

- **Interface layer:** CLI and OFX plugin
- **Application layer:** job orchestration, OFX runtime service, diagnostics
- **Core layer:** inference session management, device detection, frame I/O,
  post-process, and session policies

### 3.2 Execution Tracks

The runtime contains multiple backend hooks, but product support is defined by
curated execution tracks.

The current official product tracks are:

- Apple Silicon via MLX model pack and bridge exports
- Windows RTX via ONNX Runtime TensorRT RTX EP on NVIDIA RTX 30 series and
  newer

The current experimental product track is:

- Windows DirectML

Additional provider hooks may exist in the core runtime for diagnostics,
bring-up, or future tracks. Those hooks are not support claims by themselves.

### 3.3 OFX Out-of-Process Runtime

The OFX plugin runs the inference backend in a separate process managed by the
App-layer OFX runtime service. The plugin is a thin IPC client; it does not
load ONNX sessions or GPU backends directly.

This design isolates backend failures, TensorRT RTX compilation errors, and
VRAM exhaustion from the DaVinci Resolve host process. The session broker in
the service layer pools initialized sessions across multiple OFX node
instances to avoid redundant GPU warmups.

Frame data moves between plugin and service over shared memory. The IPC
protocol is versioned to ensure the plugin and service remain compatible
across incremental updates.

### 3.4 Model Artifacts

Each product track ships curated model artifacts optimized for that track. The
runtime contract - API, parameter schema, and output format - is identical
across tracks. The artifact format and execution provider may differ.

### 3.5 Fallback Behavior

Fallback is surface-dependent.

- CLI and tolerant automation workflows may fall back to ONNX CPU execution.
- The OFX plugin favors explicit failure over silent CPU fallback on
  unsupported interactive GPU requests.

Fallback or failure is logged explicitly. The `corridorkey doctor` command
reports the active execution path and any fallback conditions before
processing begins.

---

## 4. Product Boundaries

### 4.1 Current Scope

- Native inference execution:
  - MLX for the official Apple Silicon track
  - TensorRT RTX EP for the official Windows RTX track
  - DirectML for the experimental Windows DirectML track
  - ONNX CPU fallback for tolerant workflows
- CLI surface with stable JSON and NDJSON output contracts
- OFX plugin for DaVinci Resolve 20 on Apple Silicon and Windows
- Alpha hint ingestion and rough-matte fallback generation
- Platform-specific model artifact packaging
- `doctor`, `benchmark`, and `process` commands with structured diagnostics

### 4.2 Non-Goals

- Training, fine-tuning, or exporting new model architectures
- A generic plugin framework or SDK for third-party extension
- Support for editing hosts other than DaVinci Resolve
- Browser, cloud, or server-side deployment
- Real-time preview at full resolution without hardware acceleration

---

## 5. Operational Contracts

### 5.1 CLI Output

All commands produce human-readable output by default. Append `--json` to
receive NDJSON event streams. The NDJSON schema is stable across patch
releases and is the integration surface for pipeline automation.

### 5.2 Diagnostic Commands

`corridorkey doctor` reports:

- detected hardware and selected backend
- fallback conditions, if any
- model artifact presence and validity
- platform-specific constraints relevant to the current runtime

### 5.3 Exit Codes

The CLI uses deterministic exit codes. Zero indicates success. Non-zero codes
correspond to specific failure categories documented in the `--help` output.

---

## 6. Performance Constraints

- No Python in any execution path
- No heap allocation in per-frame or per-pixel loops
- All image buffers are 64-byte aligned for SIMD compatibility
- Zero-copy frame passing via `std::span` between processing stages
- TensorRT RTX first-run compilation is expected and takes 10-30 seconds for a
  new GPU and model combination
