# Optimization Measurements

## Why This Document Exists

CorridorKey performance work must distinguish model time, runtime pipeline time,
and real host time. This file is the operational ledger for that distinction. It
keeps the current baseline, accepted checkpoints, and measurement rules short
enough to use during a regression instead of becoming a narrative archive.

`phase_8_gpu_prepare` remains the standing Windows RTX hot-path regression
baseline required by `AGENTS.md`. Any render hot-path change must be measured
against that baseline with the canonical corpus gate unless a newer accepted ADR
or task record explicitly replaces it.

## Current Reading

| Area | Evidence | Current decision |
| --- | --- | --- |
| ONNX Windows RTX baseline | `phase_8_gpu_prepare` removed the dominant CPU input-prep bottleneck and is the repository gate named by `AGENTS.md`. | Keep as the regression baseline for `avg_latency_ms` and `ort_run`. |
| TorchTRT model path | Clean readiness matrix at commit `6c6df24`, `dirty=false`: `model_green_2048 p50=285.6 ms`, `model_blue_2048 p50=667.0 ms`; all model cases finite and TensorRT-marked. | Green model inference is not the 1.8 s Resolve wall-clock by itself. Blue remains model-dominated. |
| TorchTRT OFX RPC harness | Clean readiness matrix at commit `6c6df24`, `dirty=false`: green non-tiled RPC cases range from `385.2 ms` to `618.3 ms`; green tiled is `1008.6 ms`; blue RPC cases range from `892.0 ms` to `991.5 ms`. | The in-repo sidecar path is now useful as an automated guard, but it does not prove Resolve host behavior. |
| Real Resolve / OFX host | Filtered `ofx.log` summaries from the latest single-pid Resolve window show backend frames around `1.55-2.17 s` after earlier `~2.0 s` input-ready waits. The latest problematic window has `gpu_prepare_wait_over_device_ms=0` and `torchtrt_input_ready_wait_ms=0`, but `torchtrt_input_copy_queue_wait_ms=840-1249 ms` while `torchtrt_input_copy_gpu_ms` stays around `0.10 ms`. | Do not claim full isolation from Resolve/OFX. Current evidence points to queue/host-context interaction before CUDA graph replay, not raw copy bandwidth or model execution alone. |
| Resolve panel "last frame render" | The panel value is sourced from `event=ofx_render_summary total_ms`. Cached frames report `work_origin=shared_cache` and can have much lower `total_ms` while carrying the previous backend stage totals for attribution. | Manual panel reads must be compared with `work_origin`, not only with the displayed number. |
| Tiling | Clean matrix `green_tiled_2048` reports `1008.6 ms` with many CUDA graph replays and a weak dominant-stage attribution (`ofx_broker_writeback avg=14.8 ms`). | Keep the case, but treat dominant-stage classification for tiled aggregate renders as incomplete. |

## Accepted Checkpoints

| Checkpoint | Scope | Representative result | Decision |
| --- | --- | --- | --- |
| `pre_opt` | Manual Resolve/OFX starting point for the optimization track. | Baseline was user-visible but not a controlled corpus gate. | Keep only as historical comparison context. |
| `phase_0_1_shared_ort` | Shared ORT environment, global pools, richer timings. | Improved architecture and attribution; no clear OFX throughput win. | Keep the structure, not speed claims. |
| `phase_1_direct_planar_resize` | Direct planar resize and output attribution. | First measurable direction toward the extract/resize bottleneck. | Superseded by later GPU-resident slices. |
| `phase_3_host_postprocess` | Host-side post-processing and OFX writeback loops. | Helped user-facing writeback but did not close the main bottleneck. | Keep only when comparing host post-processing changes. |
| `phase_4_input_prepare` | Parallel host prepare and fused normalized packing. | `1024` OFX-style roundtrip improved about `10.9%`; `frame_prepare_inputs` improved about `24.2%`. | Accepted prepare-path checkpoint. |
| `phase_5_preview_writeback` | Full-frame OFX harness and fused preview composite. | Full-frame `2048 -> 3840x2160` OFX-style average improved about `8.1%`; `post_composite` improved about `81.9%`. | Accepted full-frame OFX checkpoint. |
| `phase_6_device_tensors` | Pinned output buffers and vectorized FP16-to-FP32 conversion. | Full-frame OFX-style average improved about `2.7%`; `ort_run` improved about `3.7%`. | Accepted DMA/vectorization checkpoint. |
| `phase_7_gpu_resize` | Device-resident tensor flow and GPU bilinear resize. | Full-frame OFX-style average improved about `28%`; resize extraction dropped to about `27 ms`. | Accepted major GPU-residency checkpoint. |
| `phase_8_gpu_prepare` | NPP input preparation, split, resize, normalize on GPU. | Full-frame `frame_prepare_inputs` improved by about `74%`. | Standing regression baseline. |
| `torchtrt_dynamic_windows_async_input` | CUDA event handoff from GPU preparation to TorchTRT. | Green heavy source-passthrough `2048` RPC moved from Resolve-observed `~1431 ms` class to `~503 ms` in the readiness matrix. | Accepted as the input-memory-placement fix, not as proof Resolve is solved. |
| `torchtrt_dynamic_windows_gpu_postprocess_writeback` | Direct OFX output views plus GPU despill/source-passthrough. | Green alpha-only `2048` RPC improved from `435.5 ms` to `385.9 ms`; OFX broker writeback near `0 ms`. | Accepted; avoid reintroducing transport writeback copies. |
| `torchtrt_dynamic_windows_cuda_graph_replay_sync` | CUDA graph capture/replay telemetry. | Green model `2048` runner around `285-293 ms`; replay was visible in runner and RPC harness. | Accepted, but Resolve can still expose queue waits around graph input. |
| `torchtrt_dynamic_windows_resolve_host_contract` | `RenderInstanceSafe`, deferred panel param writes, replay CPU/GPU split. | Removed host-contract noise and made Resolve queue waits visible. | Accepted host contract; continue measuring Resolve separately. |
| `torchtrt_owned_work_stream` | Owned TorchTRT work stream guarded during input prep and forward. | Clean harness removed broad input-ready waits; Resolve still shows static input-copy queue waits in the plugin log. | Accepted as a stream-boundary fix; not the final Resolve performance fix. |

## Current TorchTRT Matrix Snapshot

Source: `build/release/torchtrt_matrix/torchtrt_matrix_report.json`,
profile `readiness`, commit `6c6df24`, `dirty=false`, success `true`,
failures `0`, regressions `0`.

### Model Runner

| Case | p50 ms | p99 ms | Status |
| --- | ---: | ---: | --- |
| `model_green_512` | 13.7 | 13.7 | finite, TensorRT marker present |
| `model_green_1024` | 50.7 | 51.1 | finite, TensorRT marker present |
| `model_green_2048` | 285.6 | 288.8 | finite, TensorRT marker present |
| `model_blue_512` | 35.7 | 37.7 | finite, TensorRT marker present |
| `model_blue_1024` | 130.6 | 131.3 | finite, TensorRT marker present |
| `model_blue_2048` | 667.0 | 673.6 | finite, TensorRT marker present |

### OFX RPC Harness

| Case | Avg ms | Dominant stage | Dominant avg ms | Graph replay |
| --- | ---: | --- | ---: | ---: |
| `green_alpha_only_2048` | 385.2 | `torchtrt_forward` | 292.5 | 4 |
| `green_processed_bilinear_2048` | 451.1 | `torchtrt_forward` | 292.1 | 4 |
| `green_despeckle_2048` | 465.3 | `torchtrt_forward` | 291.7 | 4 |
| `green_despeckle_plate_2048` | 489.0 | `torchtrt_forward` | 288.8 | 4 |
| `green_despeckle_random_2048` | 494.5 | `torchtrt_forward` | 289.1 | 4 |
| `green_source_passthrough_2048` | 540.6 | `torchtrt_forward` | 281.3 | 4 |
| `green_processed_lanczos_2048` | 547.4 | `torchtrt_forward` | 284.6 | 4 |
| `green_processed_lanczos_plate_2048` | 547.5 | `torchtrt_forward` | 280.7 | 4 |
| `green_source_passthrough_plate_2048` | 561.4 | `torchtrt_forward` | 281.5 | 4 |
| `green_source_passthrough_heavy_2048` | 587.9 | `torchtrt_forward` | 281.0 | 4 |
| `green_source_passthrough_heavy_plate_2048` | 618.3 | `torchtrt_forward` | 286.8 | 4 |
| `green_tiled_2048` | 1008.6 | `ofx_broker_writeback` | 14.8 | 179 |
| `blue_processed_lanczos_2048` | 892.0 | `torchtrt_forward` | 734.0 | 0 |
| `blue_source_passthrough_plate_2048` | 894.4 | `torchtrt_forward` | 727.9 | 0 |
| `blue_source_passthrough_2048` | 943.3 | `torchtrt_forward` | 780.2 | 0 |
| `blue_processed_lanczos_plate_2048` | 991.5 | `torchtrt_forward` | 713.0 | 0 |
| `blue_green_swap_2048` | 542.9 | `torchtrt_forward` | 282.5 | 4 |
| `blue_green_swap_plate_2048` | 564.0 | `torchtrt_forward` | 283.2 | 4 |

## Resolve / OFX Boundary

The OFX side cannot be dismissed yet. The official OpenFX contract exposes
threading and render-window choices through properties such as
`kOfxImageEffectPluginRenderThreadSafety`,
`kOfxImageEffectRenderInstanceSafe`, and
`kOfxImageEffectPluginPropHostFrameThreading`. Parameter mutation is also
host-sensitive: `paramSetValue` belongs to instance-change or interact-style
flows, not arbitrary render-thread updates. Our current code already reflects
that contract in these places:

| Evidence | File |
| --- | --- |
| The plugin advertises `kOfxImageEffectRenderInstanceSafe` and disables host frame threading. | `src/plugins/ofx/ofx_actions.cpp` |
| Render sequence windows gate runtime-panel `paramSetValue` chains. | `src/plugins/ofx/ofx_shared.hpp`, `src/plugins/ofx/ofx_instance.cpp` |
| Unit coverage checks Nuke and Resolve parameter deferral during render. | `tests/unit/test_ofx_lifecycle.cpp` |
| Resolve-facing summaries log RPC, input waits, graph replay, readback, color conversion, cache origin, and output write. | `src/plugins/ofx/ofx_render.cpp`, `scripts/analyze_resolve_ofx_logs.ps1` |

Current diagnostic boundary:

| Observation | Meaning |
| --- | --- |
| Harness green `2048` non-tiled RPC is `385-618 ms`, while Resolve backend summaries show `1.55-2.17 s` frames. | The gap is host/context sensitive or workload-context sensitive; the sidecar harness is not enough. |
| Resolve summaries can show `torchtrt_input_copy_queue_wait_ms` near `840-1249 ms` while `torchtrt_input_copy_gpu_ms` is near `0.10 ms`. | The data copy itself is not the expensive work; the wait before it is. |
| The same summaries keep `torchtrt_replay_gpu_ms` around `272-362 ms` in the problematic window. | Raw graph replay/model execution is not the whole wall-clock. |
| `ofx_client_readback_ms`, `ofx_foreground_srgb_to_linear_ms`, and `ofx_write_output_ms` are usually tens of milliseconds in the same window. | These are worth tracking, but they do not explain a steady `~1.8 s` by themselves in the captured samples. |
| `work_origin=shared_cache` can display much lower `total_ms` while repeating prior backend attribution fields. | Panel numbers must be read with cache origin. |

When investigating Resolve, keep separate:

| Source | What it proves | What it cannot prove |
| --- | --- | --- |
| Model runner | Artifact validity, finite outputs, raw model latency. | OFX transport, Resolve GPU contention, panel timing. |
| OFX RPC harness | Sidecar transport, runtime server, shared-memory path, automated parameter matrix. | Resolve compositor/decoder/GPU scheduling behavior. |
| Resolve `ofx.log` | Real plugin panel, cache origin, host-triggered render cadence, user-visible wall-clock. | Exact runtime process identity unless the panel/version label or server log is tied to the same test window. |
| Runtime server log | Internal stage details and artifact label for one server process. | Resolve-only behavior after automated harnesses append to the same log directory. |

## Measurement Gates

| Change type | Required gate | Reject when |
| --- | --- | --- |
| ONNX or shared render hot path | `scripts/run_corpus.sh` plus `scripts/compare_benchmarks.py` against `phase_8_gpu_prepare`. | `avg_latency_ms` or `ort_run` regresses by more than `10%`. |
| Windows TorchTRT runtime change | `scripts/windows.ps1 -Task build -Preset release`, focused tests, then `scripts/run_windows_torchtrt_matrix.ps1 -Profile readiness -Preset release -SkipBuild`. | Matrix reports failures, regressions, constant invalid outputs, or missing expected graph replay/fallback telemetry. |
| Resolve-visible change | Package via `scripts/windows.ps1 -Task package-ofx -Track rtx -Flavor online`, install the produced label, then inspect `ofx.log` summaries from that exact manual window. | The panel/version label cannot be tied to the log window, or queue waits remain unexplained by stage telemetry. |
| OFX host-contract change | Unit tests covering lifecycle and render-thread parameter deferral. | `paramSetValue` can run from Render, BeginSequenceRender, EndSequenceRender, or SyncPrivateData paths where the host contract forbids it. |

## Recording Format

Add one row per accepted checkpoint. Do not add long chronological prose.

| Field | Required content |
| --- | --- |
| Checkpoint | Stable name, commit, artifact label, and dirty state. |
| Scope | Model runner, OFX RPC harness, real Resolve, corpus gate, or packaging. |
| Command/report | Exact command or report path. |
| Result | `avg_latency_ms`, relevant p50/p99, dominant stages, and queue waits. |
| Decision | Keep, reject, supersede, or follow-up. |

Example row shape:

| Checkpoint | Scope | Command/report | Result | Decision |
| --- | --- | --- | --- | --- |
| `torchtrt_owned_work_stream` | OFX RPC harness | `build/release/torchtrt_matrix/torchtrt_matrix_report.json` | Green non-tiled `385-618 ms`; graph replay present; dirty `false`. | Keep, but continue Resolve-only queue investigation. |

## Short Command Index

| Goal | Command |
| --- | --- |
| Build Windows release | `scripts/windows.ps1 -Task build -Version <version> -Preset release` |
| Package OFX installer | `scripts/windows.ps1 -Task package-ofx -Version <version> -Preset release -Track rtx -Flavor online` |
| Run focused release tests | `ctest --test-dir build/release -C Release --output-on-failure -R "unit_tests|unit_tests_gpu|integration_tests|regression_tests|windows_torchtrt_matrix_case_coverage"` |
| Run TorchTRT matrix | `scripts/run_windows_torchtrt_matrix.ps1 -Profile readiness -Preset release -SkipBuild` |
| Analyze Resolve logs | `scripts/analyze_resolve_ofx_logs.ps1 -TailSummaries 20 -SinceLocalTime "yyyy-MM-dd HH:mm:ss"` |

## External References

| Topic | Reference |
| --- | --- |
| OpenFX image-effect properties and render threading contract | `https://openfx.readthedocs.io/en/main/Reference/api/file/ofxImageEffect_8h.html` |
| OpenFX parameter suite contract | `https://openfx.readthedocs.io/en/main/Reference/api/file/ofxParam_8h.html` |
