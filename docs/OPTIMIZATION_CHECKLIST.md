# Optimization Checklist

## Why This Document Exists

This document is the single resume point for the runtime optimization work.
It keeps the effort bounded, measurable, and easy to restart without relying
on previous chat history. Read this file before starting a new optimization
slice.

The work described here follows [ARCHITECTURE.md](./ARCHITECTURE.md),
[GUIDELINES.md](./GUIDELINES.md), and the official ONNX Runtime, NVIDIA, and
Microsoft documentation already chosen for this effort.
Recorded benchmark checkpoints live in
[OPTIMIZATION_MEASUREMENTS.md](./OPTIMIZATION_MEASUREMENTS.md).

## Why Scope Control Matters

This optimization track exists to improve long-lived ORT workloads without
changing the product contract. Every slice must preserve runtime behavior,
fallback semantics, diagnostics, and OFX isolation while making measurement
clearer or performance better.

- Active feature branch for this work: `perf/optimization`
- Baseline display version: `0.7.3`
- Optimization checkpoint display versions: `0.7.4-X`
- Public API changes in `include/corridorkey/` are not part of this effort
- MLX stays unchanged unless build correctness requires a narrow fix
- Benchmark JSON changes must remain additive
- Canonical repo scripts stay authoritative for build, corpus, and release work
- Measurement comes before optimization in every phase

## Why The Checkpoint Version Contract Must Be Explicit

The optimization track now uses a fixed user-facing version policy so manual
tests can confirm the installed build without guesswork.

- `0.7.3` is the baseline build line
- `0.7.4` is the patch line reserved for optimization checkpoints
- `0.7.4-0` is the shared-ORT checkpoint
- `0.7.4-1` is the extract-output attribution checkpoint
- `0.7.4-2` is the runtime timing correction checkpoint
- `0.7.4-3` is the direct-planar-resize checkpoint
- `0.7.4-4` is the output-validation-fusion checkpoint
- `0.7.4-5` is the initial I/O-binding groundwork checkpoint
- `0.7.4-6` is the I/O-binding regression-fix checkpoint
- `0.7.4-7` is the host-postprocess and OFX-default-alignment checkpoint
- each new measured optimization slice increments the suffix:
  `0.7.4-7`, `0.7.4-8`, `0.7.4-9`
- the base semantic version remains `0.7.4` while the visible checkpoint label
  changes per slice
- checkpoint comparison only counts when the installed build identity was
  confirmed before the test

## Why The Current Baseline Matters

The baseline phase is complete enough to compare further slices without
guessing. The current codebase already has repeatable timing surfaces, shared
ORT process context plumbing, and a validated release flow.

### Phase 0: Baseline And Measurement Discipline

- [x] Existing timing surfaces in `src/core/engine.cpp`,
      `src/core/inference_session.cpp`, and
      `src/app/job_orchestrator.cpp` were preserved
- [x] ORT creation sub-stages were added:
      `ort_env_acquire`, `ort_session_options`,
      `ort_session_create`, `ort_metadata_extract`
- [x] Existing stage names were kept for report compatibility
- [x] Benchmark metadata was expanded to include `batch_size`, `tiling`,
      effective resolution, warmup runs, and steady-state runs
- [x] Official benchmark flow was extended through
      `scripts/run_corpus.sh` and `scripts/compare_benchmarks.py`
- [x] An OFX-equivalent benchmark harness was added without introducing a new
      public CLI command

### Phase 1: ORT Process-Level Architecture

- [x] Internal `OrtProcessContext` was introduced in `src/core/`
- [x] ORT environment ownership moved out of `InferenceSession`
- [x] ORT environment creation now uses global thread pools
- [x] ORT sessions disable per-session threads
- [x] ORT sessions enable `session.use_env_allocators=1`
- [x] Shared CPU arena allocator registration is handled at the env level
- [x] Retry paths, optimized-cache paths, provider setup, fallback behavior,
      diagnostics, and timing propagation were preserved
- [x] Context ownership is explicit and injected; this phase does not rely on
      a library-wide singleton
- [x] MLX behavior was kept intact apart from a narrow warning cleanup needed
      for strict Windows builds

## Why Validation State Must Be Explicit

This work only moves forward if the current slice is already proven stable.
The following validations were completed against the current implementation.

- [x] `scripts/build.ps1 -Preset debug`
- [x] `ctest --preset unit`
- [x] `ctest --preset integration`
- [x] `scripts/build.ps1 -Preset release`
- [x] Release unit test pass on `build/release`
- [x] Release integration test pass on `build/release`
- [x] CLI benchmark smoke test with JSON output
- [x] OFX benchmark harness smoke test with JSON output
- [x] Windows release packaging through the canonical release script
- [x] Local installer generation, bundle validation, and doctor validation
- [x] Optimization checkpoint release generated as `0.7.4-7`
- [x] Baseline and optimized installers were copied into
      `dist/optimization_checkpoints/` for sequential local A/B testing

## Why The Current Findings Change The Next Step

The next step should follow the newest measured bottleneck, not the oldest
intuition. The current repo-side harness now shows a real gain from attacking
the extract path directly, so the next manual plugin comparison can test
whether that gain survives the full OFX path.

- Shared cache reuse and engine reuse are active in the tested OFX flow
- TensorRT path stayed healthy during the sampled runtime-server session
- `frame_prepare_inputs` is material but not dominant in the sampled OFX run
- `ort_run` is material but not dominant in the sampled OFX run
- `frame_extract_outputs` was the dominant measured cost that motivated the
  last performance slice
- `0.7.4-1` now splits `frame_extract_outputs` and `batch_extract_outputs` into
  conservative sub-stages without removing the parent stage names
- repo-side synthetic benchmark smoke now shows `frame_extract_outputs_resize`
  as the dominant part of the extract block
- `0.7.4-1` also exposed a runtime panel semantics bug: `Last Frame` could
  double count nested timings and could report cached backend work as if it
  were the wall time of the current frame
- `0.7.4-2` fixes the runtime panel semantics so `Last Frame` reflects the
  measured wall time of the current frame while hotspot selection prefers the
  deepest actionable stage
- `0.7.4-3` removes the extra planar-to-interleaved image materialization pass
  before resize and adds direct planar resize/output population paths for both
  bilinear and Lanczos handling
- repo-side `512` benchmark harness comparisons between `0.7.4-2` and
  `0.7.4-3` on the same workspace showed:
  - CPU average latency improved from about `2578.4 ms` to `1596.8 ms`
  - CPU `frame_extract_outputs_resize` improved from about `79.0 ms` to
    `2.3 ms`
  - RTX average latency improved from about `446.9 ms` to `282.7 ms`
  - RTX `frame_extract_outputs_resize` improved from about `103.7 ms` to
    `8.9 ms`
- after `0.7.4-3`, the synthetic bottleneck shifts away from resize and back
  toward `ort_run` plus the remaining CPU-side preparation/extract work
- `0.7.4-4` fuses output-stat logging and finite-value validation into a
  single scan for the TensorRT high-resolution diagnostic path
- repo-side `2048` RTX harness comparisons between `0.7.4-3` and `0.7.4-4`
  on the same workspace showed:
  - average latency improved from about `1211.7 ms` to `1055.3 ms`
  - `frame_extract_outputs_tensor_materialize` improved from about `60.4 ms`
    to `20.5 ms`
  - `frame_extract_outputs_finalize` improved from about `66.8 ms` to
    `26.4 ms`
  - `frame_extract_outputs` improved from about `134.9 ms` to `54.2 ms`
- the first manual A/B between `0.7.3` and `0.7.4-0` did not show a speed gain
- the latest `0.7.4-0` local retest was confirmed by versioned runtime log and
  installed-bundle hash match
- latest sampled OFX raw averages were approximately:
  - `frame_prepare_inputs`: `315 ms`
  - `ort_run`: `414 ms`
  - `frame_extract_outputs`: `2619 ms`
  - `post_composite`: `100.5 ms`
- latest sampled OFX steady-state averages were approximately:
  - `frame_prepare_inputs`: `304 ms`
  - `ort_run`: `378 ms`
  - `frame_extract_outputs`: `2502 ms`
  - `post_composite`: `97.9 ms`
- current reading against the `0.7.3` baseline:
  - `frame_prepare_inputs`, `ort_run`, and `frame_extract_outputs` are
    effectively tied in steady-state
  - `post_composite`, `post_despill`, and `post_premultiply` are still worse
    on `0.7.4-0`
- current `0.7.4-2` status:
  - installer, doctor report, and bundle validation are ready for the next
    local plugin comparison
  - runtime panel semantics now match the intended reading order:
    wall time first, backend hotspot second
- current `0.7.4-3` status:
  - installer, doctor report, and bundle validation are ready for the next
    local plugin comparison
  - repo-side harness numbers now show the first clear throughput win of this
    optimization track
  - manual OFX comparison now also shows a real render-time reduction in the
    plugin path at `Maximum (2048)`
  - median top-level stage timings in the recorded local `0.7.4-3` window were
    about:
    - `frame_prepare_inputs`: `273.0 ms`
    - `ort_run`: `461.1 ms`
    - `frame_extract_outputs`: `476.5 ms`
    - `post_composite`: `110.1 ms`
  - current steady-state render opportunity is now split between `ort_run` and
    the remaining host-side extract work
  - current cold-start opportunity remains dominated by `ort_session_create`
    and `session_create_requested`
- current `0.7.4-4` status:
  - installer, doctor report, and bundle validation are ready for the next
    local plugin comparison
  - repo-side `2048` RTX harness now shows a second clear gain after the
    direct-planar-resize slice
  - the remaining steady-state render opportunity is now led by `ort_run`,
    then the still-material resize/finalize work inside `frame_extract_outputs`,
    then `frame_prepare_inputs`
  - the cold-start opportunity is still dominated by
    `ort_session_create` and `session_create_requested`
- `0.7.4-5` adds a narrow Windows RTX I/O-binding path that keeps the existing
  unbound path as the fallback and reuses session-owned bound output buffers
- `0.7.4-5` benchmark JSON now reports additive I/O-binding metadata with
  `requested_mode`, `eligible`, `active`, and `observed`
- repo-side `2048` RTX harness comparisons between `0.7.4-4` unbound and
  `0.7.4-5` auto-bound on the same workspace showed:
  - average latency improved from about `660.1 ms` to `477.2 ms`
  - `frame_extract_outputs` improved from about `53.5 ms` to `32.5 ms`
  - `frame_prepare_inputs` improved from about `28.1 ms` to `26.2 ms`
  - `ort_run` stayed effectively tied at about `413 ms`
- repo-side `3840x2160` sequence comparisons between `0.7.4-4` unbound and
  `0.7.4-5` auto-bound on the same workspace showed:
  - total duration stayed effectively flat at about `23.63 s`
  - `batch_extract_outputs` improved from about `377.2 ms` to `367.3 ms`
  - `batch_extract_outputs_resize` improved from about `318.5 ms` to
    `308.2 ms`
  - `sequence_infer_batch` stayed effectively tied and slightly higher at
    about `1495.3 ms` to `1509.0 ms`
- current `0.7.4-5` status:
  - repo-side measurement remains worth keeping because it shows a real
    single-frame extract-path gain even though sequence throughput stayed flat
  - the first packaged build exposed an OFX-visible foreground regression that
    must be treated as a blocker before Phase 3
- `0.7.4-6` fixes the bound single-frame foreground path so the OFX-visible
  result keeps a populated foreground image instead of collapsing to a black
  silhouette
- `0.7.4-6` also aligns the packaged output contract by output name instead of
  trusting raw output index order when binding named outputs
- `0.7.4-7` removes visible `Auto` wording from OFX selector choices that were
  still presented as selectable modes and makes `Draft (512)` the real default
  quality from the initial bootstrap path onward
- `0.7.4-7` also adds a fused bilinear resize path for planar alpha and
  foreground outputs plus parallel row execution for the OFX writeback and
  foreground linearization loops
- repo-side sequential `3840x2160` workload reruns between `0.7.4-6` and
  `0.7.4-7` stayed effectively flat to slightly worse:
  - `2048` total duration moved from about `122.3 s` to `123.9 s`
  - `512` total duration moved from about `146.7 s` to `148.4 s`
- current `0.7.4-7` status:
  - installer, doctor report, and bundle validation are ready for the next
    local plugin comparison
  - the quality default and visible selector language now match the agreed
    checkpoint policy
  - the repo-side corpus does not justify a broad speedup claim for this slice
  - the next high-value optimization work should focus on the still-dominant
    full-frame host path in `batch_prepare_inputs` and on extending the
    lower-copy output path beyond the current high-resolution bound path
- Ignore `CorridorHint` errors when they come from unrelated branch tests

## Why A Resume Map Saves Time

The files below are the current implementation footprint for the completed
optimization slices. Inspect them before changing architecture again.

- `src/core/ort_process_context.hpp`
- `src/core/ort_process_context.cpp`
- `src/core/inference_session.hpp`
- `src/core/inference_session.cpp`
- `src/core/inference_session_metadata.hpp`
- `src/core/engine.cpp`
- `src/core/engine_internal.hpp`
- `src/app/job_orchestrator.cpp`
- `src/app/ofx_session_broker.hpp`
- `src/app/ofx_session_broker.cpp`
- `src/app/runtime_diagnostics.cpp`
- `src/common/stage_profiler.hpp`
- `scripts/run_corpus.sh`
- `scripts/compare_benchmarks.py`
- `tests/unit/test_ort_process_context.cpp`
- `tests/unit/test_stage_profiler.cpp`
- `tests/integration/test_engine_warmup.cpp`
- `tests/integration/test_job_orchestrator.cpp`
- `tests/integration/test_ofx_session_broker.cpp`
- `tests/integration/ofx_benchmark_harness.cpp`

## Why Build Identity Must Be Verified Before Testing

Manual tests are only useful if the installed build identity is explicit.
Before recording a local result, verify the build in this order:

1. check the OFX panel version label
2. if needed, confirm the runtime version reported by the packaged CLI or
   doctor output
3. if ambiguity remains, compare the installed OFX bundle hash against the
   checkpoint bundle

The current expected visible identities are:

- baseline installer: `0.7.3`
- current optimization installer: `0.7.4-6`

## Why The Next Tasks Are Ordered

The pending work stays phase-ordered so that later GPU-path changes do not hide
basic measurement or lifetime mistakes. Do not skip ahead.

### Completed Slice: Extract Output Attribution

- [x] Split `frame_extract_outputs` into conservative internal sub-stages so
      the bottleneck becomes attributable instead of monolithic
- [x] Kept the existing `frame_extract_outputs` stage name for compatibility
      and added sub-stages under it rather than replacing it
- [x] Covered output tensor materialization, resize, and final consumer-visible
      output finalization
- [x] Extended tests only where needed to prove stage presence in synthetic and
      workload benchmark reporting
- [x] Generated the `0.7.4-1` installer and checkpoint artifacts for the next
      local comparison

### Completed Slice: Runtime Panel Timing Correction

- [x] Corrected `Last Frame` so it preserves the actual wall time of the
      current frame instead of summing overlapping backend timings
- [x] Corrected nested timing aggregation fallback so parent and child stages
      are not double counted when no wall-time sample is available
- [x] Updated hotspot selection to prefer the deepest actionable stage instead
      of the largest parent envelope
- [x] Added unit regression coverage for nested stage timings and cache-hit wall
      time handling
- [x] Generated the `0.7.4-2` installer and checkpoint artifacts for the next
      local comparison

### Completed Slice: Direct Planar Resize

- [x] Removed the extra intermediate interleaved image materialization before
      output resize in both frame and batch extract paths
- [x] Added direct planar-to-destination resize helpers for bilinear and
      Lanczos output handling while keeping the existing benchmark stage names
- [x] Parallelized the hot resize kernels with row-safe chunking so the work
      scales without changing observable output semantics
- [x] Added unit regression coverage proving the direct-planar paths match the
      previous resize results
- [x] Generated the `0.7.4-3` installer and checkpoint artifacts for the next
      local comparison

### Completed Slice: Output Validation Fusion

- [x] Fused TensorRT high-resolution output-stat collection and finite-value
      validation into one scan per buffer instead of two
- [x] Kept the same validation behavior and diagnostic payload on failure while
      reducing steady-state hot-path overhead on successful frames
- [x] Added unit coverage for the new analysis helper used by the fused path
- [x] Measured the slice on the repo-side RTX `2048` harness before packaging
- [x] Generated the `0.7.4-4` installer and checkpoint artifacts for the next
      local comparison

### Completed Slice: I/O Binding Groundwork

- [x] Designed a narrow ORT I/O Binding path without removing the current path
- [x] Gated the first implementation to the Windows RTX path
- [x] Bound both input and output tensors explicitly
- [x] Kept lifetime, ownership, and shape handling explicit with
      session-owned bound output buffers
- [x] Benchmarked bound and unbound paths in frame and sequence workloads
- [x] Added additive benchmark metadata so reports can distinguish requested,
      eligible, active, and observed binding state
- [x] Generated the `0.7.4-5` installer and checkpoint artifacts for the next
      local comparison

### Completed Slice: I/O Binding Regression Fix

- [x] Restored foreground buffer allocation on the bound single-frame path so
      the OFX-visible result keeps a valid foreground image
- [x] Reordered packaged output metadata by discovered output name so bound
      names, shapes, and element types stay aligned
- [x] Added unit regression coverage for output-order mapping and bound
      foreground-allocation decisions
- [x] Rebuilt and revalidated debug and release outputs before packaging
- [x] Generated the `0.7.4-6` installer and checkpoint artifacts for the next
      local comparison

### Phase 3: Device Tensors And Pinned-Host Strategy

- [ ] Introduce explicit device-aware memory placement where needed
- [ ] Add a pinned-host output path when the consumer still requires host
      visibility
- [ ] Keep synchronization explicit and documented
- [ ] Only evaluate reduced provider synchronization after the bound path is
      proven correct and measurable

### Phase 4: Move Input Preparation Off The CPU Hot Path

- [ ] Refactor input preparation so Windows RTX can support GPU-friendly
      preprocessing
- [ ] Preserve the current CPU path as the fallback and comparison baseline
- [ ] Minimize temporary host buffers
- [ ] Verify that lower `frame_prepare_inputs` cost also improves total work

### Phase 5: Move Selected Post-Process Steps Off The CPU Hot Path

- [ ] Separate raw output handling from post-process stages cleanly
- [ ] Move only low-risk post-process steps first
- [ ] Validate visual parity on representative frames
- [ ] Keep each moved step individually measurable

### Phase 6: TensorRT EP Refinement

- [ ] Revisit TensorRT provider options only after memory placement is under
      control
- [ ] Validate real workload profile coverage before enabling provider features
- [ ] Evaluate `user_compute_stream` and `enable_cuda_graph` only through
      measurement
- [ ] Keep only options that show measurable benefit without reliability loss

### Phase 7: Native Release Optimization

- [ ] Review final release compiler and linker settings
- [ ] Evaluate LTCG and practical PGO workflow through measurement
- [ ] Evaluate allocator changes only if benchmark evidence justifies them
- [ ] Keep packaging and debugging discipline intact

## Why The Reference Set Must Stay Fixed

The implementation path in this checklist is anchored to primary sources. When
resuming the work, keep these references as the default decision set:

- ONNX Runtime C and C++ guidance for shared envs, global thread pools, and
  shared allocators
- ONNX Runtime guidance for I/O Binding
- ONNX Runtime guidance for device tensors and pinned host memory
- ONNX Runtime CUDA Execution Provider performance notes
- ONNX Runtime TensorRT Execution Provider guidance, including
  `user_compute_stream`
- NVIDIA TensorRT RTX performance best practices
- Microsoft guidance for Windows release optimization only when it directly
  applies to the release phase

## Why Boundaries Must Stay Visible

The items below are not part of the current execution track. Keeping them out
of scope protects reviewability and keeps measurements interpretable.

- Do not port Python optimization code mechanically
- Do not reproduce Python FlashAttention or PyTorch-specific tricks
- Do not redesign the OFX product architecture
- Do not broaden support scope or backend policy in this track
- Do not keep speculative provider options without measurement

## Why A Restart Procedure Must Be Short

When resuming this work in a fresh session, use this order:

1. Read this document, [ARCHITECTURE.md](./ARCHITECTURE.md), and
   [GUIDELINES.md](./GUIDELINES.md)
2. Read [OPTIMIZATION_MEASUREMENTS.md](./OPTIMIZATION_MEASUREMENTS.md)
3. Inspect the implementation footprint listed above
4. Re-run unit and integration validation before changing behavior
5. Verify the current checkpoint version label before any manual OFX test
6. Re-run the official corpus flow before and after the next slice
7. Compare results with `scripts/compare_benchmarks.py`
8. Only then advance the checklist and start the next slice
