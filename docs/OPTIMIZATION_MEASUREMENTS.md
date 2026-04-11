# Optimization Measurements

## Why This Document Exists

This document defines the comparison protocol for optimization work and keeps
the recorded checkpoints in one place. The goal is to compare each slice
against a stable baseline instead of relying on memory or one-off tests.

Use this file together with [OPTIMIZATION_CHECKLIST.md](./OPTIMIZATION_CHECKLIST.md).

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
- current output-validation-fusion checkpoint is `0.7.4-4`
- the next measured slice becomes `0.7.4-5`

Recommended checkpoint labels for this track:

- `pre_opt`
- `phase_0_1_shared_ort`
- `phase_1_extract_output_attribution`
- `phase_1_runtime_panel_timing_correction`
- `phase_1_direct_planar_resize`
- `phase_1_output_validation_fusion`
- `phase_2_iobinding`
- `phase_3_device_tensors`
- `phase_4_gpu_prepare_inputs`
- `phase_5_gpu_postprocess`
- `phase_6_tensorrt_refine`
- `phase_7_release_opt`

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

- baseline installer:
  `dist/optimization_checkpoints/pre_opt/CorridorKey_Resolve_v0.7.3_Windows_RTX_Installer.exe`
- current optimized installer:
  `dist/optimization_checkpoints/phase_1_output_validation_fusion/CorridorKey_Resolve_v0.7.4_Windows_RTX_Installer.exe`

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
  - pending local plugin comparison against `pre_opt` and `0.7.4-3`
- Keep or revise decision:
  keep this slice because it reduces steady-state hot-path diagnostic overhead
  without weakening failure diagnostics

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

## Why Installer Handling Must Stay Predictable

Release packaging recreates `dist/`, so the local checkpoint folder must be
restored after generating a new optimized installer if the baseline installer
needs to remain available in the same workspace.

- keep `dist/optimization_checkpoints/pre_opt/` as the preserved baseline
- keep `dist/optimization_checkpoints/phase_1_extract_output_attribution/` as
  the preserved attribution checkpoint
- keep `dist/optimization_checkpoints/phase_1_runtime_panel_timing_correction/`
  as the preserved runtime-panel checkpoint
- keep `dist/optimization_checkpoints/phase_1_direct_planar_resize/` as the
  preserved direct-planar-resize checkpoint
- keep `dist/optimization_checkpoints/phase_1_output_validation_fusion/` as the
  current optimized checkpoint
- replace only the checkpoint currently under test when a new slice is built

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
