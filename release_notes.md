## Overview

v0.7.5 delivers the comprehensive runtime optimization track for Windows RTX and Apple Silicon, migrating the heaviest host-side (CPU) bottlenecks to fully device-resident GPU streams (CUDA, NPP, ONNX Runtime I/O Binding on Windows; Apple Accelerate on macOS). End-to-end processing for full-frame 4K workflows has improved radically, cutting individual module latencies by 70% to 90%. This release also adds blue-screen keying support, a system-wide CLI on Windows, a hardened CI/CD pipeline, and several stability fixes. The Windows RTX and Apple Silicon bundles ship together.

## Changelog

### Added
- **GPU-Accelerated Output Resize Pipeline (Windows RTX):** Planar outputs now resize completely on the GPU leveraging NVIDIA Performance Primitives (NPP), bypassing over 300ms of costly host-side scale processing per frame.
- **GPU-Accelerated Input Preparation (Windows RTX):** Alpha hint splices, image background normalizations, and initial tensor scaling loops run on an asynchronous device-resident CUDA/NPP path (`GpuInputPrep`), cutting start-of-frame execution time by ~74%.
- **Pinned Host Memory (Windows RTX):** Output extraction uses `cudaMallocHost` via a custom `PinnedBuffer` allocator, preventing asynchronous DMA fallback stalls.
- **TensorRT I/O Binding (Windows RTX):** Narrow I/O binding path keeps tensors device-resident through the full inference cycle, eliminating host round-trips for packaged TensorRT contexts.
- **Vectorized CPU Matrix Math:** AVX2 F16C hardware intrinsics for fast FP16-to-FP32 tensor materialization on Windows, with safe architectural gating on Apple Silicon.
- **Apple Accelerate Vectorization (macOS):** Premultiply, alpha levels, image clamping, and temporal smoothing now use Apple Accelerate vDSP/vImage, delivering significant speedups on Apple Silicon.
- **MLX Input Prep Vectorization (macOS):** Vectorized MLX input preparation path with transparency fix using Accelerate framework.
- **MLX Load-Time Breakdown Stages (macOS):** `benchmark --json` now reports per-stage MLX initialization (`mlx_artifact_resolve`, `mlx_buffer_alloc`, `mlx_bridge_import`, `mlx_jit_compile`) and the first-frame compilation cost (`engine_warmup_first_run`) separately from steady-state inference, so load time is auditable field-by-field.
- **Bridge-Aware Warmup (macOS):** The warmup frame is now sized to the active MLX bridge resolution (512–2048) instead of a 64×64 dummy, which forces JIT kernel materialization on the real working shape. First real frame after warmup pays no extra compilation cost.
- **System Memory Telemetry:** `benchmark --json` reports `system_metrics.peak_ram_mb` and `system_metrics.system_wired_mb` (macOS unified memory wired pages) for post-run memory audits.
- **`scripts/benchmark_mac_m4.sh`:** Repeatable harness that sweeps the bridge resolutions, optionally captures `powermetrics` ANE/GPU/CPU samples, and emits `dist/bench_m4_<host>_<timestamp>.json` for release validation.
- **Global ORT Thread Pooling Context:** ONNX Runtime context ownership relocated to a cross-plugin shared `OrtProcessContext`, disabling expensive per-session thread limitations and enabling `use_env_allocators=1`, halving multi-instance RAM and bootstrap costs.
- **Blue-Screen Keying:** Canonicalized OFX flow now supports blue-screen chroma key in addition to green-screen.
- **Granular Execution Metrics:** Dense, non-destructive stage timers (`tensor_materialize`, `resize`, `finalize`, `ofx_broker_writeback`) and metadata tracing (active memory modes, warmups) in benchmarking JSON logs.
- **System-Wide CLI (Windows):** The Windows installer now registers the CorridorKey bundle's `Contents\Win64\` directory on the machine `PATH`, so `corridorkey.exe` is callable from any terminal after install. The bundled `install_plugin.bat` manual path mirrors the same behavior. Uninstall cleanly removes the entry.
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
- **Apple Silicon (M1 and newer):** Download `CorridorKey_Resolve_v0.7.5_macOS_AppleSilicon.pkg` (recommended) or `CorridorKey_Resolve_v0.7.5_macOS_AppleSilicon.dmg`. The `CorridorKey_Runtime_v0.7.5_macOS_AppleSilicon.zip` CLI runtime is available for scripted/headless use.

## macOS Performance Reference (Mac Mini M4, 16 GB)

All figures were measured on a Mac Mini M4 (10-core CPU, 10-core GPU, 16 GB unified memory, macOS 26.3.1) using the `mlx` backend with `corridorkey_mlx.safetensors` and the matching `_bridge_{512,768,1024,1536,2048}.mlxfn` exports. Synthetic numbers come from `corridorkey benchmark` (2 warmup + 5 steady-state runs on zero-filled buffers); the 4K workload row comes from the `mixkit-girl-dancing` 4K sample with 336 decoded frames end-to-end (decode + inference + post + encode).

| Bridge | Synthetic cold / avg (ms) | Synthetic steady fps | Peak RSS (MB) |
|---|---|---|---|
| 512  | 1111 / 283 | 3.5 | 348 |
| 768  |  652 / 310 | 3.2 | 401 |
| 1024 |  688 / 327 | 3.1 | 469 |
| 1536 |  790 / 352 | 2.8 | 616 |
| 2048 |  897 / 406 | 2.5 | 826 |

| 4K real workload | Per frame | FPS | Peak RSS |
|---|---|---|---|
| 4K video @ 1024 bridge (full pipeline: decode + infer + post + encode) | ~730 ms | 1.37 | ~2.0 GB |

Per-frame breakdown at 4K / 1024: MLX compute (`mlx_materialize_outputs`) ~299 ms, video encode ~156 ms, source passthrough ~38 ms, video decode ~33 ms. Post-process stages (despill, despeckle, premultiply, composite) total <50 ms — they are not a bottleneck on this hardware.

**Honest comparison vs Windows RTX 3080:** The Windows RTX baseline is ~1 s load and ~1 s/frame at 4K / 2048 on a 10 GB RTX 3080 with 32 GB system RAM. Mac Mini M4 (16 GB) has roughly 6× less FP32 compute (4.6 vs 29.8 TFLOPS), no FP16 tensor cores, ~6× less memory bandwidth (120 vs 760 GB/s), and unified memory shared with the OS. Expect ~5–7× slower per-frame throughput at the same internal resolution. The 512 and 1024 bridges are the sweet spot on 16 GB M4; 2048 runs but without meaningful quality gain because of the gap vs the Windows RTX 2048 pipeline.

**Recommended internal resolution on 16 GB M4**: start at Draft (512) — the plugin default — and bump to High (1024) only when edge quality matters. Ultra (1536) and Maximum (2048) are supported but mostly useful on M4 Pro / M4 Max with 24 GB+.

## Installation Instructions

### Windows
1. Close DaVinci Resolve if it is running.
2. Run the downloaded installer (or extract the zip and run `install_plugin.bat` as Administrator).
3. The installer automatically overwrites the previous version and registers `corridorkey.exe` on the system `PATH`.
4. Open a new terminal to pick up the PATH change.
5. Launch DaVinci Resolve and load the plugin from the OpenFX Library.

### macOS (Apple Silicon)
1. Close DaVinci Resolve if it is running.
2. Double-click the downloaded `.pkg` and follow the installer. The OFX bundle is installed to `/Library/OFX/Plugins/CorridorKey.ofx.bundle` and the CLI tool ships inside the bundle at `Contents/Resources/bin/corridorkey`.
3. If macOS shows a quarantine prompt for a direct `.dmg` download, approve the plugin under System Settings → Privacy & Security, or run `xattr -dr com.apple.quarantine /Library/OFX/Plugins/CorridorKey.ofx.bundle`.
4. Launch DaVinci Resolve and load the plugin from the OpenFX Library.

## Uninstallation

### Windows
Go to **Windows Settings > Apps > Installed apps**, search for "CorridorKey Resolve OFX", and click Uninstall. The uninstaller removes the bundle and restores the previous system `PATH`.

### macOS
Remove `/Library/OFX/Plugins/CorridorKey.ofx.bundle`. A `sudo rm -rf /Library/OFX/Plugins/CorridorKey.ofx.bundle` is sufficient; there is no shared state outside the bundle.
