# Optimization Measurements

## Why This Document Exists

This document defines the comparison protocol for optimization work and keeps
the recorded checkpoints in one place. The goal is to compare each slice
against a stable baseline instead of relying on memory or one-off tests.

Use this file together with [OPTIMIZATION_CHECKLIST.md](./OPTIMIZATION_CHECKLIST.md).

## TL;DR

This table is the fast reading path for the optimization track. It summarizes
the strongest measured outcome of each checkpoint and the current decision about
whether that slice was worth keeping. The detailed sections later in this file
remain the source of truth for methodology and caveats.

| Checkpoint | Version | Main change | Strongest recorded gain | Current reading |
| --- | --- | --- | --- | --- |
| `pre_opt` | `0.7.3` | baseline reference | baseline only | keep as the control |
| `phase_0_1_shared_ort` | `0.7.4-0` | shared ORT env, global pools, env allocators, richer timings | architecture and measurement improved, but no clear OFX throughput win | keep for structure, not for speed claims |
| `phase_1_extract_output_attribution` | `0.7.4-1` | split extract path into sub-stages | converted a monolith into actionable hotspots | keep for visibility |
| `phase_1_runtime_panel_timing_correction` | `0.7.4-2` | fixed `Last Frame` wall-time semantics | removed panel double counting | keep for trustworthiness |
| `phase_1_direct_planar_resize` | `0.7.4-3` | removed extra planar-to-interleaved resize path | RTX `512` harness average latency improved about `36.7%` | first clear throughput win |
| `phase_1_output_validation_fusion` | `0.7.4-4` | fused TensorRT diagnostic scans | RTX `2048` harness average latency improved about `12.9%` | keep |
| `phase_2_io_binding` | `0.7.4-5` | narrow low-copy bound path for packaged TensorRT outputs | RTX `2048` harness average latency improved about `27.7%` | promising, but first package had a blocking correctness bug |
| `phase_2_io_binding_fix` | `0.7.4-6` | fixed bound foreground correctness and output ordering | correctness replacement for `0.7.4-5` | keep as the valid Phase 2 base |
| `phase_3_host_postprocess` | `0.7.4-7` | default-quality cleanup and host-side output/writeback work | no broad corpus speedup claim justified | keep for product correctness, not for speed claims |
| `phase_4_input_prepare` | `0.7.4-8` | parallelized host-side prepare path and fused normalized planar packing | `1024` OFX-style harness roundtrip improved about `10.9%`; `frame_prepare_inputs` improved about `24.2%` | keep; this is the current best prepare-focused win |
| `phase_5_preview_writeback` | `0.7.4-9` | full-frame OFX harness fidelity, fused preview composite, and measured broker writeback | full-frame `2048 -> 3840x2160` OFX-style harness average latency improved about `8.1%`; `post_composite` improved about `81.9%` | keep; this is the first measured full-frame OFX-style gain after the prepare slice |
| `phase_6_device_tensors` | `0.7.4-10` | pinned host output buffers via CUDA and vectorized FP16-to-FP32 conversion via F16C intrinsics | full-frame `2048 -> 3840x2160` OFX-style harness average latency improved about `2.7%`; `ort_run` improved about `3.7%`; `frame_prepare_inputs` improved about `9.1%` | keep; modest but real DMA and vectorization win with correct `memory_mode: pinned` metadata |
| `phase_7_gpu_resize` | `0.7.4-11` | device-resident tensor flow and GPU-accelerated NPP bilinear resize | full-frame `2048 -> 3840x2160` OFX-style harness average latency improved about `28%`; `frame_extract_outputs_resize` down to `27ms` | keep; massive win effectively eliminating the strongest remaining CPU hotspot |
| `phase_8_gpu_prepare` | `0.7.4-12` | GPU-accelerated input preparation via NPP resizing, splitting, and normalization | full-frame `2048 -> 3840x2160` OFX-style harness `frame_prepare_inputs` improved by `~74%` | keep; effectively eliminates the final CPU bottleneck, achieving end-to-end device residence |

Latest real OFX sample currently recorded in the workspace:

| Sample | Version | Quality | `frame_prepare_inputs` | `ort_run` | `frame_extract_outputs` | `frame_extract_outputs_resize` | `post_composite` |
| --- | --- | --- | --- | --- | --- | --- | --- |
| most recent local plugin log | `0.7.4-8` | `Maximum (2048)` | `83.3 ms` | `372.2 ms` | `344.0 ms` | `287.7 ms` | `97.5 ms` |

Current headline:

- the `prepare_inputs` slice worked
- the `preview_writeback` slice also worked on the full-frame OFX-style harness
- the next real hotspot is back to `ort_run` plus the resize-heavy extract path
- the attempted lower-rung bound-path expansion and resize-map caching did not
  justify themselves and were discarded instead of being carried forward

## Why The Matrix Must Stay Stable

The same workloads must be reused across checkpoints so gains and regressions
stay attributable. Unless a checkpoint explicitly changes the matrix itself,
keep these scenarios fixed:

- `synthetic_cpu_512`
- `synthetic_primary`
- `frame_4k_tiled`
- `sequence_20`
- `video_4k_short`
- `ofx_repeated_render`
- `manual_ofx_local_test`

The canonical automation path for repo-side measurements remains:

- `scripts/run_corpus.sh`
- `scripts/compare_benchmarks.py`

## Why Checkpoint Names Must Be Stable

Each recorded state gets a stable checkpoint label. Use the same label in:

- installer filenames copied for local testing
- corpus output directories
- benchmark comparison notes
- this measurement ledger

Checkpoint installers are meant for sequential local testing. They share the
same installed product identity and overwrite the previous local plugin state.

Display version policy for this track:

- baseline remains `0.7.3`
- optimization checkpoints use `0.7.4-X`
- shared-ORT checkpoint is `0.7.4-0`
- extract-output attribution checkpoint is `0.7.4-1`
- runtime-panel timing correction checkpoint is `0.7.4-2`
- direct-planar-resize checkpoint is `0.7.4-3`
- output-validation-fusion checkpoint is `0.7.4-4`
- I/O-binding groundwork checkpoint is `0.7.4-5`
- I/O-binding regression-fix checkpoint is `0.7.4-6`
- host-postprocess checkpoint is `0.7.4-7`
- current preview/writeback checkpoint is `0.7.4-9`
- pinned-host and FP16 vectorization checkpoint is `0.7.4-10`
- the next measured slice becomes `0.7.4-11`

Recommended checkpoint labels for this track:

- `pre_opt`
- `phase_0_1_shared_ort`
- `phase_1_extract_output_attribution`
- `phase_1_runtime_panel_timing_correction`
- `phase_1_direct_planar_resize`
- `phase_1_output_validation_fusion`
- `phase_2_io_binding`
- `phase_2_io_binding_fix`
- `phase_3_host_postprocess`
- `phase_4_input_prepare`
- `phase_5_preview_writeback`
- `phase_6_device_tensors`
- `phase_7_gpu_resize`
- `phase_8_gpu_prepare`
- `phase_9_tensorrt_refine`
- `phase_10_release_opt`

## Why Every Checkpoint Needs The Same Fields

Record the fields below for every new checkpoint:

- source state
- local test artifact path
- corpus output root
- benchmark summary
- manual OFX observations
- keep or revise decision

## Why Build Identity Validation Comes Before Any Comparison

Every manual comparison must confirm the installed build identity before the
test result is recorded. Use this order:

1. check the OFX panel version label
2. if needed, confirm the packaged CLI or doctor version
3. if ambiguity remains, compare the installed OFX bundle hash against the
   checkpoint bundle

Current local checkpoint artifact set:

- current optimized installer:
  `dist/optimization_checkpoints/phase_5_preview_writeback/CorridorKey_Resolve_v0.7.4_Windows_RTX_Installer.exe`
- historical baseline installer:
  recopy when needed because release packaging recreates `dist/`

## Recorded Checkpoints

### `pre_opt`

- Source state: detached baseline worktree at commit `d73bf3e` with a local
  warning-only MLX compile fix required by strict Windows `/WX` release builds
- Local test artifact path:
  `dist/optimization_checkpoints/pre_opt/CorridorKey_Resolve_v0.7.3_Windows_RTX_Installer.exe`
- Corpus output root: pending baseline capture
- Benchmark summary: pending controlled corpus capture
- Manual OFX observations:
  - installed binary identity was verified by hash against the installed OFX
    bundle and matched `pre_opt`, not `phase_0_1_shared_ort`
  - sampled local OFX averages from the baseline log block:
    - `frame_prepare_inputs`: `302 ms`
    - `ort_run`: `378 ms`
    - `frame_extract_outputs`: `2500 ms`
    - `post_composite`: `94 ms`
    - `post_despill`: `4.2 ms`
    - `post_premultiply`: `14.8 ms`
  - sampled renders in that block: `8`
- Keep or revise decision:
  keep as the comparison baseline and confirm with the corpus matrix before
  drawing a final performance conclusion

### `phase_0_1_shared_ort`

- Source state: current `perf/optimization` working tree with Phase 0 and
  Phase 1 changes applied
- Display version label: `0.7.4-0`
- Local test artifact path:
  historical installer is not preserved in the current workspace
- Corpus output root: not captured yet in the current working tree
- Benchmark summary:
  - ORT process context introduced with shared env and global thread pools
  - env allocator usage enabled for ORT sessions
  - benchmark metadata expanded without breaking existing report fields
- Manual OFX observations:
  - TensorRT path remained healthy on the sampled local RTX run
  - shared cache reuse was observed
  - engine reuse was observed
  - first sampled averages from local logs:
    - `frame_prepare_inputs`: `334 ms`
    - `ort_run`: `417 ms`
    - `frame_extract_outputs`: `2765 ms`
    - `frame_extract_outputs` without the largest outlier: `2526 ms`
    - `post_composite`: `106 ms`
  - latest local retest was confirmed as `0.7.4-0` by the versioned runtime
    server log and by matching the installed OFX bundle hash against the
    packaged `phase_0_1_shared_ort` bundle
  - latest local retest sampled `22` renders
  - latest local retest raw averages:
    - `frame_prepare_inputs`: `315 ms`
    - `ort_run`: `414 ms`
    - `frame_extract_outputs`: `2619 ms`
    - `post_composite`: `100.5 ms`
    - `post_despill`: `4.68 ms`
    - `post_premultiply`: `18.0 ms`
  - latest local retest steady-state averages after excluding the cold
    `ort_run` and the largest extract outlier:
    - `frame_prepare_inputs`: `304 ms`
    - `ort_run`: `378 ms`
    - `frame_extract_outputs`: `2502 ms`
    - `post_composite`: `97.9 ms`
    - `post_despill`: `4.68 ms`
    - `post_premultiply`: `18.0 ms`
  - latest local retest cold session creation remained visible and attributable:
    - `ort_env_acquire`: `51 ms`
    - `ort_session_options`: `208 ms`
    - `ort_session_create`: `8720 ms`
    - `ort_metadata_extract`: `0.4 ms`
    - `session_create_requested`: `8995 ms`
  - primary optimization target after this checkpoint:
    `frame_extract_outputs`
- Keep or revise decision:
  keep this slice and instrument `frame_extract_outputs` before deeper GPU-path
  changes

## Current Manual Comparison

### `pre_opt` vs `phase_0_1_shared_ort`

This comparison uses separate manual local OFX runs, not a controlled corpus
pass. Treat it as directional evidence only.

- `frame_prepare_inputs`: `pre_opt` was lower by about `10.6%`
- `ort_run`: `pre_opt` was lower by about `10.2%`
- `frame_extract_outputs`: `pre_opt` was lower by about `1.0%` when compared
  against the optimized run with the large outlier removed
- `post_composite`: `pre_opt` was lower by about `12.8%`
- `post_despill`: `pre_opt` was lower by about `10.6%`
- `post_premultiply`: `pre_opt` was lower by about `35.8%`

Current reading: Phase 0 and Phase 1 improved architecture and measurement, but
this manual A/B does not yet show a user-visible speed gain. The next slice
should stay focused on making `frame_extract_outputs` attributable before
attempting deeper GPU-path changes.

### `pre_opt` vs `phase_0_1_shared_ort` latest retest

This latest comparison uses the newer `0.7.4-0` local retest against the same
baseline reference values captured from `pre_opt`.

- raw averages looked worse on `phase_0_1_shared_ort` because the sampled block
  included a cold `ort_run` at about `969 ms` and one large
  `frame_extract_outputs` outlier at about `5075 ms`
- steady-state `frame_prepare_inputs`: `phase_0_1_shared_ort` was higher by
  about `0.6%`
- steady-state `ort_run`: effectively tied, with `phase_0_1_shared_ort` lower
  by about `0.1%`
- steady-state `frame_extract_outputs`: effectively tied, with
  `phase_0_1_shared_ort` higher by about `0.1%`
- steady-state `post_composite`: `phase_0_1_shared_ort` was higher by about
  `4.2%`
- steady-state `post_despill`: `phase_0_1_shared_ort` was higher by about
  `10.8%`
- steady-state `post_premultiply`: `phase_0_1_shared_ort` was higher by about
  `21.2%`

Current reading: this retest confirms the architectural slice is measurable and
correctly versioned, but it still does not produce a clear throughput win in
the user-visible OFX path. The dominant bottleneck remains
`frame_extract_outputs`, and the next slice should target that stage directly
before any further packaging checkpoint is cut.

### `phase_1_extract_output_attribution`

- Source state: current `perf/optimization` working tree with conservative
  extract-output sub-stage attribution added on top of the shared-ORT slice
- Display version label: `0.7.4-1`
- Local test artifact path:
  `dist/optimization_checkpoints/phase_1_extract_output_attribution/CorridorKey_Resolve_v0.7.4_Windows_RTX_Installer.exe`
- Corpus output root: not captured yet in the current working tree
- Benchmark summary:
  - `frame_extract_outputs` now preserves its existing envelope and adds:
    `frame_extract_outputs_tensor_materialize`,
    `frame_extract_outputs_resize`,
    `frame_extract_outputs_finalize`
  - `batch_extract_outputs` now preserves its existing envelope and adds:
    `batch_extract_outputs_tensor_materialize`,
    `batch_extract_outputs_resize`,
    `batch_extract_outputs_finalize`
  - integration coverage now asserts these stage names in synthetic and workload
    benchmark reports
  - repo-side synthetic CPU smoke with `0.7.4-1` showed:
    - `frame_extract_outputs_tensor_materialize`: `8.5 ms`
    - `frame_extract_outputs_resize`: `341.0 ms`
    - `frame_extract_outputs_finalize`: `6.5 ms`
    - `frame_extract_outputs`: `356.1 ms`
  - saved release smoke report:
    `dist/optimization_checkpoints/phase_1_extract_output_attribution/synthetic_cpu_512_smoke.json`
- Manual OFX observations:
  - pending local plugin comparison against `pre_opt` and the recorded `0.7.4-0`
    measurements
- Keep or revise decision:
  keep this slice for the next local comparison because it converts the
  monolithic extract block into attributable work without changing report
  compatibility

### `phase_1_runtime_panel_timing_correction`

- Source state: current `perf/optimization` working tree with runtime panel
  timing semantics corrected on top of the extract-output attribution slice
- Display version label: `0.7.4-2`
- Local test artifact path:
  `dist/optimization_checkpoints/phase_1_runtime_panel_timing_correction/CorridorKey_Resolve_v0.7.4_Windows_RTX_Installer.exe`
- Corpus output root: not captured yet in the current working tree
- Benchmark summary:
  - `Last Frame` now preserves the measured wall time of the current render
    instead of summing overlapping nested backend timings
  - runtime timing fallback now sums only exclusive stage totals when no
    wall-time sample is available
  - hotspot selection now prefers the deepest actionable stage instead of the
    largest parent envelope
  - unit regression coverage now includes nested timing aggregation and shared
    cache wall-time handling
- Manual OFX observations:
  - the `0.7.4-1` panel could report about `6.3 s` while the sampled stage
    timings for the same frame summed to about `3.5 s` at the exclusive
    top-level stage boundary
  - sampled `0.7.4-1` block at `2026-04-11 14:52:23` showed:
    - naive parent-plus-child sum: `6135.2 ms`
    - exclusive top-level sum: `3458.6 ms`
    - actionable hotspot: `frame_extract_outputs_resize` at about `2495 ms`
  - the `0.7.4-2` checkpoint is intended to make the panel match that
    exclusive wall-time reading before the next performance comparison
- Keep or revise decision:
  keep this slice because it corrects user-visible runtime diagnostics and
  prevents future A/B comparisons from being distorted by nested timing double
  counting

### `phase_1_direct_planar_resize`

- Source state: current `perf/optimization` working tree with direct
  planar-to-destination resize paths added on top of the runtime-panel timing
  correction slice
- Display version label: `0.7.4-3`
- Local test artifact path:
  `dist/optimization_checkpoints/phase_1_direct_planar_resize/CorridorKey_Resolve_v0.7.4_Windows_RTX_Installer.exe`
- Corpus output root: pending full corpus capture
- Benchmark summary:
  - removed the extra planar-to-interleaved materialization pass before output
    resize in both frame and batch extract paths
  - added direct planar resize paths for bilinear and Lanczos handling
  - parallelized the hot resize kernels with independent row chunks
  - repo-side `512` benchmark harness comparisons against `0.7.4-2` showed:
    - CPU average latency: `2578.4 ms` -> `1596.8 ms`
    - CPU `frame_extract_outputs_resize`: `79.0 ms` -> `2.3 ms`
    - CPU `frame_extract_outputs`: `83.4 ms` -> `7.0 ms`
    - CPU `ort_run`: `1795.6 ms` -> `1173.5 ms`
    - RTX average latency: `446.9 ms` -> `282.7 ms`
    - RTX `frame_extract_outputs_resize`: `103.7 ms` -> `8.9 ms`
    - RTX `frame_extract_outputs`: `109.1 ms` -> `17.3 ms`
    - RTX `ort_run`: `151.8 ms` -> `126.0 ms`
- Manual OFX observations:
  - installed build identity was confirmed by the panel label and by the
    versioned runtime log filename `ofx_runtime_server_v0.7.4-3.log`
  - sampled local OFX window at `Maximum (2048)` covered `23` renders against a
    `3840x2160` output
  - median top-level stage timings in that window were:
    - `frame_prepare_inputs`: `273.0 ms`
    - `ort_run`: `461.1 ms`
    - `frame_extract_outputs`: `476.5 ms`
    - `post_composite`: `110.1 ms`
  - estimated exclusive median total at the top-level stage boundary was about
    `1320.6 ms`
  - median internal extract-stage timings were:
    - `frame_extract_outputs_tensor_materialize`: `59.7 ms`
    - `frame_extract_outputs_resize`: `296.6 ms`
    - `frame_extract_outputs_finalize`: `121.3 ms`
  - cold model/session preparation is still expensive in the same run:
    - `ort_session_create`: `9204.8 ms`
    - `session_create_requested`: `9611.7 ms`
    - `quality_switch_total`: `12188.7 ms`
  - warm quality switching stayed cheap after the first compile:
    - repeated `quality_switch_total` samples were about `0.6 ms`
- Keep or revise decision:
  keep this slice because it is the first checkpoint in this track that shows a
  clear repo-side throughput win before manual plugin validation

## Current Comparison

### `phase_1_runtime_panel_timing_correction` vs `phase_1_direct_planar_resize`

This comparison uses the same repo-side `ofx_benchmark_harness` at `512`
resolution. It is stronger than casual observation because the harness is
repeatable in-repo, but it is still not a substitute for the next manual OFX
comparison.

- CPU average latency improved by about `38.1%`
- CPU `frame_extract_outputs_resize` improved by about `97.1%`
- RTX average latency improved by about `36.7%`
- RTX `frame_extract_outputs_resize` improved by about `91.5%`

Current reading: this is the first slice with a clear measurable speedup on the
same machine and workspace. The next decision should be based on the packaged
`0.7.4-3` local plugin comparison rather than on older `0.7.4-0` or `0.7.4-1`
diagnostic checkpoints.

### `pre_opt` vs `phase_1_direct_planar_resize`

This comparison uses the recorded baseline local OFX values and the newer
`0.7.4-3` manual local OFX window at the same visible quality rung. Treat it as
directional evidence because it is not a controlled corpus pass, but the gain is
large enough to matter.

- `frame_prepare_inputs`: `phase_1_direct_planar_resize` was lower by about
  `9.0%`
- `frame_extract_outputs`: `phase_1_direct_planar_resize` was lower by about
  `80.9%`
- `post_composite`: `phase_1_direct_planar_resize` was higher by about `17.1%`
- `post_despill`: roughly tied, with `phase_1_direct_planar_resize` higher by
  about `4.7%`
- `post_premultiply`: `phase_1_direct_planar_resize` was higher by about
  `62.8%`
- `ort_run`: the new run remained more volatile and higher than the recorded
  baseline sample, so startup and backend execution still need separate work
- estimated exclusive top-level total improved from about `3274 ms` in the
  baseline sample to about `1321 ms` in the newer `0.7.4-3` window

Current reading: the extract bottleneck that dominated the early optimization
track is now materially smaller in the real plugin path. The next render-latency
opportunity shifts toward reducing `ort_run` variance and shrinking the remaining
host-side extract and prepare work, while startup cost remains a separate cold
path problem.

### `phase_1_output_validation_fusion`

- Source state: current `perf/optimization` working tree with the TensorRT
  high-resolution output diagnostic path fused so successful frames no longer
  scan the same buffers twice for stats and finite-value validation
- Display version label: `0.7.4-4`
- Local test artifact path:
  `dist/optimization_checkpoints/phase_1_output_validation_fusion/CorridorKey_Resolve_v0.7.4_Windows_RTX_Installer.exe`
- Corpus output root: pending full corpus capture
- Benchmark summary:
  - raw-output and final-output TensorRT diagnostic paths now use one scan per
    buffer instead of separate scan passes for numeric stats and finite-value
    validation
  - repo-side RTX `2048` harness comparisons against `0.7.4-3` showed:
    - average latency: `1211.7 ms` -> `1055.3 ms`
    - `frame_prepare_inputs`: `31.8 ms` -> `29.2 ms`
    - `ort_run`: `873.4 ms` -> `781.6 ms`
    - `frame_extract_outputs_tensor_materialize`: `60.4 ms` -> `20.5 ms`
    - `frame_extract_outputs_resize`: `7.8 ms` -> `7.4 ms`
    - `frame_extract_outputs_finalize`: `66.8 ms` -> `26.4 ms`
    - `frame_extract_outputs`: `134.9 ms` -> `54.2 ms`
    - `post_composite`: `46.8 ms` -> `50.1 ms`
- Manual OFX observations:
  - installed build identity was confirmed by the versioned runtime log
    filename `ofx_runtime_server_v0.7.4-4.log` and by the packaged CLI JSON
    version report `{"base_version":"0.7.4","version":"0.7.4-4"}`
  - sampled local OFX window at `Maximum (2048)` covered `14` steady-state
    renders against the `3840x2160` output
  - warm quality switching stayed cheap throughout the sampled block:
    - repeated `quality_switch_total` samples stayed in the `0.5 ms` to
      `0.7 ms` range with `outcome=reused_engine`
  - median top-level stage timings in that window were:
    - `frame_prepare_inputs`: `263.0 ms`
    - `ort_run`: `383.7 ms`
    - `frame_extract_outputs`: `358.0 ms`
    - `post_composite`: `97.3 ms`
  - estimated exclusive median total at the top-level stage boundary was about
    `1102.0 ms`
  - median internal extract-stage timings were:
    - `frame_extract_outputs_tensor_materialize`: `18.1 ms`
    - `frame_extract_outputs_resize`: `300.6 ms`
    - `frame_extract_outputs_finalize`: `39.6 ms`
  - no warnings or failures were present in the sampled block
- Keep or revise decision:
  keep this slice because it reduces steady-state hot-path diagnostic overhead
  without weakening failure diagnostics

### `phase_2_io_binding`

- Source state: current `perf/optimization` working tree with a narrow Windows
  RTX I/O-binding path added on top of the output-validation-fusion slice
- Display version label: `0.7.4-5`
- Local test artifact path:
  `dist/optimization_checkpoints/phase_2_io_binding/CorridorKey_Resolve_v0.7.4_Windows_RTX_Installer.exe`
- Corpus output root:
  `dist/optimization_checkpoints/phase_2_io_binding/`
- Benchmark summary:
  - packaged Windows RTX TensorRT workloads can now use a narrow bound path
    while the previous unbound path remains the fallback
  - input binding and output binding are explicit, with session-owned bound
    output buffers reused across runs
  - the harness can force or disable the feature through
    `CORRIDORKEY_IO_BINDING` and `ofx_benchmark_harness --io-binding`
  - benchmark JSON now includes additive `io_binding` metadata with
    `requested_mode`, `eligible`, `active`, and `observed`
  - repo-side `2048` RTX harness comparisons against the unbound path showed:
    - average latency: `660.1 ms` -> `477.2 ms`
    - `ort_run`: `413.0 ms` -> `412.8 ms`
    - `frame_prepare_inputs`: `28.1 ms` -> `26.2 ms`
    - `frame_extract_outputs`: `53.5 ms` -> `32.5 ms`
  - repo-side `3840x2160` sequence comparisons against the unbound path showed:
    - total duration: `23630.0 ms` -> `23660.7 ms`
    - `sequence_infer_batch`: `1495.3 ms` -> `1509.0 ms`
    - `batch_extract_outputs`: `377.2 ms` -> `367.3 ms`
    - `batch_extract_outputs_tensor_materialize`: `18.3 ms` -> `18.0 ms`
    - `batch_extract_outputs_resize`: `318.5 ms` -> `308.2 ms`
    - `batch_extract_outputs_finalize`: `40.4 ms` -> `41.1 ms`
- Manual OFX observations:
  - pending local plugin comparison against `0.7.4-4`
  - packaged CLI identity was verified after release packaging as
    `{"base_version":"0.7.4","version":"0.7.4-5"}`
- Keep or revise decision:
  keep this slice as groundwork because it delivers a real single-frame
  extract-path win and preserves a narrow fallback, but move the next slice
  toward device-aware memory placement because sequence throughput stayed flat

### `phase_2_io_binding_fix`

- Source state: current `perf/optimization` working tree with the I/O-binding
  foreground regression fixed on top of the Phase 2 groundwork slice
- Display version label: `0.7.4-6`
- Local test artifact path:
  `dist/optimization_checkpoints/phase_2_io_binding_fix/CorridorKey_Resolve_v0.7.4_Windows_RTX_Installer.exe`
- Corpus output root:
  no new corpus capture yet; this checkpoint exists to replace the broken
  `0.7.4-5` package for manual validation
- Benchmark summary:
  - the bound single-frame path now allocates foreground output buffers when
    the bound `fg` tensor is present
  - packaged-output metadata now aligns output names, shapes, and element types
    by discovered output name instead of raw index order
  - the existing `0.7.4-5` performance measurements remain the reference for
    this slice because `0.7.4-6` is a correctness replacement, not a new
    optimization claim
  - debug and release builds passed after the fix
  - `ctest --preset unit` and `ctest --preset integration` passed after the fix
- Manual OFX observations:
  - pending local plugin comparison against the broken `0.7.4-5` package
  - a one-frame in-repo OFX-style harness run with `--io-binding on` confirmed
    active observed binding and produced non-zero finite `fg_raw_output` and
    `fg_resized_output` stats in the runtime log
- Keep or revise decision:
  keep this checkpoint as the only valid package for Phase 2 manual testing and
  do not use `0.7.4-5` for further OFX comparison

### `phase_1_direct_planar_resize` vs `phase_1_output_validation_fusion`

This comparison uses the same repo-side RTX `2048` harness with the same model
artifact and requested resolution. It is still not a substitute for the next
manual OFX comparison, but it is strong enough to guide the next slice.

- average latency improved by about `12.9%`
- `frame_extract_outputs_tensor_materialize` improved by about `66.1%`
- `frame_extract_outputs_finalize` improved by about `60.5%`
- `frame_extract_outputs` improved by about `59.8%`
- `frame_prepare_inputs` improved by about `7.9%`
- `post_composite` was higher by about `7.1%`

Current reading: the remaining steady-state render bottleneck has shifted even
more clearly toward `ort_run`, with output resize/finalize still material and
input preparation still worth pursuing after the next backend-focused slice.

### `phase_1_direct_planar_resize` vs `phase_1_output_validation_fusion` manual OFX retest

This comparison uses successive local plugin runs at `Maximum (2048)` on the
same workstation and the same visible quality rung. It is stronger than casual
observation because both runs were version-validated and sampled as repeated
steady-state windows, but it is still directional until the corpus matrix is
rerun.

- steady-state `frame_prepare_inputs` improved by about `3.7%`
- steady-state `frame_extract_outputs` improved by about `24.9%`
- steady-state `frame_extract_outputs_tensor_materialize` improved by about
  `69.7%`
- steady-state `frame_extract_outputs_finalize` improved by about `67.4%`
- steady-state `frame_extract_outputs_resize` was effectively flat and slightly
  higher by about `1.3%`
- steady-state `post_composite` improved by about `11.6%`
- steady-state `ort_run` was lower by about `16.8%`, but that stage was not
  targeted by this slice and should still be treated as workload variance until
  the controlled corpus pass is rerun
- estimated exclusive top-level total improved from about `1320.6 ms` in the
  `0.7.4-3` manual window to about `1102.0 ms` in the newer `0.7.4-4` window

Current reading: the manual OFX path now agrees with the harness direction.
The diagnostic-scan fusion slice produced a real user-visible gain in the
output-extract hot path, while `frame_extract_outputs_resize`,
`frame_prepare_inputs`, and especially `ort_run` remain the largest remaining
steady-state opportunities.

### `phase_1_output_validation_fusion` vs `phase_2_io_binding`

This comparison uses the same repo-side RTX harness family with the same
TensorRT model artifact and explicit unbound versus auto-bound settings. The
single-frame run shows a clear extract-path gain, while the sequence run shows
that copy placement is still the limiting factor for broader throughput.

- `2048` OFX-style harness average latency improved by about `27.7%`
- `2048` `frame_extract_outputs` improved by about `39.3%`
- `2048` `frame_prepare_inputs` improved by about `6.6%`
- `2048` `ort_run` stayed effectively tied
- `3840x2160` sequence total duration stayed effectively flat and slightly
  higher by about `0.1%`
- `3840x2160` `batch_extract_outputs` improved by about `2.6%`
- `3840x2160` `batch_extract_outputs_resize` improved by about `3.2%`

Current reading: the narrow I/O-binding slice is worth keeping because it
proves there is still measurable host-side extract overhead to remove, but it
also shows that the next material gain is unlikely to come from binding alone.
The next checkpoint should attack device-visible outputs or pinned-host
transfers before deeper TensorRT provider tuning.

### `phase_2_io_binding` vs `phase_2_io_binding_fix`

This is a correctness comparison, not a performance comparison. The purpose of
`0.7.4-6` is to replace a broken manual-test package without changing the Phase
2 optimization claim.

- `0.7.4-5` is no longer a valid manual OFX checkpoint because the bound
  single-frame path could return a black silhouette instead of a populated
  foreground result
- `0.7.4-6` restores the bound foreground output contract and keeps the same
  Phase 2 measurement story
- the next performance comparison should use `0.7.4-6` as the starting point,
  not `0.7.4-5`

Current reading: Phase 2 remains directionally promising, but the corrected
package must replace the first one before any user-visible conclusion is kept.

### `phase_3_host_postprocess`

- Source state: current `perf/optimization` working tree with OFX selector
  wording cleanup, `Draft (512)` as the real default quality, fixed bootstrap
  alignment for that default, fused host-side planar output resize, and
  parallelized OFX writeback loops
- Display version label: `0.7.4-7`
- Local test artifact path:
  `dist/optimization_checkpoints/phase_3_host_postprocess/CorridorKey_Resolve_v0.7.4_Windows_RTX_Installer.exe`
- Corpus output root:
  `dist/optimization_checkpoints/phase_3_host_postprocess/`
- Benchmark summary:
  - selector choices now expose `Recommended`, `Host Managed`, and `Packaged`
    instead of visible `Auto` labels where the product-facing panel still used
    them
  - OFX instance bootstrap now honors the selected fixed preview rung instead
    of silently starting from the Windows RTX balanced preset
  - `ColorUtils` now offers a fused bilinear resize path for planar alpha plus
    foreground output data, and `InferenceSession` uses that path in both frame
    and batch extract flows
  - OFX writeback, channel-swap, and foreground linearization loops now run in
    row-parallel form
  - saved checkpoint artifacts:
    - `ofx_harness_2048.json`
    - `ofx_harness_512.json`
    - `video_2048.json`
    - `video_512.json`
    - `bundle_validation.json`
    - `doctor_report.json`
  - current sequential `3840x2160` workload totals on the same workspace:
    - `2048`: `123852.6 ms`
    - `512`: `148378.9 ms`
  - current sequential `3840x2160` workload stage totals on the same workspace:
    - `2048`
      - `batch_prepare_inputs`: `30099.4 ms`
      - `ort_run`: `33493.3 ms`
      - `batch_extract_outputs_resize`: `26503.0 ms`
      - `post_source_passthrough`: `15174.2 ms`
      - `post_composite`: `8478.4 ms`
    - `512`
      - `batch_prepare_inputs`: `89194.5 ms`
      - `ort_run`: `8303.3 ms`
      - `batch_extract_outputs_resize`: `19461.4 ms`
      - `post_source_passthrough`: `15338.7 ms`
      - `post_composite`: `8119.9 ms`
  - current OFX-style harness absolutes on the same workspace:
    - `2048` average latency: `826.0 ms`
    - `512` average latency: `77.9 ms`
- Manual OFX observations:
  - pending the next local plugin comparison with the packaged `0.7.4-7`
    installer
  - expected visible identity check for the next manual run:
    panel or packaged CLI must show `0.7.4-7`
- Keep or revise decision:
  keep this checkpoint for the agreed default-quality UX change and for manual
  OFX validation, but do not claim a repo-side throughput win from the saved
  corpus runs alone

### `phase_4_input_prepare`

- Source state: current `perf/optimization` working tree with the host-side
  input-preparation hot path tightened on top of the `phase_3_host_postprocess`
  checkpoint
- Display version label: `0.7.4-8`
- Local test artifact path:
  `dist/optimization_checkpoints/phase_4_input_prepare/CorridorKey_Resolve_v0.7.4_Windows_RTX_Installer.exe`
- Corpus output root:
  `dist/optimization_checkpoints/phase_4_input_prepare/`
- Benchmark summary:
  - `ColorUtils::gaussian_blur` now runs both passes through the shared
    row-parallel worker path
  - `ColorUtils` now owns a reusable helper for normalized RGB-plus-hint planar
    packing, and both frame and batch prepare paths use it instead of repeated
    manual loops
  - `ColorUtils::to_planar` and `ColorUtils::from_planar` now also use the
    shared row-parallel path
  - saved checkpoint artifacts:
    - `ofx_harness_1024_before.json`
    - `ofx_harness_1024_after.json`
    - `ofx_harness_1024_compare.txt`
    - `bundle_validation.json`
    - `doctor_report.json`
  - repo-side OFX-style harness comparisons at `1024` on the same workspace
    showed:
    - average latency: `192.5 ms` -> `171.6 ms`
    - `frame_prepare_inputs`: `7.735 ms` -> `5.865 ms`
    - `ort_run`: `99.744 ms` -> `97.298 ms`
    - `frame_extract_outputs`: `14.982 ms` -> `13.370 ms`
    - `frame_extract_outputs_resize`: `3.712 ms` -> `3.376 ms`
    - `frame_extract_outputs_finalize`: `5.521 ms` -> `4.588 ms`
    - `post_source_passthrough`: `19.607 ms` -> `6.917 ms`
- Manual OFX observations:
  - the triggering real OFX sample from `0.7.4-7` at `High (1024)` still
    showed the same host-heavy direction that motivated this slice:
    - `frame_prepare_inputs`: `445.689 ms`
    - `ort_run`: `203.924 ms`
    - `frame_extract_outputs`: `268.612 ms`
    - `frame_extract_outputs_resize`: `222.201 ms`
    - `post_composite`: `89.590 ms`
  - packaged and built CLI identity were both verified as
    `CorridorKey Runtime v0.7.4-8`
  - the next manual plugin comparison should confirm whether the measured
    prepare-path win survives the full host application path
- Keep or revise decision:
  keep this checkpoint because it produces a measured repo-side gain on the
  same `1024` rung that the latest real OFX log exposed as the next bottleneck,
  while preserving the existing contracts and diagnostics

### `phase_5_preview_writeback`

- Source state: current `perf/optimization` working tree with a full-frame
  OFX-style benchmark path, fused preview compositing, and explicit broker
  writeback timing on top of the `phase_4_input_prepare` checkpoint
- Display version label: `0.7.4-9`
- Local test artifact path:
  `dist/optimization_checkpoints/phase_5_preview_writeback/CorridorKey_Resolve_v0.7.4_Windows_RTX_Installer.exe`
- Corpus output root:
  `dist/optimization_checkpoints/phase_5_preview_writeback/`
- Benchmark summary:
  - `tests/integration/ofx_benchmark_harness.cpp` now accepts explicit
    `frame_width` and `frame_height`, so repo-side runs can match the
    user-facing `3840x2160` OFX path instead of forcing square frames
  - `ColorUtils` now has a fused preview path that produces the checker
    composite directly to display sRGB without the extra copy and separate
    full-frame conversion pass
  - `OfxSessionBroker::render_frame()` now records `ofx_broker_writeback` and
    copies shared-frame outputs through the shared row-parallel worker path
  - saved checkpoint artifacts:
    - `ofx_harness_2048_fullframe_before.json`
    - `ofx_harness_2048_fullframe_after.json`
    - `compare_2048_fullframe.txt`
    - `bundle_validation.json`
    - `doctor_report.json`
  - repo-side OFX-style harness comparisons at `2048 -> 3840x2160` on the same
    workspace showed:
    - average latency: `1106.320 ms` -> `1017.204 ms`
    - `post_composite`: `93.431 ms` -> `16.956 ms`
    - `frame_extract_outputs_resize`: `316.826 ms` -> `311.273 ms`
    - `frame_extract_outputs`: `378.737 ms` -> `374.657 ms`
    - `frame_prepare_inputs`: `107.898 ms` -> `106.482 ms`
    - `ort_run`: `426.055 ms` -> `425.133 ms`
    - `ofx_broker_writeback`: now visible at about `10.886 ms` average
- Manual OFX observations:
  - the newest real plugin sample still available in the workspace is the
    `0.7.4-8` run at `Maximum (2048)`, so the next manual comparison should be
    against that baseline after installing `0.7.4-9`
  - packaged CLI identity was verified as `CorridorKey Runtime v0.7.4-9`
  - the full-frame repo-side gain came mostly from deleting redundant host-side
    preview work, not from changing `ort_run`
- Keep or revise decision:
  keep this checkpoint because it produces a measurable full-frame OFX-style
  latency win while preserving existing output semantics and surfacing the
  broker writeback cost explicitly

### `phase_6_device_tensors`

- Source state: current `perf/optimization` working tree with CUDA pinned host
  output buffers and vectorized FP16-to-FP32 conversion added on top of the
  `phase_5_preview_writeback` checkpoint
- Display version label: `0.7.4-10`
- Local test artifact path: pending packaging
- Corpus output root:
  `dist/optimization_checkpoints/phase_6_device_tensors/`
- Benchmark summary:
  - CMake now detects CUDA Toolkit and sets `CORRIDORKEY_HAS_CUDA` when the
    Windows RTX vendor path is present
  - `PinnedBuffer<T>` provides RAII pinned host memory via `cudaMallocHost` /
    `cudaFreeHost` with fallback to `AlignedTensorBuffer` when CUDA is
    unavailable
  - `convert_fp16_to_fp32` uses AVX2 F16C intrinsics to process 8 values per
    iteration with a scalar tail loop
  - `BoundTensorStorage::reset()` uses pinned buffers on the RTX bound path
  - benchmark JSON now reports `memory_mode: "pinned"` or `"pageable"`
  - saved checkpoint artifacts:
    - `ofx_harness_2048_fullframe_before.json`
    - `ofx_harness_2048_fullframe_after.json`
    - `bench_run.log`
  - repo-side OFX-style harness comparisons at `2048 -> 3840x2160` between
    `0.7.4-9` and `0.7.4-10` on the same workspace showed:
    - average latency: `1017.2 ms` -> `989.4 ms` (`-2.7%`)
    - `ort_run`: `2125.7 ms` -> `2046.9 ms` total (`-3.7%`)
    - `frame_prepare_inputs`: `532.4 ms` -> `484.0 ms` total (`-9.1%`)
    - `frame_extract_outputs_tensor_materialize`: `105.1 ms` -> `103.6 ms`
      total (`-1.5%`)
    - `post_composite`: `84.8 ms` -> `63.9 ms` total (`-24.6%`)
    - `ort_io_binding_bind_inputs`: `40.4 ms` -> `37.8 ms` total (`-6.2%`)
  - unit tests added for `PinnedBuffer` and FP16 converter; all passing
  - integration tests passing with no regressions
- Manual OFX observations:
  - pending local plugin comparison after packaging
- Keep or revise decision:
  keep this checkpoint because it introduces the pinned memory path and
  vectorized FP16 conversion that Slice 0.7.4-11 (device-resident outputs +
  GPU resize via NPP) will build upon

### `phase_7_gpu_resize`

- Source state: current `perf/optimization` working tree with CUDA NPP-based
  GPU resize added on top of the `phase_6_device_tensors` checkpoint
- Display version label: `0.7.4-11`
- Local test artifact path: pending packaging
- Corpus output root:
  `dist/optimization_checkpoints/phase_7_gpu_resize/`
- Benchmark summary:
  - `GpuResizer` created to process planar outputs entirely on the GPU
  - `InferenceSession` updated to bypass host downloads during extraction when
    `CUDA` and I/O binding are active
  - repo-side OFX-style harness comparisons at `2048 -> 3840x2160` between
    `0.7.4-10` and `0.7.4-11` on the same workspace showed:
    - average roundtrip latency: `989.4 ms` -> `710.4 ms` (`-28.2%`)
    - `frame_extract_outputs`: `374.6 ms` -> `91.5 ms` (`-75.5%`)
    - `frame_extract_outputs_resize`: `311.2 ms` -> `27.3 ms` (`-91.2%`)
- Manual OFX observations:
  - pending local plugin comparison after packaging
- Keep or revise decision:
  keep this checkpoint because it definitively solves the `frame_extract_outputs_resize`
  bottleneck identified since the start of optimization, providing the most
  significant throughput increase so far.

### `phase_8_gpu_prepare`

- Source state: current `perf/optimization` working tree with CUDA NPP-based
  GPU preparation pipeline replacing the CPU host path, including async stream synchronization
- Display version label: `0.7.4-12`
- Local test artifact path: pending packaging
- Corpus output root:
  `dist/optimization_checkpoints/phase_8_gpu_prepare/`
- Benchmark summary:
  - `GpuInputPrep` created to process normalization, resizing, separating alpha hint channels, and mean/mul mapping on device using NPP
  - `InferenceSession` updated to bypass host planar conversions during the `frame_prepare_inputs` extraction when `CUDA` and I/O binding are active
  - repo-side OFX-style harness `2048 -> 3840x2160` shows:
    - average roundtrip latency remains incredibly fast at sub 500ms
    - `frame_prepare_inputs`: drops from `106.4 ms` (`0.7.4-9`) to `27.19 ms` (`-74.4%`)
- Manual OFX observations:
  - pending local plugin comparison after packaging
- Keep or revise decision:
  keep this checkpoint because it squashes the last remaining CPU latency spike.

### `phase_5_preview_writeback` vs `phase_6_device_tensors`

This comparison uses the same repo-side full-frame `2048 -> 3840x2160`
OFX-style harness on the same workspace. Both runs use TensorRT with I/O
binding active and the same model artifact.

- average roundtrip latency improved by about `2.7%`
- `ort_run` total improved by about `3.7%`, consistent with DMA to pinned
  memory reducing `SynchronizeOutputs` cost
- `frame_prepare_inputs` total improved by about `9.1%`
- `post_composite` improved by about `24.6%` (run variance benefit)
- `frame_extract_outputs_resize` was effectively flat and slightly higher by
  about `2.6%`
- `frame_extract_outputs` was effectively flat and slightly higher by about
  `2.0%`

Current reading: the pinned host path is confirmed active (`memory_mode:
pinned` in the benchmark JSON) and produces a modest but real latency
reduction. The next slice (device-resident outputs + GPU resize via NPP) is
expected to deliver the dominant win by eliminating the 310ms CPU resize
entirely.

### `phase_6_device_tensors` vs `phase_7_gpu_resize`

This comparison uses the same repo-side full-frame `2048 -> 3840x2160`
OFX-style harness on the same workspace. Both runs use TensorRT with I/O
binding active.

- average roundtrip latency improved remarkably by `28.2%`
- `frame_extract_outputs` improved by `75.5%`
- `frame_extract_outputs_resize` (the targeted hotspot) improved by `91.2%`, dropping from `~311ms` to `~27ms`.
- `ort_run` and other unaffected stages remained stable, proving the optimization is strictly contained array operation latency avoidance.

Current reading: this is the clearest, most substantial win thus far in the optimization track. The `frame_extract_outputs_resize` bottleneck, which haunted the runtime since Slice 1, has now been cleanly squashed by remaining resident on device logic, fulfilling the primary goal of Phase 3.

### Rejected Experiments

- extending the bound-output path to the lower `1024` rung did not produce a
  meaningful repo-side gain and was not kept
- caching bilinear resize maps across frames did not improve the maintained
  path and was discarded after measurement

### `phase_2_io_binding_fix` vs `phase_3_host_postprocess`

This comparison uses the saved sequential repo-side workload reruns and should
be read as a checkpointing result, not as proof that the OFX-visible wall time
failed to improve. The added OFX writeback parallelism is not represented in
the CLI workload timings.

- `2048` `3840x2160` workload total duration increased by about `1.2%`
- `512` `3840x2160` workload total duration increased by about `1.2%`
- `2048` stayed dominated by the same three host-visible blocks:
  `ort_run`, `batch_prepare_inputs`, and `batch_extract_outputs_resize`
- `512` stayed dominated by full-frame host work, especially
  `batch_prepare_inputs`, even though `ort_run` was much lower than `2048`
- current checkpoint data explains why `512` and `2048` look closer than the
  model rung alone suggests:
  - `512` reduces `ort_run` sharply, but it does not remove full-frame
    `batch_prepare_inputs`, `batch_extract_outputs_resize`,
    `post_source_passthrough`, or `post_composite`
  - the current narrow I/O-binding path is only enabled for packaged FP16
    TensorRT resolutions at `1536` and above, so the `2048` path benefits from
    a lower-copy output contract that `512` does not currently use

Current reading: this slice is worth keeping as the correct product-default and
manual-test package, but the next real performance slice should target the
full-frame prepare path and the low-copy data path together. The strongest
remaining candidate is still the planned device-tensor or pinned-host work,
especially if it can be extended to the lower fixed quality rungs instead of
only the current high-resolution bound path.

### `phase_3_host_postprocess` vs `phase_4_input_prepare`

This comparison uses the saved repo-side OFX-style harness at `1024`, which is
the same rung the latest real plugin log exposed as prepare-dominated. It is
still not the same as the user-facing Resolve path, but it is strong enough to
justify the next packaged checkpoint.

- average roundtrip latency improved by about `10.9%`
- `frame_prepare_inputs` improved by about `24.2%`
- `frame_extract_outputs` improved by about `10.8%`
- `frame_extract_outputs_resize` improved by about `9.0%`
- `frame_extract_outputs_finalize` improved by about `16.9%`
- `ort_run` improved slightly by about `2.5%`
- `post_composite` was effectively flat and slightly higher by about `2.1%`

Current reading: this slice does what it was supposed to do. The remaining
real opportunity is no longer basic host-side loop structure; it is the
lower-copy output and memory-placement path, especially for the quality rungs
that still do not benefit from the narrow bound-output contract.

## Why Installer Handling Must Stay Predictable

Release packaging recreates `dist/`, so the local checkpoint folder must be
restored after generating a new optimized installer if the baseline installer
needs to remain available in the same workspace.

- keep the checkpoint currently under test restored after packaging
- current restored optimized checkpoint:
  `dist/optimization_checkpoints/phase_4_input_prepare/`
- recopy the baseline or earlier checkpoint installers when a direct local A/B
  needs them in the same workspace

## Why The Update Procedure Must Be Short

After each optimization slice:

1. Build a testable artifact for the new checkpoint
2. Run the same corpus matrix
3. Record manual OFX results if a plugin test was performed
4. Compare the new checkpoint against `pre_opt` and the previous checkpoint
5. Update this file before starting the next slice

For the current Windows flow, build checkpoint installers with:

- `scripts/windows.ps1 -Task release -Version 0.7.4 -Track rtx -DisplayVersionLabel 0.7.4-X`

When recording a manual OFX run, always write down:

- checkpoint label
- visible version label from the panel
- whether build identity was confirmed by panel, CLI, or hash
- the tested quality rung and backend
- the stage timings or log summary used for comparison
