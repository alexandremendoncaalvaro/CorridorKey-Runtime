# Product Direction: Native, Distributable, and Integratable Engine

## Executive Summary

- The product is communicated as a **production-oriented native engine** for executing CorridorKey with operational predictability on real hardware.
- The delivery sequence is explicit: **1. macOS first**, **2. Windows Universal GPU next**, **3. Integration surfaces**, **4. Broad platform expansion**.
- The immediate focus remains closing the macOS production runtime, but documentation must reflect that **Windows Universal GPU** is the current active product track.
- Core value is not "supporting many backends"; it is delivering **simple installation, reliable diagnostics, reproducible benchmarks, stable contracts, and consistent tier-based behavior**.
- The runtime is unified, but **model artifacts are curated per platform**. The product does not assume a single packaged ONNX is the right path for both Apple Silicon and Windows.
- Python remains allowed only in internal conversion and release preparation tools. The final distributed artifact must have **zero Python dependencies** for the end user.

## Product Positioning

- The project is not a "cross-platform port" of CorridorKey.
- The primary message is: **CorridorKey as a native, distributable, and integratable engine, built for real hardware and reproducible production use.**
- `library-first` remains a central pillar: CLI, future GUI, sidecars, plugins, and pipeline integrations all consume the same core logic.
- `doctor`, `benchmark`, `models`, `presets`, `process --json`, and stage-level telemetry are core product features, not accessories.

## Platform Tracks

- **Current: macOS 14+ Apple Silicon**
  - Unified runtime contracts.
  - `int8_512` and `int8_768` as compatibility baselines.
  - CPU as a mandatory fallback.
  - Mac acceleration requires **platform-specific artifacts** (MLX-first).
  - Portable CLI bundle as the first external artifact.
- **Active: Windows 11 (Universal GPU)**
  - **TensorRT RTX** as the primary high-performance path.
  - Integrated support for **CUDA** (GTX) and **DirectML** (AMD/Intel/Universal).
  - Focus on predictable installation, multi-backend diagnostics, and local caching.
  - CPU as a mandatory fallback.
- **Future**
  - GUI and sidecars as thin runtime consumers.
  - Linux and other paths only after macOS and Windows tracks are validated.

## Delivery Sequence

### Phase 1 — macOS production runtime (Release Gate)
- Robust backend and fallback logic.
- Performance guided by real-world benchmarks.
- Reliable tiling for high-resolution processing.
- Portable CLI bundle for third-party operators.

### Phase 2 — Windows Universal GPU track (Active)
- Installation and provider contracts for consumer GPUs.
- Caching, JIT compilation, and diagnostic strategy for TensorRT and CUDA.
- Validated tiers and models for RTX, GTX, and AMD hardware.
- `doctor` and `benchmark` parity with the macOS track.

### Phase 3 — Integration surfaces
- Stable Sidecar/Tauri contract built on existing JSON/NDJSON.
- GUI as a thin client.
- Explicit support for plugin and pipeline embedding.

### Phase 4 — Broader platform expansion
- Linux and secondary paths.
- Additional validation only where there is a clear value proposition.

## Strategic Commitments

- **Predictability over Magic**: Runtime behavior must be understandable and reproducible.
- **Practical Distribution over Fragile Setup**: Reduce accidental dependencies and installation friction.
- **Integration as a Structural Goal**: The runtime is a foundation for other tools, not just a standalone command.
- **Real Validation over Broad Claims**: Hardware support is communicated only after evidence-based testing.
- **Disciplined Scope**: Prioritize few, high-quality paths over generic, unvalidated compatibility.
