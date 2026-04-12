## Overview
This pre-release concludes the comprehensive runtime optimization track, delivering profound performance gains for Windows RTX by migrating the heaviest host-side (CPU) bottlenecks to fully device-resident GPU streams (via CUDA, NPP, and ONNX Runtime I/O Binding). End-to-end processing for full-frame 4K workflows has improved radically, cutting individual module latencies like resizing and input preparation by over 70% to 90%.

## Changelog
### Added
- **GPU-Accelerated Output Resize Pipeline:** Planar outputs now resize completely on the GPU leveraging NVIDIA Performance Primitives (NPP). This safely bypasses over 300ms of costly host-side scale processing per frame (Slice 0.7.4-11).
- **GPU-Accelerated Input Preparation:** We migrated alpha hint splices, image background normalizations, and initial tensor scaling loops to an asynchronous device-resident CUDA/NPP path (`GpuInputPrep`), slicing start-of-frame execution time by ~74% (Slice 0.7.4-12).
- **Pinned Host Memory (CUDA):** Standard output extraction bounds now correctly initialize using `cudaMallocHost` via a custom `PinnedBuffer` allocator (`memory_mode: pinned`), preventing asynchronous DMA fallback stalls (Slice 0.7.4-10).
- **Vectorized CPU Matrix Math:** Integrated optimized AVX2 F16C hardware intrinsic logic for ultra-fast fallback scenarios involving FP16-to-FP32 tensor materialization paths.
- **Global ORT Thread Pooling Context:** Relocated ONNX Runtime context ownership to a cross-plugin shared `OrtProcessContext`. We successfully disabled expensive per-session thread limitations and enabled `use_env_allocators=1`, halving multi-instance RAM and bootstrap costs (Slice 0.7.4-0).
- **Granular Execution Metrics:** Pushed dense, non-destructive stage timers (e.g., `tensor_materialize`, `resize`, `finalize`, `ofx_broker_writeback`) and metadata tracing (active memory modes, warmups) out to the internal benchmarking JSON logs (Slices 1 & 9).

### Changed
- **Direct Planar Projections:** Reworked memory array conversions, completely decommissioning obsolete `interleaved` RGBA memory paths before output resizing. Frames map tensor chunks directly onto their planar destination outputs (Slice 0.7.4-3).
- **Fused Display Preview Chains:** Combined the standard checkerboard visualization passes directly with the sRGB display render step, removing fully redundant nested copies across interactive previewing (Slice 0.7.4-9).
- **Intelligent Parallel Loop Chunking:** Recompiled the legacy single-threaded processing (OFX writebacks, channel packing, and Gaussian Blurs) to execute concurrently on shared row-parallel loops (Slices 7 & 8).
- **Fused TensorRT Security Analysis:** Condense our mandatory statistical array scans and finite-value checking operations during TensorRT inference execution into a strictly unified single analytical pass (Slice 0.7.4-4).
- **Enforced Target Render Defaults:** Scrubbed ambiguous "Auto" selector rhetoric safely out of the UX controls, officially restricting the unconfigured plugin bootstrap environment directly to "Draft (512)".

### Fixed
- **DaVinci Resolve "Last Frame" Correctness:** Assured the internal wall-clock calculations accurately aggregate overlapping backend timestamps, preventing the OFX diagnostics panel from drastically double-counting elapsed frames. The deepest actionable bottleneck is safely preferred over bloated parent logic envelopes (Slice 0.7.4-2).
- **Corrupted Foreground Buffer Bindings:** Resolved an isolated black-silhouette rendering sequence affecting the explicit foreground arrays resulting from strict IO bounding policies (Slice 0.7.4-6).

## Assets & Downloads

### Windows
- **NVIDIA RTX 30 Series or newer:** Download `CorridorKey_Resolve_v0.7.4_Windows_RTX_Installer.exe`.
- **Windows DirectML track (experimental):** Download `CorridorKey_Resolve_v0.7.4_Windows_DirectML_Installer.exe`.
- Do not describe the DirectML installer as official support for every AMD, Intel, or RTX 20 series GPU family. Refer readers to `help/SUPPORT_MATRIX.md` for the real support designation.

## Installation Instructions

1. Close DaVinci Resolve if it is running.
2. Run the downloaded installer.
3. The installer automatically overwrites the previous version.
4. Launch DaVinci Resolve and load the plugin from the OpenFX Library.

## Uninstallation
To remove the plugin, go to **Windows Settings > Apps > Installed apps**, search
for "CorridorKey Resolve OFX", and click Uninstall.
