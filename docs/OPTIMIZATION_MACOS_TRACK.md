# macOS Apple Silicon Optimization Track

## Why This Document Exists

The macOS Apple Silicon product track went through a telemetry and packaging
pass in v0.7.5 but not yet through a device-resident pipeline pass comparable
to the Windows RTX v0.7.4 series. This document is the single resume point for
the macOS performance work. It captures the methodology, the baseline
numbers, and the per-slice measurements so each step can be validated in
isolation and the cumulative improvement is auditable.

Read this together with [ARCHITECTURE.md](./ARCHITECTURE.md),
[GUIDELINES.md](./GUIDELINES.md), and
[OPTIMIZATION_MEASUREMENTS.md](./OPTIMIZATION_MEASUREMENTS.md) (the
parallel Windows RTX measurement log).

## Why Scope Control Matters

The macOS track targets Apple-native paths: VideoToolbox for decode and
encode, IOSurface-backed `CVPixelBuffer` pools, Metal Performance Shaders and
custom Metal compute kernels for pre and post processing, Core ML for the
Neural Engine, MetalFX for preview upscale, and an async three-stage
pipeline for decode-infer-encode overlap.

- Active feature branch: `perf/macos-optimization`
- Baseline display version: `0.7.5`
- Checkpoint display versions: `0.7.5-X` for the pipeline track,
  `0.7.7-X` for the Core ML / ANE track, `0.7.8-X` for OFX Metal and
  MetalFX integration, `0.8.0-X` for model-level adaptation
- Cross-platform surface stability: the OFX panel, CLI flags, and preset
  catalog stay identical across macOS, Windows, and Linux. Implementation
  diverges freely between platforms.
- Public API in `include/corridorkey/` does not grow to accommodate Apple
  framework types; all platform specifics are wrapped in `src/`.
- MLX stays available as the fallback backend; Core ML becomes the default
  when its export is validated.
- Measurement comes before optimization in every phase.

## Why The Checkpoint Version Contract Must Be Explicit

- `0.7.5` is the telemetry and packaging baseline (shipped)
- `0.7.5` is the pipeline track patch line. Individual slice labels:
  - `0.7.5-1` baseline checkpoint
  - `0.7.5-2` hardware ProRes encoder default
  - `0.7.5-3` IOSurface-backed frame pool
  - `0.7.5-4` VideoToolbox hardware decoder
  - `0.7.5-5` fused Metal preprocessing kernel
  - `0.7.5-6` `MPSImageLanczosScale` for upscale
  - `0.7.5-7` fused Metal postprocessing kernel
  - `0.7.5-8` async three-stage pipeline
  - `0.7.5-9` MLX zero-copy input via `MTLBuffer`
  - `0.7.5-10` SKU-aware runtime defaults
  - `0.7.5-11` v0.7.5 release
- `0.7.7` is the Core ML / ANE track patch line
- `0.7.8` is the Resolve integration track (OFX Metal + MetalFX preview)
- `0.8.0` is the model-level adaptation (ANE surgery, distillation, W4A16)

The base semantic version advances when a release tag lands. Between
releases only the slice suffix moves.

## Why Measurement Comes First

Every slice commits a `dist/benchmarks/bench_m4_<slice>.json` output from
`scripts/benchmark_mac_m4.sh` plus a short summary in this document. The
benchmark harness already captures the fields that matter for this track:
load breakdown, per-bridge steady-state latency, peak RSS, system wired
memory, and optionally the `powermetrics` ANE/GPU/CPU sample log.

### Benchmark protocol

The benchmark command is:

```
scripts/benchmark_mac_m4.sh --resolutions "512,768,1024,1536,2048" \
    --tag "<slice-label>" \
    --output dist/benchmarks/bench_m4_<slice>.json
```

On Mac Mini M4 16 GB the runtime is identical to the user-facing release
build. Benchmark JSON files live under `dist/benchmarks/` which is
gitignored; the prose summary in this document is the committed record.

## Checkpoint Matrix

| Checkpoint | Version | Main change | Strongest recorded gain | Notes |
| --- | --- | --- | --- | --- |
| `baseline` | `0.7.5` | shipped telemetry and packaging | baseline only | see the baseline section |
| `pipeline_baseline` | `0.7.5-1` | branch-local baseline under the new track | baseline only | fresh measurement after branch rename |
| `hw_prores_encoder` | `0.7.5-2` | ProRes 4444 via VideoToolbox as default lossless encoder for `.mov` | 4K real workload `video_encode_frame` dropped from about 156 ms to 95.6 ms per frame (about 39 percent), peak RSS was unchanged | keep; full-pipeline end-to-end improved by 24 ms per frame because swscale now pays the BGRA to ayuv64le conversion that the VT encoder needs, and the win is amplified once the preprocessing slices remove that CPU conversion |

## Baseline (`0.7.5-1`)

Recorded on Mac Mini M4 16 GB (Mac16,10), macOS 26.3.1 build 25D771280a,
MLX 0.31.0 bridges, release-macos-portable build. Synthetic benchmark, 2
warmup + 5 steady-state runs at each bridge resolution.

### Synthetic load and per-frame at each bridge

| Bridge | Cold ms | Steady avg ms | Steady FPS | Warmup first-run ms | MLX materialize per-frame ms | Peak RSS MB |
| --- | --- | --- | --- | --- | --- | --- |
| 512  | 1313 | 291 | 3.4 | 1014 | 0 (batched) | 197 |
| 768  |  675 | 331 | 3.0 |  352 | 286 | 400 |
| 1024 |  707 | 332 | 3.0 |  370 | 287 | 467 |
| 1536 |  806 | 371 | 2.7 |  426 | 288 | 614 |
| 2048 |  965 | 437 | 2.3 |  497 | 286 | 817 |

Observations that shape the next slices:

- `mlx_materialize_outputs` lands on a near-flat 286 ms per frame from 768
  through 2048. This is the Metal command submission and wait floor
  documented upstream in [MLX #1192](https://github.com/ml-explore/mlx/issues/1192)
  and the Metal dispatch model in general. Pure MLX optimizations cannot
  move this number much; the Core ML ANE path in v0.7.7 can, because the
  ANE has a different dispatch model.
- Peak RSS stays under 1 GB even at 2048 synthetic. A synthetic benchmark
  does not expose video decode or encode memory; real 4K workload tops out
  around 2 GB at 1024 bridge.
- Steady FPS is the direct consequence of the 286 ms floor: the per-frame
  budget is dominated by the Metal wait, not by bridge compute size.

### 4K real workload reference

Recorded on the same hardware with
`assets/video_samples/mixkit-girl-dancing-with-her-earphones-on-a-green-background-28306-4k.mp4`
(336 frames at 4K) through the 1024 bridge, synchronous path, MLX backend:

| Stage | Per frame |
| --- | --- |
| Video decode (libav SW) | 22 to 33 ms |
| Input prep (resize + normalize + pack, CPU) | 27 ms |
| MLX eval + wait | 275 to 299 ms |
| Output extract + alpha upscale (Lanczos, CPU) | 50 ms |
| Post-process (despill, premultiply, composite, CPU) | 50 ms |
| Video encode (qtrle lossless, CPU) | 138 to 156 ms |
| Total | about 730 ms |
| FPS end-to-end | about 1.37 |

The encode stage alone accounts for 21% of the per-frame budget and is
the largest single target for slice `0.7.5-2`.

## Slice `0.7.5-2` Result (Hardware ProRes Encoder)

Same hardware and input as the baseline. The FFmpeg build available in
`build/release-macos-portable` includes `prores_videotoolbox`, so the
lossless encoder selection for `.mov` now returns it ahead of `qtrle`
and `png` on Apple builds.

| Metric | Baseline `0.7.5-1` | Slice `0.7.5-2` | Delta |
| --- | --- | --- | --- |
| Encode per frame (`video_encode_frame`) | about 156 ms | 95.6 ms | about minus 60 ms (minus 39 percent) |
| Total per frame (`video_infer_batch`) | about 730 ms | 706 ms | about minus 24 ms (minus 3 percent) |
| Peak RSS | about 2.0 GB | about 2.0 GB | unchanged |
| End-to-end FPS | about 1.37 | 1.42 | plus about 0.05 |

The encode stage moved to the Media Engine as designed. The residual
encode cost is the CPU-side BGRA to ayuv64le conversion that FFmpeg
`swscale` does to feed the VT encoder the YUVA 4:4:4 16-bit format it
requires. A later slice that makes the entire output path zero-copy
through Metal textures and IOSurface-backed CVPixelBuffers will remove
that conversion and push the encode closer to 15 to 25 ms per frame on
the Media Engine.

## Plan Summary

See the approved plan for the slice-by-slice detail. Each slice is an
atomic commit on the `perf/macos-optimization` branch using
Conventional Commits prefixes (`perf`, `feat`, `fix`, `docs`, `test`,
`refactor`, `chore`) per [GUIDELINES.md](./GUIDELINES.md) §9.2. Each
slice refreshes the Checkpoint Matrix above with its measured delta.
