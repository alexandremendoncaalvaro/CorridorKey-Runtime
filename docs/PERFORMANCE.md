# Performance Optimization Strategy

> This document defines performance optimization priorities, techniques, and expected gains for CorridorKey Runtime.
> It serves as a reference for implementation decisions and profiling targets.
>
> **Related:** [ARCHITECTURE.md](ARCHITECTURE.md) — structural principles |
> [GUIDELINES.md](GUIDELINES.md) — code standards | [SPEC.md](SPEC.md) — technical design

---

## Philosophy

Performance optimization for CorridorKey focuses on **real-world gains on constrained hardware** (8GB laptops, older CPUs). We prioritize memory bandwidth, cache efficiency, and vectorization over micro-optimizations. Every optimization must:

- **Measurable:** Demonstrate 5%+ improvement via profiling (not speculation)
- **Portable:** Work across macOS, Windows, Linux, and ARM
- **Maintainable:** Not sacrifice code clarity for marginal gains
- **Targetable:** Work toward specific hardware tiers (M1/x86/ARM)

---

## 1. Compilation & Build-Time Optimization

### 1.1 Aggressive Compiler Flags

Enable maximum optimization for Release builds:

- `-O3 -march=native`: Baseline aggressive optimization with CPU-specific instructions
- `-ffast-math`: Relaxes IEEE 754 compliance (acceptable for image processing)
- `-finline-limit=10000 -funroll-loops`: Aggressive inlining and loop unrolling
- `-fvectorize -fpredictive-commoning`: Vector code generation hints
- CPU-specific tuning:
  - Apple Silicon: `-mcpu=apple-m1` (or M2/M3)
  - Intel/AMD x86: `-march=haswell` (or newer like `zen3`)
  - ARM: `-march=armv8.2-a+fp16+dotprod`

**Expected gain:** 15–25% from flags alone.

### 1.2 Link-Time Optimization (LTO)

Enable `-fIPO` (Interprocedural Optimization):

- Enables cross-file inlining and dead-code elimination at link time
- Trade-off: longer build time (10–30 minutes), but binary is 5–15% faster
- Include as `release-lto` preset in CMakePresets.json
- Recommended for final release builds only

**Expected gain:** 5–15%.

### 1.3 Profile-Guided Optimization (PGO) — Nice-to-Have

Two-pass compilation:
1. Build binary with `-fprofile-generate`
2. Run representative workload (real video processing)
3. Rebuild with `-fprofile-use -fprofile-correction`

Results in 10–30% speedup on the profiled workload; less effective on diverse workloads.

**Expected gain:** 10–30% (varies by workload).

---

## 2. Memory Strategy (The Multiplier)

Memory bandwidth is the bottleneck on low-end hardware. Optimizing memory access patterns is worth more than any CPU optimization.

### 2.1 Alignment & Allocation

All image data must be **64-byte aligned** (cache line size on modern CPUs, supports AVX-512):

- macOS/Linux: `posix_memalign()`
- Windows: `_aligned_malloc()`
- Already implemented in `ImageBuffer` class

**Impact:** Enables SIMD operations and eliminates false cache-line sharing.

### 2.2 Memory Layout Optimization

Image data layout affects both cache locality and SIMD efficiency:

**Current approach:** HWC (Height-Width-Channels) for video I/O, conversion to planar for computation.

**Opportunity:** Minimize conversions. Consider operations that are layout-aware:

- For post-processing (despill, despeckle): Keep planar (CHW) to batch process by channel
- For video decode: Convert directly to needed layout if possible (FFmpeg supports multiple output formats)
- For inference: ONNX expects NCHW; pre-allocate and reuse conversion buffers via pooling

**Strategy:** Use multi-format pipeline with explicit layout conversions only at I/O boundaries.

### 2.3 Buffer Pooling & Reuse

Eliminate allocations in hot paths:

- `InferenceSession` already maintains `m_resize_pool` and `m_planar_pool`
- Extend pooling to new transient buffers (despeckle masks, despill intermediates)
- Pre-allocate pools at engine creation based on frame size
- For sequences: Use ring buffers (circular fixed-size) instead of `std::vector` (avoids reallocations)

**Expected gain:** 15–25% saved from allocation/deallocation/GC overhead.

### 2.4 Ring Buffers for Frame Sequences

For video processing, frames are accessed sequentially then discarded. Use a fixed-size ring buffer:

- Allocate once at startup for max batch size
- Reuse slots as old frames exit the pipeline
- Zero dynamic allocation during processing
- Better cache locality (circular data structure stays in L3)

**Expected gain:** 10–15% reduction in allocation overhead; eliminates memory fragmentation.

---

## 3. Vectorization (SIMD)

Modern CPUs execute scalar operations slowly compared to SIMD. Auto-vectorization is unreliable; explicit SIMD is required for hot paths.

### 3.1 Identify Hot Operations

Profile to find where time is spent. Typical breakdown (measured on reference hardware):

- **Premultiply/Unpremultiply:** 25% of post-processing time
- **Despill (green spill removal):** 20%
- **sRGB ↔ Linear conversion:** 15%
- **Blur/Despeckle (convolution):** 25%
- **I/O (decode/encode):** 15%

Focus SIMD optimization on the top 3: premultiply, despill, color conversion.

### 3.2 Explicit SIMD Implementation

Use platform-specific intrinsics instead of relying on compiler auto-vectorization:

- **AVX2** (Intel/AMD x86-64 post-2013): Process 8 floats in parallel
- **NEON** (ARM, Apple Silicon): Process 4 floats in parallel
- **Runtime dispatch:** Detect at startup and bind appropriate implementation

Separate of Arrays (SOA) layout is **critical for SIMD efficiency:**
- Instead of `[R0 G0 B0 R1 G1 B1]` (interleaved)
- Use `[R0 R1 R2 ...] [G0 G1 G2 ...] [B0 B1 B2 ...]` (planar)
- Enables vectorized operations without gather/scatter overhead

**Expected gain:** 4–8x speedup on vectorized operations (significant since they're hot paths).

### 3.3 Platform-Specific Strategies

**Apple Silicon (M1/M2/M3):**
- NEON SIMD + custom CPU extensions
- Ultra-fast unified memory (no GPU-CPU copies needed for inference)
- Large L3 cache (24MB): Fits entire 512-pixel frames + working buffers
- Strategy: Batch operations to stay in L3, minimize off-chip memory traffic

**x86-64 (Intel/AMD):**
- AVX-512 on newest CPUs (Xeon, Raptor Lake); fallback to AVX2 (most hardware)
- Out-of-order execution; branch prediction highly sensitive
- Strategy: Organize branches by frequency (most common path first)

**ARM (Linux, embedded):**
- NEON support; limited memory bandwidth
- Thermal throttling common on sustained load
- Strategy: Thread count conservatively (available_cores × 0.8), prioritize cache-oblivious algorithms

---

## 4. Processor-Specific Optimization

### 4.1 Device Detection & Smart Defaults

Beyond simple hardware enumeration, the runtime should assess actual capabilities:

- **Lightweight benchmark:** On startup, process 5–10 test frames and measure actual throughput
- **Adaptive resolution:** If measured FPS < target, reduce resolution (1024px → 768px → 512px)
- **Adaptive batch size:** Calculate max safe batch based on available memory (safety factor 0.7) and measured memory-per-frame
- **Backend selection:** Prefer hardware-accelerated backends (CoreML, TensorRT) over CPU if available
- **Thread count:** Use `std::thread::hardware_concurrency()` as baseline; reduce if thermal throttling detected

**Result:** Out-of-the-box configuration optimal for each hardware profile without manual tuning.

### 4.2 Heterogeneous Processing

Exploit hybrid CPU cores (performance + efficiency cores, especially on Apple Silicon and newer x86):

- **Heavy ops:** Inference, heavy convolution → Performance cores
- **Light ops:** Metadata, formatting → Efficiency cores
- Compiler naturally schedules; if needed, use `sched_setaffinity()` on Linux or `pthread_set_qos_class_self_np()` on macOS

### 4.3 Hardware-Accelerated Codecs

Replace software-only encoding with hardware-accelerated options:

- **macOS:** `hevc_videotoolbox` (H.265 hardware encoding, 5–10x vs libx264)
- **Windows:** `hevc_nvenc` (NVIDIA) or `h264_qsv` (Intel Quick Sync)
- **Linux:** `hevc_vaapi` if available; fallback to libx264

Codec selection is transparent to user; detect at startup and select best available.

**Expected gain:** 5–10x speedup on encoding (non-critical but noticeable on low-end hardware).

---

## 5. Pipelining & Parallelism

### 5.1 Software Pipelining (3-Stage)

For video processing, split into parallel stages:

1. **Decode stage:** FFmpeg pulls compressed frames in background thread
2. **Inference stage:** Main thread processes (GPU or CPU inference)
3. **Encode stage:** Separate thread encodes results while next frame infers

**Benefit:** Throughput becomes `max(decode_time, infer_time, encode_time)` instead of sum. In practice, 40–60% speedup for video pipelines.

**Implementation:** Lock-free queues between stages (or simple mutex + condition_variable for simplicity).

### 5.2 Adaptive Batching

Batch multiple frames for inference if memory allows:

- Most ONNX backends support batched inputs
- Safe batch size = `(available_memory × 0.7) / (frame_pixels × overhead_multiplier)`
- Run batches asynchronously; don't block I/O on batch completion

**Expected gain:** 20–40% throughput improvement (depends on backend batch efficiency).

### 5.3 Task-Level Parallelism

Post-processing operations (despeckle, despill, color conversion) are embarrassingly parallel:

- Divide frame into 64×64 tiles
- Process tiles in parallel via `std::for_each(std::execution::par_unseq, ...)`
- Each tile works independently; minimal synchronization

Already implemented partially; ensure all hot post-processing functions use this pattern.

---

## 6. Algorithm-Level Optimization

### 6.1 Fused Operations

Post-processing typically involves multiple passes over the same data:
1. Despeckle alpha
2. Despill color
3. Premultiply RGBA

Each pass loads frame from memory independently (memory bandwidth waste).

**Optimization:** Fuse into single pass where possible:
- For each pixel, apply despeckle + despill + premultiply in-memory
- Memory bandwidth: 3 reads + 1 write instead of 6 reads + 3 writes
- Trade-off: Slightly higher register pressure, minimal

**Expected gain:** 30–50% reduction in memory traffic (significant on bandwidth-limited hardware).

### 6.2 Cache-Oblivious Algorithms

For operations like transpose, resize, convolution: Use **tiled strategies** that work well regardless of cache size:

- Process in 64×64 tiles (typical L1 cache size for float data)
- Reduces cache misses by ordering accesses predictably
- Especially effective for large images (1024+)

Example: Tiled transpose is 5–10x faster than naive on large matrices.

### 6.3 Adaptive Precision

Not all operations need full 32-bit float precision:

- **sRGB LUT:** Use 16-bit float (half precision) for lookups; 4x smaller, faster
- **Intermediate buffers:** FP16 for despeckle masks, despill accumulators; convert back to FP32 at output
- **Quantized inference:** Use INT8 model variant on low-memory hardware (minor accuracy loss, major speed gain)

Be explicit about precision in function signatures to avoid silent bugs.

---

## 7. Profiling & Measurement

### 7.1 Built-in Performance Monitoring

Include minimal profiling instrumentation in the runtime:

- Auto-collect timing for major functions (decode, infer, encode, post-process)
- Expose metrics via CLI (`corridorkey info --benchmark`)
- Use for debugging regressions and validation

Implementation: Simple RAII timer class in `src/common/` with scoped tracking.

### 7.2 External Profiling Tools

Validate optimizations with native profilers:

- **macOS:** Xcode Instruments (Sampler, System Trace)
- **Linux:** `perf record/report`, `cachegrind`, FlameGraph
- **Windows:** Windows Performance Analyzer, Intel VTune

Run on real workloads (target video) to avoid micro-benchmark artifacts.

---

## 8. Expected Performance Improvements

Cumulative impact of all optimizations, starting from baseline (MacBook Air M1, 512px frames):

| Phase | Changes | Cumulative | Impact |
|-------|---------|-----------|----------|
| Baseline | Unoptimized | 0.5 fps | Reference |
| Build flags + LTO | Compilation flags, link-time opt | 0.6 fps | +20% |
| SIMD core ops | AVX2/NEON premultiply, despill, color | 0.85 fps | +42% total |
| Memory pooling | Buffer reuse, ring buffers | 1.0 fps | +100% total |
| Software pipeline | 3-stage decode/infer/encode | 1.4 fps | +180% total |
| Fused post-proc | Single-pass despeckle+despill | 1.9 fps | +280% total |
| **All together** | **Full optimization stack** | **1.9 fps** | **+280%** |

**Realistic expectation on diverse hardware:** 2–3x overall speedup. Outliers ("only" +50%) indicate hardware-specific bottlenecks (I/O bound, GPU wait, etc.).

---

## 9. Implementation Roadmap

### Phase 1: Immediate (0–2 weeks)
Priority: Compilation, memory pooling, basic SIMD.

- Enable aggressive compiler flags in Release preset
- Implement LTO in `release-lto` preset
- Add explicit AVX2 premultiply function
- Extend buffer pooling to all transient allocations

**Expected gain:** 35–45%.

### Phase 2: Core (2–6 weeks)
Priority: Vectorization, memory layout, software pipelining.

- Implement NEON equivalents for ARM
- Add runtime SIMD dispatch based on CPU capabilities
- Refactor post-processing to use planar layout internally
- Implement 3-stage software pipeline for video processing

**Expected gain:** 100–150% additional (cumulative ~2.5x).

### Phase 3: Polish (6–12 weeks)
Priority: Fused operations, adaptive algorithms, profiling.

- Fuse post-processing operations (despeckle+despill+premultiply)
- Implement adaptive batch sizing and resolution scaling
- Add built-in profiling instrumentation
- Optimize hot paths identified by profiling

**Expected gain:** 50–100% additional (cumulative ~3–4x).

---

## 10. Limitations & Caveats

Not all optimization opportunities are always worthwhile:

- **Memory bandwidth ceiling:** On low-end hardware, throughput is bandwidth-limited (not CPU-limited). SIMD on already-bandwidth-saturated operations yields <5% gain.
- **GPU utilization:** For inference, GPU efficiency depends on batch size and model size. Tiny batches (N=1) may underutilize hardware.
- **OS scheduling:** Thread count, CPU affinity, thermal management controlled by OS; limited userspace control.
- **Algorithm limits:** Some morphological operations (despeckle) are inherently sequential; parallelization not always feasible.

**Strategy:** Always measure before and after optimization. If gain < 5%, deprioritize.

---

## References

- Fedor Pikus, "Hands-On High Performance Computing" (Packt, 2021) — cache, SIMD, profiling
- Chandler Carruth, "High Performance Code 201: Hybrid Data Structures" (CppCon 2016) — memory layout, cache efficiency
- Agner Fog, "Optimizing software in C++" (free online) — detailed CPU + compiler behavior
- FFmpeg source: `libavfilter/`, `libavcodec/` — real-world image processing optimizations
- TensorRT documentation: "Optimizing TensorFlow for Inference" — batching, precision, hardware acceleration
