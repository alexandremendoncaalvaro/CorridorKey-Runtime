# Optimization Measurements

## Why This Document Exists

This document defines the comparison protocol for optimization work and keeps
the recorded checkpoints in one place. The goal is to compare each slice
against a stable baseline instead of relying on memory or one-off tests.
`phase_8_gpu_prepare` is the standing baseline for the Windows RTX hot
render path — every change to that path must be measured against it via
`scripts/run_corpus.sh` and `scripts/compare_benchmarks.py`.

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
| `phase_9_blue_dedicated_screen_color` | `0.8.3-win.1` (proposed) | dedicated CorridorKeyBlue catalog + screen-color OFX selection / render branching + despill `screen_channel` generalization | green-path bench gate within +1.5% (`avg_latency_ms`) and +4.3% (`ort_run`); blue dedicated baseline pending FP32-I/O wrapper re-export | gate passes; blue 512 measured; 1024 / 1536 / 2048 to be re-recorded after the in-flight re-export |
| `phase_10_blue_dynamic_hybrid` | `0.8.3-win.1` | dynamic TorchScript candidates for green and blue | dynamic TorchScript loads and produces finite output at `512`, `1024`, and `2048`; dynamic green is `~40%` to `~54%` slower than the optimized green ONNX path in the recorded runner comparison | Windows RTX artifact identity is dynamic green plus dynamic blue; keep `phase_8_gpu_prepare` as the hot-path regression gate until a true dynamic Torch-TensorRT baseline is recorded |
| `torchtrt_dynamic_windows_pinned_input` | `0.8.4-win.1-40-g9772a28-dirty-wc82aa0fe39c2` | persistent pinned host staging for TorchTRT GPU input upload | green heavy source passthrough `2048` RPC average improved from `620.0 ms` to `547.8 ms`; `gpu_prepare_upload_enqueue` dropped to about `0.08 ms` in the readiness matrix | keep; this ports the pinned host transfer rule to the TorchTRT input path without changing visual math |
| `torchtrt_dynamic_windows_async_input` | `0.8.5-win.1-40-g9772a28-dirty-wc24b619e0786` | CUDA event handoff from GPU preparation to TorchTRT | green heavy source passthrough `2048` RPC average improved from the Resolve-observed `~1431 ms` frame average to `503.0 ms` in the readiness matrix; `frame_prepare_inputs` is now about `20.3 ms` and no longer exposes `gpu_prepare_sync` in the device-prepared path | keep; the remaining green bottleneck is model inference plus selected post-processing, not input synchronization |
| `torchtrt_dynamic_windows_gpu_postprocess_writeback` | derived local `0.8.5-win.1-40-g9772a28-dirty` label | direct OFX output views plus TorchTRT GPU despill/source-passthrough post-processing | green alpha-only `2048` RPC average improved from `435.5 ms` to `385.9 ms`; OFX broker writeback is about `0.000 ms` | keep direct output views and GPU despill/source passthrough; constant-only despeckle measurements are not valid for readiness claims because they can hit post-processing early exits |
| `torchtrt_dynamic_windows_cuda_graph_replay_sync` | `0.8.5-win.1-40-g9772a28-dirty-w996ada37955c` | TorchTRT CUDA Graph capture on a side stream with explicit replay/fallback telemetry | green model `2048` runner `p50 285.6 ms`; green processed plate `2048` OFX RPC `507.8 ms`; green alpha-only `2048` OFX RPC `417.3 ms`; every green and Blue-Green case reported replay and no fallback | keep; this brings the TorchTRT path in line with the documented CUDA Graph constraints already enforced by the ONNX path |
| `torchtrt_dynamic_windows_resolve_host_contract` | derived local `0.8.5-win.1-40-g9772a28-dirty` label | restore OFX `RenderInstanceSafe`, defer render-sequence panel writes, and add CUDA event elapsed stages for TorchTRT replay/direct forward | readiness matrix stays green; green model `2048` runner `p50 293.0 ms`; green alpha-only `2048` RPC `390.5 ms`; green source-passthrough plate `2048` RPC `507.0 ms`; green replay CPU/GPU split is about `296.1 ms` vs `288.9 ms` in the harness | keep; this removes host-contract noise and gives the next Resolve manual test enough telemetry to distinguish GPU work from queue wait |

Latest real OFX sample currently recorded in the workspace:

| Sample | Version | Quality | `frame_prepare_inputs` | `ort_run` | `frame_extract_outputs` | `frame_extract_outputs_resize` | `post_composite` |
| --- | --- | --- | --- | --- | --- | --- | --- |
| most recent local plugin log | `0.7.4-8` | `Maximum (2048)` | `83.3 ms` | `372.2 ms` | `344.0 ms` | `287.7 ms` | `97.5 ms` |

Current headline:

- the `prepare_inputs` slice worked
- the `preview_writeback` slice also worked on the full-frame OFX-style harness
- Windows RTX dynamic artifacts need their own green and blue baseline before
  optimization claims replace the fixed ONNX measurements
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

Windows RTX dynamic TorchTRT checkpoints also require:

- `scripts/run_windows_torchtrt_matrix.ps1`

The TorchTRT matrix records model-only runner metrics, OFX RPC sidecar metrics,
stage-timing bottleneck classification, and parameter coverage for runtime
paths. When a prior matrix report exists, run the next checkpoint with
`-BaselineReport <path>` so matching RPC cases fail on regressions above the
same percent threshold used for the hot-path gate.

TorchTRT optimization claims require non-constant input coverage. The
`readiness` profile includes `plate` cases for green processed output, green
source passthrough, green despeckle, blue processed output, blue source
passthrough, and Blue-Green Channel Swap, plus a `random` green despeckle case.
Reports that only exercise `constant` input are compatibility checks, not
release-readiness or optimization evidence.

## Why Checkpoint Names Must Be Stable

Each recorded state gets a stable checkpoint label. Use the same label in:

- installer filenames copied for local testing
- corpus output directories
- benchmark comparison notes
- this measurement ledger

Checkpoint installers are meant for sequential local testing. They share the
same installed product identity and overwrite the previous local plugin state.

Display version policy for this track:

- new Windows local checkpoints use the derived `git describe` label from
  `scripts/windows.ps1`
- historical baseline remains `0.7.3`
- historical optimization checkpoints used `0.7.4-X`
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

### `phase_9_v0.7.5-1`

- Source state: current main after revert `3f70623` (undo of PRs #33/#34/#35) plus
  the 13 pipeline-autosufficiency fixes landed in this session (TensorRT-RTX SDK
  auto-download, vcpkg asset-cache mirror for eigen3, OpenFX SDK auto-stage, MSVC
  env init, etc.). The OFX hot path is post-revert — the intent stated at the
  start of the cycle was paridade with `phase_8_gpu_prepare`.
- Display version label: `0.7.5-1`
- Local test artifact path:
  `dist/CorridorKey_Resolve_v0.7.5_Windows_RTX_Installer.exe`
- Corpus output root:
  `dist/optimization_checkpoints/phase_9_v0.7.5-1/`
- Benchmark summary:
  - repo-side OFX-style harness `2048 -> 3840x2160` on the same workspace,
    TensorRT RTX EP, `io_binding: on`, `memory_mode: pinned`, 20 iterations:
    - `avg_latency_ms` (roundtrip): `709.77 ms`
    - `ort_run`: `427.29 ms`
    - `frame_prepare_inputs`: `48.71 ms`
    - `frame_extract_outputs`: `122.09 ms`
    - `frame_extract_outputs_resize`: `49.40 ms`
    - `frame_extract_outputs_tensor_materialize`: `22.40 ms`
    - `frame_extract_outputs_finalize`: `50.28 ms`
    - `post_composite`: `15.30 ms`
    - `ofx_broker_writeback`: `12.36 ms`
    - `ort_io_binding_bind_inputs`: `7.38 ms`
    - cold session: `ort_session_create` `8570.08 ms`,
      `ofx_prepare_session` `8752.95 ms`
- Manual OFX observations:
  - pending local plugin comparison with the packaged `0.7.5-1` installer
- Comparison against `phase_8_gpu_prepare`:
  - `avg_latency_ms` regressed from `~<500 ms` to `709.77 ms` (+42%); the
    current numbers sit at the `phase_7_gpu_resize` level (~710 ms), not the
    `phase_8_gpu_prepare` level
  - `frame_prepare_inputs` regressed from `27.19 ms` to `48.71 ms` (+79%)
  - `frame_extract_outputs_resize` regressed from `27.3 ms` to `49.40 ms` (+81%)
  - The phase_8 GPU input-prep code (`src/core/gpu_prep.cpp`) is present in the
    tree and guarded by `m_io_binding_enabled && m_gpu_prep.available()`; this
    run had I/O binding active but the timing shows the path is either falling
    back to CPU silently or running slower than when phase_8 was measured. No
    dedicated `gpu_prep_*` stage timing exists in the current harness, so the
    failing branch cannot be confirmed from the JSON alone.
- Keep or revise decision:
  record the regression; do not ship `v0.7.5-1` to public pre-release on
  performance grounds. The installer stays available for local manual Resolve
  validation (the OFX render path is functional; this is a throughput
  regression, not a correctness one). Investigate the GPU input-prep fallback
  before promoting a public pre-release.

### `phase_9_v0.7.5-21_cuda_graph`

- Source state: current main with `CORRIDORKEY_TRT_CUDA_GRAPH=1` made the
  default in the OFX runtime server process. When active, the server also
  forces `CORRIDORKEY_IO_BINDING=on` because CUDA graph capture requires
  fixed-address pre-allocated output buffers (see ORT CUDA EP docs,
  "CUDA Graphs (Preview)" section). Opt-outs remain available via
  explicit env var values (`CORRIDORKEY_TRT_CUDA_GRAPH=0` or
  `CORRIDORKEY_IO_BINDING=off`).
- Display version label: `0.7.5-21`
- Local test artifact path:
  `dist/CorridorKey_Resolve_v0.7.5_Windows_RTX_Installer.exe`
- Corpus output root:
  `dist/optimization_checkpoints/phase_9_v0.7.5-21/`
- Benchmark summary:
  - Exposes `enable_cuda_graph` on the TensorRT RTX EP provider options,
    gated on `CORRIDORKEY_TRT_CUDA_GRAPH` env var. The matching
    `kCudaGraphEnable` alias was added to the local
    `tensorrt_rtx_option_names` namespace in `inference_session.cpp`.
  - Harness (60 frames Jordan4k 4K @ 2048, exclusive GPU):
    - `avg_latency_ms`: `669 ms` (vs `684 ms` on v0.7.5-11 graph off)
    - steady-state `ort_run`: `~364 ms` (vs `~400 ms` graph off)
  - DaVinci Resolve session (91 frames, 4K @ 2048, ORT per-op profile):
    - warm `model_run` p50: `344 ms` (vs `5200 ms` on v0.7.5-11 graph
      off / io on; `2632 ms` on v0.7.5-20 graph off / io off)
    - warm `model_run` p99: `524 ms` (vs `26146 ms` / `7265 ms`)
    - warm `model_run` mean: `358 ms` (vs `4626 ms` / `2786 ms`)
    - 10-frame buckets show steady-state plateau at 344-352 ms across
      frames 20-89 -- no in-session degradation. Frame 0 carries the
      CUDA graph capture warm-up (~460 ms), subsequent frames replay
      the captured DAG.
- Manual OFX observations:
  - Tested live in Resolve 20 on RTX 3080 with the mirror-aware
    pipeline (v0.7.5-21 installer). Kernel execution time stays flat
    across the session; the 26 s outlier that motivated the original
    v0.7.5 revert does not reappear.
  - No quality regression observed: same model, same inputs, same
    outputs -- CUDA graph changes only the CUDA submission mechanism,
    not the computation.
- Interpretation:
  - The CUDA driver submits a captured graph as one atomic DAG, so
    Resolve's own decoder / color / compositor CUDA work cannot
    interleave between TensorRT RTX kernels the way it did with
    per-kernel launches. This matches the `torch.compiler.
    cudagraph_mark_step_begin()` pattern used by the reference
    CorridorKey Python engine.
  - Empirical evidence only: the ORT documentation credits CUDA
    graphs with reduced CPU launch overhead and does not claim a
    shared-GPU contention resistance benefit. Our measurements in
    a real Resolve session show the benefit regardless.
- Keep or revise decision:
  keep as the OFX runtime server default. Supersedes the v0.7.5-20
  `CORRIDORKEY_IO_BINDING=off` mitigation, which was a partial fix
  for the same symptom. The CLI and `ofx_benchmark_harness` default
  behavior is unchanged because they do not run the runtime server
  entrypoint.

### `phase_9_blue_dedicated_screen_color`

- Source state: branch `feat/blue-screen-dedicated-model-rtx` rebased on
  `main` at `6268dc8`. Adds the dedicated CorridorKeyBlue catalog entries,
  screen-color-aware OFX selection / render branching, the despill
  generalization with `screen_channel`, and the export-pipeline tooling
  needed to evaluate per-resolution CorridorKeyBlue artifacts.
- Display version label: pending (`0.8.3-win.1` proposed once packaged)
- Local test artifact path: pending packaging
- Corpus output root: `build/runtime_corpus_after/` (this branch),
  `build/runtime_corpus_before/` (main at `6268dc8`)
- Bench gate (the metric CLAUDE.md requires for any render hot-path edit):
  - matrix: `synthetic_primary` at `1024` on RTX 3080, TensorRT RTX EP,
    `corridorkey_fp16_1024.onnx` (the green production model -- the gate
    measures whether the new screen-color branching regresses the green
    path).
  - `cold_latency_ms`: 363.39 -> 380.61 (+4.7%, within the 10% threshold)
  - `avg_latency_ms`: 101.29 -> 102.77 (+1.5%, within the 10% threshold)
  - `ort_run`: 748.99 -> 781.26 (+4.3%, within the 10% threshold)
  - `frame_prepare_inputs`: 51.50 -> 45.63 (-11.4%, run-to-run variance)
  - All other stages within +/-5% of the noise floor on this single host.
- Blue dedicated baselines (separate from the gate, recorded for future
  comparisons; same RTX 3080, TensorRT RTX EP, `synthetic` benchmark):
  - `corridorkey_blue_fp16_512.onnx`: `avg_latency_ms` `27.49 ms`,
    `cold_latency_ms` `342.6 ms`, `ort_run` `427.20 ms` cumulative.
    Production-fast, ships.
  - `corridorkey_blue_fp16_1024.onnx`: TensorRT and CUDA EP both produce
    all-NaN inference output. CPUExecutionProvider on the same ONNX
    returns finite outputs across all 1860 intermediate tensors when
    captured via shape-inferred value_info, so the model is
    mathematically valid -- the corruption is in the GPU EP execution.
    Same effect reproduces with and without onnxsim, with and without
    `force_fp16_initializers`, and across `op_block_list` strategies
    that cover the LayerNorm decomposition. Identical graph topology
    to the green 1024 ONNX (1812 nodes, 27 Casts, same op counts, same
    initializer dtypes); the only difference is initializer values, so
    the issue is weight-driven FP16 instability that surfaces only on
    GPU EPs. Removed from the local model pack pending a TensorRT-side
    fix; runtime falls back to the canonicalization workaround per the
    documented "Windows Model Availability Policy" in
    `RELEASE_GUIDELINES.md`.
  - `corridorkey_blue_fp16_1536.onnx` and `corridorkey_blue_fp16_2048.onnx`:
    the export path forces FP16 at trace time for >=1536px, producing
    ONNXes with FP16 I/O. TensorRT serves those correctly (inference
    finite) but the runtime's FP32 host pipeline cannot fuse with the
    FP16 entry point, measuring `avg_latency_ms` ~28 s and ~71 s
    respectively against `~245 ms` and `~484 ms` for the production
    green ladder at the same resolutions. Same removal rationale:
    runtime falls back to canonicalization until an FP32-I/O production
    layout is reproducible for blue at >=1024 without re-triggering the
    TensorRT FP16 NaN.
- Diagnostic finding logged for the audit trail:
  - The original symptom: `optimize_model.py` invokes
    `onnxruntime.transformers.float16.convert_float_to_float16` with
    `force_fp16_initializers=True` and `keep_io_types=True`. This works
    for the green checkpoint at every resolution and for the blue
    checkpoint at 512px, but corrupts the blue 1024 dynamic export --
    the resulting FP16 ONNX returns all-NaN inference output on
    TensorRT and CUDA execution providers.
  - Cast variants tried (all produced NaN on TensorRT): default
    settings, `force_fp16_initializers=False`, `op_block_list=['Softmax']`,
    `op_block_list=['Sqrt']`, `['ReduceMean']`, `['Div']`, `['Sqrt','ReduceMean']`.
    Variants that produced finite output but at ~80x latency hit (171
    Cast nodes vs the green 27 because op_block_list inserts Cast
    around every blocked op): `op_block_list=['Pow']`, `['Pow','Sqrt']`,
    `['Pow','Sqrt','ReduceMean','Div']`, `['Pow','Sqrt','ReduceMean','Div','Softmax']`.
    Skipping onnxsim entirely (running cast on the raw export) also NaN.
    Switching to `onnxconverter_common.float16` produced a model whose
    Cast outputs failed type validation at ORT load time.
  - The CPU vs GPU divergence on identical FP16 weights is the smoking
    gun. ORT bug, TensorRT FP16 path quirk on this specific weight
    distribution, or both. Tracking as a follow-up.
- Keep or revise decision:
  keep the green-path bench gate result (the runtime change is correct).
  The blue dedicated artifact at 512px ships; 1024 / 1536 / 2048 are
  recorded as known-blocked in the model pack until the GPU EP NaN is
  isolated, and the OFX render falls back to the canonicalization
  workaround on the green model whenever a missing blue rung is
  requested. Re-revisit this entry once the GPU NaN root cause lands.

### `phase_10_blue_dynamic_hybrid`

This checkpoint compares the validated green ONNX TensorRT RTX EP path against
dynamic TorchScript candidates exported from the green and blue checkpoints.
The product contract uses dynamic RTX artifact identity for both Windows RTX
screen-color variants. The recorded comparison remains the performance
guardrail for the first true dynamic Torch-TensorRT optimization baseline.

- Dynamic TorchScript validation:
  - green dynamic artifact: finite output at `512`, `1024`, and `2048`
  - blue dynamic artifact: finite output at `512`, `1024`, and `2048`
  - C++ runner loads the same dynamic `.ts` file at multiple runtime
    resolutions
- Green performance comparison:
  - `512`: optimized ONNX green `30.8 ms`; dynamic green `43.0 ms`
  - `1024`: optimized ONNX green `102.8 ms`; dynamic green `158.7 ms`
  - `2048`: optimized ONNX green `503.0 ms`; dynamic green `764.4 ms`
- Blue dynamic baseline:
  - `512`: `44.5 ms`
  - `1024`: `156.9 ms`
  - `2048`: `753.4 ms`
- Product-path smoke validation:
  - green dynamic artifact exported from `CorridorKey.pth` validates against
    eager PyTorch at `512`, `1024`, and `2048`
  - blue dynamic artifact SHA256 matches the Hugging Face manifest
  - C++ TorchTRT runner loads green and blue dynamic artifacts and produces
    finite output at `512`
  - CLI synthetic benchmark at `512` reports TorchTRT backend with green
    dynamic artifact at `61.7 ms` average latency
  - CLI synthetic benchmark at `512` reports TorchTRT backend with blue
    dynamic artifact at `49.8 ms` average latency
- Decision:
  - use `corridorkey_dynamic_green_fp16.ts` as the single Windows RTX green
    dynamic artifact
  - use `corridorkey_dynamic_blue_fp16.ts` as the single Windows RTX blue
    dynamic artifact
  - exclude ONNX artifacts from the Windows RTX shipped path and fallback
    policy
  - measure green and blue at `1024` and `2048` through CLI and OFX before
    replacing the `phase_8_gpu_prepare` regression baseline

#### Dynamic Torch-TensorRT Viability Probe

The measured dynamic TorchScript artifacts are not serialized TensorRT
engines. Binary inspection finds TorchScript and CUDA metadata in the dynamic
green and blue artifacts, but not TensorRT engine markers. Fixed diagnostic
Torch-TensorRT artifacts under `models/` do contain TensorRT markers.

The true dynamic Torch-TensorRT probe used the same patched Hiera dynamic graph
and explicit `torch.export.Dim` constraints matching the runtime's multiple of
`32` input padding. Export succeeds for `512` through `1024`, which validates
the shape contract. Torch-TensorRT dry-run can report one TensorRT engine for
all graph operators, but full TensorRT conversion fails on dynamic
`upsample_bicubic2d` in positional embedding. A partitioned probe that leaves
dynamic bicubic and bilinear resize operations in PyTorch compiles, but fails
at runtime on dynamic reshape and resize shape propagation. Do not promote a
dynamic `.ts` as Torch-TensorRT until it contains TensorRT engine markers,
loads in C++, runs multiple resolutions, and has a recorded green and blue
baseline.

A reduced probe proves that the TensorRT blocker is not Hiera execution as a
whole. Removing positional embedding and rewriting decoder resizes from
dynamic `size` tensors to constant architectural scale factors allows
Torch-TensorRT to compile and run one dynamic engine at `512` and `1024`.
Keeping dynamic decoder `size` tensors fails in the TensorRT resize converter;
keeping decoder `.view(..., -1, H, W)` fails because the converter treats the
post-projection channel count as dynamic. The valid dynamic TensorRT candidate
therefore uses explicit projection channel counts and fixed decoder scale
factors.

The valid positional-embedding materialization keeps one `.ts` file per model
and uses TorchScript extra files to embed the source positional embedding.
The C++ runtime loads that extra payload, builds a cached positional grid for
the requested runtime resolution, and forwards the model tensor plus the grid
to the TensorRT module. A green `512` through `1024` candidate loaded with
`torch.jit.load` returned alpha and foreground tensors at both resolutions.
The external-pos eager wrapper matches the original dynamic eager path exactly;
the saved TensorRT candidate stays within FP16-level differences, with observed
maximum absolute differences below `0.004`.

The saved green candidate with embedded positional data is `209.9 MB`. The C++
TorchTRT runner loads it, reports the embedded external positional metadata,
and produces finite outputs:

- `512`: `14.7 ms` average forward over five timed iterations after two warmup
  iterations
- `1024`: `50.5 ms` average forward over five timed iterations after two
  warmup iterations
- `2048`: `277.8 ms` average forward over three timed iterations after one
  warmup iteration

The blue FP16 candidate with the same `512` through `2048` dynamic profile
compiles and produces finite C++ runner output at `512` and `1024`, but returns
NaN output at `1536` and `2048`. This matches the earlier blue FP16 instability
recorded for high-resolution TensorRT artifacts and blocks publication of the
blue FP16 TensorRT candidate as the full Windows RTX blue product artifact.

The blue FP32 candidate validates through a `1536` maximum profile with maximum
absolute differences below `0.006` against the eager external-pos wrapper. The
C++ runner produces finite output with this artifact:

- `512`: `35.5 ms` average forward over three timed iterations after one
  warmup iteration
- `1024`: `183.6 ms` average forward over three timed iterations after one
  warmup iteration
- `1536`: `678.9 ms` average forward over two timed iterations after one
  warmup iteration

The blue FP32 `2048` maximum profile does not build on the RTX 3080 10 GB
validation machine. Limiting TensorRT workspace to `4 GiB` still fails with no
implementation for the fused encoder/decoder TensorRT segment after rejecting
a tactic that requests roughly `8.9 GiB`. The blue TensorRT candidate is
separate from the promoted dynamic blue TorchScript product artifact.

#### Dynamic TorchScript Pinned Upload Probe

The LibTorch-backed dynamic path now packs the model input into pinned host
memory when the allocation succeeds and uploads to CUDA with non-blocking
transfer semantics. A clean OFX RPC harness run at `2048 -> 3840x2160`,
random input, source passthrough, and bilinear output resize reports:

- green dynamic: `1186.8 ms` average roundtrip over six frames;
  `torchtrt_prepare_upload` is `0.12 ms` to `0.18 ms` on hot frames;
  `torchtrt_forward` remains `673 ms` to `683 ms` on hot frames
- blue dynamic: `1209.5 ms` average roundtrip over four frames;
  `torchtrt_prepare_upload` is `0.12 ms` to `0.15 ms` on hot frames;
  `torchtrt_forward` remains `669 ms` to `675 ms` on hot frames

This keeps the DMA fix because it removes upload spikes from the measured hot
path, but it does not change the conclusion that the primary gap versus the
green ONNX TensorRT RTX EP path is model execution, not host upload.

#### Dynamic TorchScript GPU Prepare Probe

The LibTorch-backed dynamic path now reuses the established `GpuInputPrep`
normalization and resize path before the TorchScript call. The session still
materializes the prepared planar tensor on host before uploading it into
LibTorch, so this is a measured reuse of the existing GPU prep kernel rather
than a fully device-resident handoff.

The OFX RPC harness run at `2048 -> 3840x2160`, random input, source
passthrough, and bilinear output resize reports:

- green dynamic: `1141.7 ms` average roundtrip over six frames; hot
  `frame_prepare_inputs` is `33.1 ms` to `45.4 ms`; hot `torchtrt_forward`
  remains `668 ms` to `673 ms`
- blue dynamic: `1105.4 ms` average roundtrip over six frames; hot
  `frame_prepare_inputs` is `33.6 ms` to `36.6 ms`; hot `torchtrt_forward`
  remains `669 ms` to `681 ms`

A shorter `1024 -> 1920x1080` cross-check reports `313.7 ms` average
roundtrip for green dynamic and `309.2 ms` for blue dynamic over four frames,
with hot forward calls at about `130 ms`.

The probe is kept because it reduces the measured dynamic path against the
pinned-upload checkpoint while preserving green and blue parity. The remaining
gap is the model forward plus the temporary host/device boundary between
`GpuInputPrep` and LibTorch.

#### Dynamic TorchScript Device Input Probe

The dynamic path now can hand the `GpuInputPrep` planar CUDA buffer directly to
LibTorch as a CUDA tensor. This uses LibTorch's non-owning external-memory
tensor contract, with `GpuInputPrep` retaining ownership of the backing buffer
for the duration of the synchronized forward. The path removes the temporary
device-to-host download plus pinned host re-copy before inference.

A back-to-back `2048 -> 3840x2160` OFX RPC harness comparison on the same
machine state, random input, source passthrough, and bilinear output resize
reports:

- previous GPU-prep plus pinned upload path: `1375.2 ms` average roundtrip over
  six frames; `torchtrt_prepare_planar_copy` averages `6.9 ms`
- device input path: `1294.5 ms` average roundtrip over six frames;
  `torchtrt_prepare_device_wrap` and `torchtrt_prepare_device_cast` together
  stay below `0.1 ms` on hot frames

A green and blue cross-check with the same `2048 -> 3840x2160` harness settings
reports `1366.6 ms` average roundtrip for green dynamic and `1279.6 ms` for blue
dynamic over six frames. Both traces use the device input handoff on rendered
frames; the remaining `torchtrt_prepare_upload` sample belongs to warmup.

The device input probe is kept because it removes an avoidable host/device
boundary and improves the dynamic path under direct comparison. The dominant
remaining costs are still model forward, output materialization, and CPU-side
full-frame post-processing.

#### Dynamic TorchScript GPU Output Resize Probe

The dynamic path now mirrors the Python engine's output-resize placement for
bilinear output scaling: alpha and foreground stay as CUDA tensors through the
model-output resize, then the cropped and resized tensors are packed into one
host transfer. The Lanczos path remains CPU-side because the maintained CUDA
path is bilinear.

A `2048 -> 3840x2160` OFX RPC harness run with random input, source
passthrough, and bilinear output resize reports:

- green dynamic: `1110.3 ms` average roundtrip over six frames;
  `frame_extract_outputs_resize` averages `0.6 ms`; hot `torchtrt_forward`
  stays at about `662 ms` to `674 ms`
- blue dynamic: `1114.9 ms` average roundtrip over six frames;
  `frame_extract_outputs_resize` averages `0.7 ms`; hot `torchtrt_forward`
  stays at about `660 ms` to `673 ms`

A sequential `1024 -> 1920x1080` cross-check reports `301.4 ms` average
roundtrip for green dynamic and `311.0 ms` for blue dynamic over six frames.
Hot forward calls stay at about `128 ms` to `131 ms`, matching the prior
dynamic forward floor rather than the invalid concurrent-GPU probe.

The GPU output-resize probe is kept because it removes the CPU output-resize
work from the bilinear dynamic path without changing the dynamic artifact
contract or the Lanczos fallback.

#### Dynamic TorchScript OFX Auxiliary Output Skip Probe

The OFX runtime service transports alpha and foreground through shared memory.
The OFX plugin applies per-node alpha adjustments, restores any screen-color
canonicalization, converts foreground to linear, and writes the host output
itself. Core `FrameResult` processed and composite images are therefore file and
CLI outputs, not OFX transport outputs.

The OFX RPC harness now exposes `output_auxiliary_images` so the same binary can
compare the previous full `FrameResult` behavior with the OFX transport
behavior. Measurements use dynamic TorchScript, `2048 -> 3840x2160`, random
input, source passthrough, and bilinear output resize:

- green with auxiliary images enabled:
  - average roundtrip over four frames: `1141.4 ms`
  - render frames include `post_premultiply` plus `post_composite`, averaging
    `38.2 ms` per frame
- green with auxiliary images disabled:
  - average roundtrip over four frames: `1137.2 ms`
  - render frames omit `post_premultiply` and `post_composite`
  - last two hot frames improve from `1045.3 ms` to `1005.9 ms`
- blue with auxiliary images enabled:
  - average roundtrip over four frames: `1211.2 ms`
  - render frames include `post_premultiply` plus `post_composite`, averaging
    `40.7 ms` per frame
- blue with auxiliary images disabled:
  - average roundtrip over four clean frames: `1037.0 ms`
  - render frames omit `post_premultiply` and `post_composite`
  - last three hot frames improve from `1071.4 ms` to `971.3 ms`

Current reading: keep this OFX-specific transport flag because it removes
deterministically unused host work without changing alpha, foreground, source
passthrough, despeckle, despill, or CLI/file outputs. The dominant remaining
dynamic RTX cost is still `torchtrt_forward`.

#### Dynamic TorchScript Same-Resolution GPU Prep Probe

The dynamic TorchScript path no longer routes frames that already match the
requested runtime resolution through TorchTrtSession's host packing path. It
reuses `GpuInputPrep` and hands the prepared CUDA planar buffer to LibTorch.
`GpuInputPrep` skips NPP resize calls when source and target dimensions match,
while preserving the existing NPP resize path when scaling is required.

A same-resolution `2048 -> 2048` OFX RPC harness comparison with random input,
source passthrough disabled, bilinear output resize, and auxiliary images
disabled reports:

- host pack checkpoint: `822.8 ms` average roundtrip over five frames; hot
  `torchtrt_prepare_pack` is `18.8 ms` to `19.3 ms`; hot `torchtrt_forward` is
  `646.6 ms` to `667.6 ms`
- device prep with no-op resize bypass: `830.9 ms` average roundtrip over five
  frames; hot `frame_prepare_inputs` is `13.7 ms` to `14.5 ms`;
  `torchtrt_prepare_device_wrap` stays below `0.1 ms`; hot `torchtrt_forward`
  is `665.2 ms` to `679.0 ms`

A `2048 -> 3840x2160` cross-check with source passthrough enabled reports
`1082.7 ms` average roundtrip over four frames; hot frames stay between
`975.2 ms` and `1003.6 ms` with the unchanged resize path.

Current reading: keep the no-op resize bypass because it removes redundant
preprocessing work and keeps the scaled path unchanged. It is not a replacement
for a true TensorRT engine because `torchtrt_forward` remains the dominant
cost.

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

### `torchtrt_dynamic_windows_transport`

This checkpoint uses the Windows TorchTRT readiness matrix with dynamic green
and blue artifacts at `3840x2160 -> 2048`. It covers processed output,
alpha-only output, source passthrough, heavier source passthrough, despeckle,
tiling, dedicated blue, and Blue-Green Channel Swap.

Decision: keep the Windows OFX shared-frame path on named shared memory rather
than a temporary file mapping for generated runtime frames. Microsoft documents
the `CreateFileMapping(INVALID_HANDLE_VALUE, ..., name)` pattern as named
shared memory for processes that share memory without an associated file. The
plugin still supports explicit file-backed mappings for tests and non-generated
paths.

Decision: do not clear the entire shared-frame payload on creation. The client
fully writes `rgb` and `hint`, and the runtime fully writes `alpha` and
`foreground` before either result plane is read. Clearing the full mapped
payload touched about `265 MB` per UHD frame and added about `90 ms` per frame
without changing visual output.

Measured readiness results after the transport correction:

- green model `2048`: `p50 303.8 ms`, finite output
- blue model `2048`: `p50 682.1 ms`, finite output
- green processed `2048`: `531.9 ms` average RPC roundtrip
- green source passthrough `2048`: `600.9 ms` average RPC roundtrip
- green heavy source passthrough `2048`: `620.0 ms` average RPC roundtrip
- green despeckle `2048`: `565.6 ms` average RPC roundtrip
- blue processed `2048`: `1012.0 ms` average RPC roundtrip
- blue source passthrough `2048`: `1091.1 ms` average RPC roundtrip
- Blue-Green Channel Swap `2048`: `550.0 ms` average RPC roundtrip

The measured transfer shape changed materially:

- generated transport creation is no longer a frame bottleneck:
  `ofx_client_transport_create` averages about `0.1 ms`
- host-to-device upload remains visible but bounded:
  `gpu_prepare_upload_enqueue` averages about `47-54 ms`
- device-to-host output is no longer the dominant issue:
  `torchtrt_output_d2h_direct` averages about `14-16 ms`
- green E2E is now dominated by `torchtrt_forward`, about `310-313 ms`
- blue E2E is now dominated by `torchtrt_forward`, about `725-799 ms` in the
  readiness runs

Current reading: the Resolve-visible slowdown was not caused by green model
inference. The unoptimized Windows transport and unnecessary full-payload clear
were real E2E costs that the previous runner-only gate did not expose. The
readiness matrix now reports client transport stages and ignores aggregate RPC
wrapper stages when naming the dominant bottleneck, so this class of failure is
covered by automation instead of manual Resolve testing alone.

### `torchtrt_dynamic_windows_pinned_input`

This checkpoint keeps the dynamic Windows TorchTRT artifact set and changes only
the host memory source used by GPU input preparation. The RGB and hint frames are
copied into persistent CUDA pinned host staging buffers when `cudaMallocHost`
succeeds; the existing pageable upload path remains the fallback when pinned
allocation is unavailable.

Decision: keep this input staging path. NVIDIA's CUDA documentation defines
page-locked host memory as the higher-bandwidth path for host-to-device
transfers and states that asynchronous copies require page-locked memory. The
Resolve logs showed `gpu_prepare_upload_enqueue` blocking for hundreds of
milliseconds from the OFX runtime path even though model inference stayed near
the expected green TorchTRT range, so moving the upload source to pinned memory
matches both the measured bottleneck and the vendor guidance.

Measured readiness results after pinned input staging:

- green model `2048`: `p50 313.8 ms`, finite output
- blue model `2048`: `p50 723.0 ms`, finite output
- green processed `2048`: `498.1 ms` average RPC roundtrip
- green source passthrough `2048`: `556.7 ms` average RPC roundtrip
- green heavy source passthrough `2048`: `547.8 ms` average RPC roundtrip
- green despeckle `2048`: `502.1 ms` average RPC roundtrip
- blue processed `2048`: `1001.6 ms` average RPC roundtrip
- blue source passthrough `2048`: `1075.7 ms` average RPC roundtrip
- Blue-Green Channel Swap `2048`: `493.9 ms` average RPC roundtrip

The measured transfer shape after pinned input staging:

- `gpu_prepare_pinned_stage` averages about `13.0 ms` in the green heavy source
  passthrough case
- `gpu_prepare_upload_enqueue` averages about `0.08 ms` in the same case
- `gpu_prepare_sync` averages about `5.9 ms` in the same case
- `frame_prepare_inputs` averages about `28.9 ms` in the same case
- green E2E remains dominated by `torchtrt_forward`, about `310-314 ms`

Current reading: the repo-side readiness harness now validates the input memory
placement fix directly. This does not prove Resolve will match the harness under
host GPU contention, but it removes the exact upload behavior that the latest
Resolve logs exposed and does not change color conversion, resize, normalization,
post-processing, or model outputs.

### `torchtrt_dynamic_windows_async_input`

This checkpoint keeps the same dynamic Windows TorchTRT artifacts and changes
the synchronization contract between GPU input preparation and TorchTRT. The
device-prepared input path records a CUDA event on the preparation stream and
passes that event to TorchTRT, where the current CUDA stream waits on it before
wrapping the prepared planar tensor. The host-output `prepare_inputs` path still
synchronizes because it must read the prepared tensor back to CPU memory.

Decision: keep this handoff. NVIDIA's CUDA runtime documentation defines
`cudaEventRecord` as capturing the work currently queued in a stream and defines
`cudaStreamWaitEvent` as the stream-side dependency primitive for waiting on
that captured work. The Resolve logs showed `gpu_prepare_sync` dominating the
green E2E path while model-only green TorchTRT stayed near `~290-310 ms`, so a
device-side stream dependency matches the measured bottleneck without changing
resize, normalization, model math, source passthrough, despill, despeckle, or
writeback semantics.

Measured readiness results after async input handoff:

- green model `2048`: `p50 290.9 ms`, finite output
- blue model `2048`: `p50 677.3 ms`, finite output
- green processed `2048`: `456.4 ms` average RPC roundtrip
- green processed bilinear `2048`: `450.3 ms` average RPC roundtrip
- green source passthrough `2048`: `488.9 ms` average RPC roundtrip
- green heavy source passthrough `2048`: `503.0 ms` average RPC roundtrip
- green alpha-only `2048`: `391.1 ms` average RPC roundtrip
- green despeckle `2048`: `461.8 ms` average RPC roundtrip
- blue processed `2048`: `934.2 ms` average RPC roundtrip
- blue source passthrough `2048`: `1043.5 ms` average RPC roundtrip
- Blue-Green Channel Swap `2048`: `452.2 ms` average RPC roundtrip

The measured stage shape after async input handoff:

- `frame_prepare_inputs` averages about `19-22 ms` across non-tiled green RPC
  cases
- `gpu_prepare_pinned_stage` averages about `10.7-12.4 ms`
- `gpu_prepare_sync` is absent from the device-prepared TorchTRT path
- `torchtrt_input_wait_event_enqueue` is the ordering point between preparation
  and inference
- green E2E is now dominated by `torchtrt_forward`, about `293-298 ms`

Current reading: the fixed path now matches the same device-resident intent as
the optimized ONNX green path for input preparation. The next measurable green
targets are post-processing and host writeback for parameter-heavy paths; the
input synchronization regression is covered by the unit GPU stage test and by
the Windows TorchTRT readiness matrix.

### `torchtrt_dynamic_windows_gpu_postprocess_writeback`

This checkpoint keeps the dynamic Windows TorchTRT artifact set and changes the
OFX output contract plus the TorchTRT post-processing path. The OFX runtime now
passes alpha and foreground output spans into the engine. TorchTRT writes the
final transport planes directly when the selected output mode and dimensions
match the OFX transport. Green and blue GPU paths apply despill on CUDA tensors;
source passthrough also runs on CUDA tensors when auxiliary outputs and
despeckle are not requested.

Decision: keep direct output views. The OFX broker writeback stage measured
about `0.000 ms` in the readiness matrix because the runtime no longer creates
a full `FrameResult` payload and then copies it back into the shared transport
for the normal OFX render path.

Decision: keep TorchTRT GPU despill and source passthrough. The matrix covers
processed, alpha-only, source-passthrough, heavy source-passthrough, blue, and
Blue-Green Channel Swap paths with the same stage timing contract. The steady
green source-passthrough post stage is about `21 ms` after the first CUDA/Torch
kernel setup frame, and visual math stays on the same alpha/output contract.

Decision: reject the NPP connected-component despeckle experiment. The
implementation followed the NPP marker-label compression contract, including
CUDA destination storage, matching pitch requirements, stream context, and
contour buffers for compressed label info. It passed functionally after those
requirements were satisfied, but green despeckle measured `569.2 ms` average RPC
roundtrip against the accepted `516.2 ms` checkpoint, a `+10.3%` regression.
The accepted path keeps CPU despeckle for this optional parameter because it
preserves visual quality and measures `471.0 ms` after direct output views.

Measured readiness results after GPU post-processing and direct writeback:

- green model `2048`: `p50 291.1 ms`, finite output
- blue model `2048`: `p50 681.1 ms`, finite output
- green alpha-only `2048`: `385.9 ms` average RPC roundtrip
- green processed `2048`: `467.0 ms` average RPC roundtrip
- green processed bilinear `2048`: `462.3 ms` average RPC roundtrip
- green source passthrough `2048`: `599.8 ms` average RPC roundtrip
- green heavy source passthrough `2048`: `540.7 ms` average RPC roundtrip
- green despeckle `2048`: `463.7 ms` average RPC roundtrip
- green tiled `2048`: `977.8 ms` average RPC roundtrip
- blue processed `2048`: `911.4 ms` average RPC roundtrip
- blue source passthrough `2048`: `951.3 ms` average RPC roundtrip
- Blue-Green Channel Swap `2048`: `462.8 ms` average RPC roundtrip

The measured stage shape after direct writeback:

- `ofx_broker_writeback` averages about `0.000 ms` in direct-output OFX cases
- non-tiled green cases are still dominated by `torchtrt_forward`, about
  `295-318 ms` depending on color mode
- `torchtrt_output_d2h_direct` averages about `36-45 ms` in the covered OFX
  cases
- green source-passthrough `post_source_passthrough_gpu` averages about
  `21 ms` after first-frame CUDA/Torch setup
- auto-despeckle uses the CPU post-processing path and measures about
  `5 ms` for `post_despeckle`

Current reading: the accepted changes port the output/writeback and GPU
post-processing wins that are valid for the TorchTRT dynamic path without
weakening quality. The optional despeckle parameter remains CPU-side because the
measured GPU connected-component path regressed beyond the hot-path gate. The
remaining green cost is model inference plus unavoidable device-to-host output
download for the OFX transport.

### `torchtrt_dynamic_windows_cuda_graph_replay_sync`

This checkpoint keeps the dynamic Windows TorchTRT artifact set and changes the
TorchTRT forward path instrumentation and CUDA Graph execution path. The OFX
runtime report now records CUDA Graph configuration, warmup, capture, replay,
direct-forward fallback, and sanitized fallback reason stages. The Windows
TorchTRT matrix rejects a case when graph configuration is enabled but neither a
replay nor an explicit fallback stage is present.

Decision: keep TorchTRT CUDA Graph capture on a side stream with persistent
static input and output tensors. The implementation follows the same constraints
used by the ONNX runtime path: CUDA Graph replay uses stable virtual memory
addresses, warmup runs before capture, capture runs on a non-default stream, and
shape changes reset the recorded graph.

Decision: keep synchronized replay timing. Earlier telemetry measured only the
enqueue cost and shifted the model work into output materialization. The accepted
path synchronizes replay inside `torchtrt_cuda_graph_replay`, so
`torchtrt_forward` is the measured model stage instead of a hidden async cost.

Measured readiness results with synchronized replay telemetry:

- green model `2048`: `p50 285.6 ms`, `p99 287.8 ms`, finite output, TensorRT
  marker present, external positional metadata present
- blue model `2048`: `p50 665.6 ms`, `p99 667.5 ms`, finite output, TensorRT
  marker present
- green alpha-only `2048`: `417.3 ms` average RPC roundtrip
- green processed `2048`: `518.6 ms` average RPC roundtrip
- green processed plate `2048`: `507.8 ms` average RPC roundtrip
- green source passthrough `2048`: `634.5 ms` average RPC roundtrip
- green source passthrough plate `2048`: `556.5 ms` average RPC roundtrip
- green heavy source passthrough `2048`: `585.7 ms` average RPC roundtrip
- green heavy source passthrough plate `2048`: `595.2 ms` average RPC roundtrip
- green despeckle `2048`: `515.6 ms` average RPC roundtrip
- green despeckle plate `2048`: `552.7 ms` average RPC roundtrip
- green tiled `2048`: `1063.2 ms` average RPC roundtrip
- blue processed `2048`: `996.6 ms` average RPC roundtrip
- blue source passthrough `2048`: `1089.2 ms` average RPC roundtrip
- Blue-Green Channel Swap `2048`: `521.7 ms` average RPC roundtrip

The measured stage shape after synchronized replay:

- green and Blue-Green non-tiled RPC cases report
  `torchtrt_cuda_graph_config_enabled`, replay samples, and zero fallback
  events
- blue RPC cases report `torchtrt_cuda_graph_config_marker_missing` and direct
  forward samples because the current blue dynamic artifact is not the external
  positional-metadata TorchTRT path
- non-tiled green cases are dominated by `torchtrt_forward`, about
  `313-319 ms` in the readiness matrix
- direct single-case OFX probe measured `torchtrt_cuda_graph_replay` at
  `289.7 ms`, `torchtrt_output_d2h_direct` at `33.4 ms`, and
  `frame_prepare_inputs` at `19.7 ms`

Current reading: the CUDA Graph replay implementation is correct under the
standalone runner and RPC harness, but a real Resolve/Fusion session exposed a
separate host-contract issue around render-safety declaration and render-thread
panel writes. That issue must stay separate from model artifact validation: the
same installed build showed stable green replay in the harness and slow replay
only under the real Resolve render context.

### TorchTRT Resolve Host Contract And Queue Attribution

Decision: advertise the OFX plugin as `kOfxImageEffectRenderInstanceSafe` with
`kOfxImageEffectPluginPropHostFrameThreading = 0`. The render path already owns
an instance-level `render_mutex`, so this descriptor matches the OpenFX contract
for one render action per instance without telling Fusion that the plugin is
globally render-unsafe.

Decision: defer runtime-panel `paramSetValue` during both `Render` and the
`BeginSequenceRender`/`EndSequenceRender` window for every host. Runtime logs
remain the live per-frame telemetry surface; panel params are synchronized from
main-thread actions.

Decision: keep CUDA event elapsed telemetry alongside the CPU wall-time stage:

- `torchtrt_cuda_graph_replay` is the CPU wait for graph replay completion
- `torchtrt_cuda_graph_replay_gpu` is the elapsed GPU work between CUDA events
- `torchtrt_forward_direct` is the CPU wait for direct forward completion
- `torchtrt_forward_direct_gpu` is the elapsed GPU work between CUDA events

Resolve/Fusion log evidence that motivated this checkpoint:

- the installed `w9558167b4cf0` build used green TorchTRT `2048`,
  `source_passthrough=1`, `sp_erode_px=6`, and `sp_blur_px=14`
- that manual session had `17` render requests with request `p50 1500.5 ms`
  and `torchtrt_cuda_graph_replay p50 1367.8 ms`
- the same build's automated green source-passthrough sessions kept graph replay
  near `315-318 ms`
- Resolve logged that `OfxImageEffectRenderUnsafe` is not supported in Fusion
- the OFX log recorded `585` `OfxActionInstanceChanged` entries during the
  manual render window
- the sidecar lifecycle was clean in that session: release destroyed the session,
  the runtime server logged `server_shutdown` and `server_stop`, and no
  CorridorKey/OFX process remained alive

Measured readiness results after the host-contract and attribution fix:

- green model `2048`: `p50 293.0 ms`, `p99 293.9 ms`, finite output, TensorRT
  marker present, external positional metadata present
- blue model `2048`: `p50 681.9 ms`, `p99 682.7 ms`, finite output, TensorRT
  marker present
- green alpha-only `2048`: `390.5 ms` average RPC roundtrip
- green processed `2048`: `468.8 ms` average RPC roundtrip
- green source passthrough `2048`: `562.6 ms` average RPC roundtrip
- green source passthrough plate `2048`: `507.0 ms` average RPC roundtrip
- green heavy source passthrough `2048`: `543.1 ms` average RPC roundtrip
- green heavy source passthrough plate `2048`: `530.5 ms` average RPC roundtrip
- green despeckle `2048`: `465.6 ms` average RPC roundtrip
- green tiled `2048`: `988.2 ms` average RPC roundtrip
- blue processed `2048`: `986.1 ms` average RPC roundtrip
- blue source passthrough `2048`: `992.4 ms` average RPC roundtrip
- Blue-Green Channel Swap `2048`: `515.4 ms` average RPC roundtrip

The measured queue split after the fix:

- non-tiled green RPC cases report replay CPU/GPU deltas of about `5-8 ms` in
  the harness
- blue RPC cases report direct-forward CPU/GPU deltas of about `5-12 ms` in the
  harness
- a future Resolve manual run where CPU replay remains near seconds while GPU
  replay remains near `290-315 ms` is queue/contention wait, not model compute
- a future Resolve manual run where both CPU replay and GPU replay rise together
  is actual model execution slowdown under the Resolve render context

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

- `scripts/windows.ps1 -Task release -Version X.Y.Z -Track rtx -Flavor online`

When recording a manual OFX run, always write down:

- checkpoint label
- visible version label from the panel
- whether build identity was confirmed by panel, CLI, or hash
- the tested quality rung and backend
- the stage timings or log summary used for comparison
