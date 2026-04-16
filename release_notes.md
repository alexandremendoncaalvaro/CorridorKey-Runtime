## Overview

v0.7.5 delivers the comprehensive runtime optimization track for Windows RTX and Apple Silicon, migrating the heaviest host-side (CPU) bottlenecks to fully device-resident GPU streams (CUDA, NPP, ONNX Runtime I/O Binding on Windows; Apple Accelerate on macOS). End-to-end processing for full-frame 4K workflows has improved radically, cutting individual module latencies by 70% to 90%. This release also adds blue-screen keying support, a hardened CI/CD pipeline, and several stability fixes.

## Changelog

### Added
- **GPU-Accelerated Output Resize Pipeline (Windows RTX):** Planar outputs now resize completely on the GPU leveraging NVIDIA Performance Primitives (NPP), bypassing over 300ms of costly host-side scale processing per frame.
- **GPU-Accelerated Input Preparation (Windows RTX):** Alpha hint splices, image background normalizations, and initial tensor scaling loops run on an asynchronous device-resident CUDA/NPP path (`GpuInputPrep`), cutting start-of-frame execution time by ~74%.
- **Pinned Host Memory (Windows RTX):** Output extraction uses `cudaMallocHost` via a custom `PinnedBuffer` allocator, preventing asynchronous DMA fallback stalls.
- **TensorRT I/O Binding (Windows RTX):** Narrow I/O binding path keeps tensors device-resident through the full inference cycle, eliminating host round-trips for packaged TensorRT contexts.
- **Vectorized CPU Matrix Math:** AVX2 F16C hardware intrinsics for fast FP16-to-FP32 tensor materialization on Windows, with safe architectural gating on Apple Silicon.
- **Apple Accelerate Vectorization (macOS):** Premultiply, alpha levels, image clamping, and temporal smoothing now use Apple Accelerate vDSP/vImage, delivering significant speedups on Apple Silicon.
- **MLX Input Prep Vectorization (macOS):** Vectorized MLX input preparation path with transparency fix using Accelerate framework.
- **Global ORT Thread Pooling Context:** ONNX Runtime context ownership relocated to a cross-plugin shared `OrtProcessContext`, disabling expensive per-session thread limitations and enabling `use_env_allocators=1`, halving multi-instance RAM and bootstrap costs.
- **Blue-Screen Keying:** Canonicalized OFX flow now supports blue-screen chroma key in addition to green-screen.
- **Granular Execution Metrics:** Dense, non-destructive stage timers (`tensor_materialize`, `resize`, `finalize`, `ofx_broker_writeback`) and metadata tracing (active memory modes, warmups) in benchmarking JSON logs.
- **CI/CD Pipeline:** Automated format checks, macOS + Windows build and unit test gates, vcpkg binary caching, compiler caching (sccache), concurrency control, and fail-fast job gating.
- **Release Validation Pipeline:** Artifact smoke tests and release-notes linter that block releases with phantom assets or out-of-scope backend claims.

### Changed
- **Direct Planar Projections:** Decommissioned obsolete interleaved RGBA memory paths before output resizing. Frames map tensor chunks directly onto planar destination outputs.
- **Fused Display Preview Chains:** Combined checkerboard visualization with sRGB display render, removing redundant nested copies during interactive previewing.
- **Parallel Loop Chunking:** OFX writebacks, channel packing, and Gaussian blurs execute concurrently on shared row-parallel loops.
- **Fused Output Validation:** Statistical array scans and finite-value checks during TensorRT inference execute in a unified single analytical pass.
- **Default Quality Preset:** Unconfigured plugin now boots to "Draft (512)" instead of the ambiguous "Auto" selector.

### Fixed
- **DaVinci Resolve "Last Frame" Timing:** Wall-clock calculations accurately aggregate overlapping backend timestamps, preventing the diagnostics panel from double-counting elapsed frames.
- **Corrupted Foreground Buffer Bindings:** Resolved black-silhouette rendering affecting foreground arrays on the I/O binding path.
- **MLX Transparency Bug (macOS):** Fixed transparency handling in the MLX inference path using Accelerate framework.
- **Apple Silicon Intrinsics Gating:** x86 SIMD intrinsics are properly gated on Apple Silicon builds.
- **macOS Build Stability:** Fixed PIMPL incomplete-type errors (clang/libc++) in `InferenceSession` and `InstanceData` by moving unique_ptr move ops out-of-line.

## Assets & Downloads

### Windows
- **NVIDIA RTX 30 Series or newer:** Download `CorridorKey_Resolve_v0.7.5_Windows_RTX_Installer.exe` or `CorridorKey_Resolve_v0.7.5_Windows_RTX.zip`.

### macOS
- **Apple Silicon (M1 or newer):** Download `CorridorKey_Resolve_v0.7.5_macOS_AppleSilicon.pkg` or `CorridorKey_Resolve_v0.7.5_macOS_AppleSilicon.dmg`.

## Installation Instructions

### Windows
1. Close DaVinci Resolve if it is running.
2. Run the downloaded installer (or extract the zip to the OFX plugin directory).
3. The installer automatically overwrites the previous version.
4. Launch DaVinci Resolve and load the plugin from the OpenFX Library.

### macOS
1. Close DaVinci Resolve if it is running.
2. Open the `.pkg` installer (or mount the `.dmg` and drag CorridorKey to the OFX plugin directory).
3. Launch DaVinci Resolve and load the plugin from the OpenFX Library.

## Uninstallation

### Windows
Go to **Windows Settings > Apps > Installed apps**, search for "CorridorKey Resolve OFX", and click Uninstall.

### macOS
Delete the CorridorKey bundle from `/Library/OFX/Plugins/`.
