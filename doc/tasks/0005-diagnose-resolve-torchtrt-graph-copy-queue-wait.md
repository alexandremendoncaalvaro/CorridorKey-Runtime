# Task `0005`: Diagnose Resolve TorchTRT Graph Copy Queue Wait

**Status:** active
**Created:** 2026-05-09
**Owner:** Runtime maintainers
**Spec ref:** doc/specs/0001-torchtrt-resolve-performance.md
**Board ref:**

## Context

Task `0004` removed the earlier Resolve-only waits at
`gpu_prepare_wait_over_device` and `torchtrt_input_ready_wait` by moving the
TorchTRT prepared-input path onto an owned PyTorch work stream. The same real
Resolve window still renders much slower than the clean OFX RPC harness.

Filtered Resolve logs for package `0.8.5-win.1-61-g6c6df24`, single PID
`40112`, show `gpu_prepare_wait_over_device=0`,
`torchtrt_input_ready_wait=0`, and `torchtrt_input_copy_queue_wait` averaging
about 806 ms with a maximum around 1249 ms. The measured device copy remains
around 0.10 ms and graph replay GPU time averages about 307 ms. The remaining
failure is therefore the queue wait before CUDA Graph replay, not raw copy
bandwidth and not replay alone.

The same failure reproduced on diagnostic package
`0.8.5-win.1-65-g22b01d3-dirty-w59c6f8033046`. A single-PID Resolve window
with 14 backend renders showed `torchtrt_work_stream_guard_present=1`,
`gpu_prepare_wait_over_device=0`, `torchtrt_input_ready_wait=0`, and
`torchtrt_input_copy_queue_wait` averaging about 975 ms while the measured
device copy remained about 0.10 ms.

This task owns the next diagnostic slice. It must separate a CUDA Graph
static-input-copy interaction from Resolve host/context contention before any
broader post-process or OFX writeback optimization is attempted.

## Acceptance Criteria

Verifiable conditions. Each as a checkbox so progress is point-editable.

- [x] Resolve log analysis uses a single-PID, time-filtered window and records
  `work_origin` counts so cached frames cannot hide backend-render behavior.
- [x] `ofx_render_summary` and the analyzer distinguish a missing
  `torchtrt_work_stream_guard_ms` field from a real zero-duration stage.
- [x] A diagnostic CUDA Graph disabled run is measured in Resolve with the same
  Green 2048 settings and source-passthrough parameters as the failing window.
- [x] A diagnostic main-style host-roundtrip input path is measured in Resolve
  with the same settings, without replacing the primary device-input path.
- [x] The OFX RPC harness still runs the comparable Green 2048 case so the
  Resolve-only gap is visible beside the automated path.
- [x] The outcome identifies whether the remaining wait is CUDA Graph specific,
  host/context specific, or still unattributed.
- [x] Any selected implementation fix is captured in a follow-up ADR before code
  changes if it changes execution topology.

## Plan

Concrete sequential steps. Each as a checkbox. Reference file paths where
applicable.

- [x] Update `scripts/analyze_resolve_ofx_logs.ps1` if needed so missing stage
  fields are reported separately from numeric zero.
- [x] Verify `src/plugins/ofx/ofx_render.cpp` emits
  `torchtrt_work_stream_guard_ms` in the installed package used for Resolve
  testing.
- [x] Add a diagnostic-only way to run the TorchTRT Resolve path with CUDA Graph
  disabled while preserving all other settings.
- [x] Add a diagnostic-only way to run the TorchTRT Resolve path with a
  main-style synchronized host-roundtrip input boundary.
- [x] Build and package through `scripts/windows.ps1`.
- [x] Run the clean OFX RPC harness case for Green 2048 processed/source
  passthrough.
- [x] Run the Resolve manual A/B windows and capture them with
  `scripts/analyze_resolve_ofx_logs.ps1 -SinceLocalTime`.
- [x] Record the comparison in Notes and either close this task or open the
  implementation ADR/task for the selected fix.
- [x] Implement the first non-topology slice: direct-forward queue-wait
  telemetry, post-sync GPU-prep device timing, and pure-inference guard.
- [x] Re-run the automated Green 2048 harness/matrix slice and compare
  `torchtrt_forward_direct_queue_wait`, `gpu_prepare_device`,
  `post_gpu_prepare`, and total latency.
- [x] Change the OFX runtime default so TorchTRT CUDA Graph is opt-in while the
  device-input path remains the default inference topology.
- [x] Rebuild, retest the focused gates, package the local RTX installer, and
  record the new installer hash before manual Resolve validation.
- [x] Split direct-forward queue wait into host enqueue wall time and CUDA event
  synchronization wait, following the TensorRT `Enqueue Time` versus `GPU
  Compute Time` diagnostic model.
- [x] Re-run the automated Green 2048 harness and verify the new split stays
  small outside Resolve.
- [ ] Use the next Resolve run only to classify whether the remaining direct
  wait is enqueue-bound or event-sync-bound.

## Priority Order

The next work is diagnostic classification, not broad optimization. The
current evidence already rules out model replay, post-process, readback, and
OFX writeback as the dominant 1.8 second-class cost in the failing Resolve
window.

| Priority | Run | Required environment before starting Resolve | Evidence required | Decision rule |
| --- | --- | --- | --- | --- |
| P0 | Default graph-on/device-input baseline | `CORRIDORKEY_TRT_CUDA_GRAPH=1`; `CORRIDORKEY_TORCHTRT_INPUT_BOUNDARY` unset | `torchtrt_work_stream_guard_present=1`; single PID; backend renders only; `torchtrt_input_copy_queue_wait` still dominates | Done for package `0.8.5-win.1-65-g22b01d3-dirty-w59c6f8033046`; the wait is reproduced. |
| P1 | CUDA Graph off, device-input unchanged | `CORRIDORKEY_TRT_CUDA_GRAPH=0`; `CORRIDORKEY_TORCHTRT_CUDA_GRAPH=0`; `CORRIDORKEY_TORCHTRT_INPUT_BOUNDARY` unset | `server_start` records both graph envs as `0`; `torchtrt_cuda_graph_fallback_not_enabled_present_count > 0`; `torchtrt_forward_direct_present_count > 0`; `torchtrt_input_copy_queue_wait` absent or zero | If total render time drops near the OFX RPC harness class, classify as CUDA Graph specific and open the implementation ADR. |
| P2 | Host-roundtrip input boundary, graph still off | `CORRIDORKEY_TRT_CUDA_GRAPH=0`; `CORRIDORKEY_TORCHTRT_CUDA_GRAPH=0`; `CORRIDORKEY_TORCHTRT_INPUT_BOUNDARY=host_roundtrip` | `torchtrt_input_boundary_host_roundtrip_present_count > 0`; `torchtrt_forward_direct_present_count > 0`; same Green 2048/source-passthrough settings | If P1 remains slow but P2 improves, classify as device-input boundary/context interaction. If both are slow, classify as Resolve host/context contention outside CUDA Graph static-input copy. |
| P3 | Record outcome and choose fix path | No new code before classification | Notes include analyzer JSON summary, selected classification, and whether a follow-up ADR is required | Only implement after the classification is documented. |

Use `scripts/run_resolve_torchtrt_diagnostic.ps1 -Mode graph-off
-LaunchResolve` for P1 and `scripts/run_resolve_torchtrt_diagnostic.ps1 -Mode
host-roundtrip -LaunchResolve` for P2 when launching Resolve from this repo.
Use `-ApplyUserEnvironment` only when Resolve must be started manually outside
the helper, then restart Resolve before measuring.

## Notes

### 2026-05-09

Task opened from the post-ADR-0004 Resolve validation. The accepted owned work
stream remains useful because it removed the previous readiness waits in both
the harness and the real Resolve window. The unresolved evidence is now narrower:
the static-input copy queue wait dominates only in Resolve, while the same
stage is small in the clean OFX RPC harness.

Added the Task 0005 diagnostic controls and log schema. Resolve log analysis now
accepts `-Pid` in addition to `-SinceLocalTime`, reports `work_origin` counts,
and separates `torchtrt_work_stream_guard_ms` field presence from the
`torchtrt_work_stream_guard_present` stage flag. The runtime server start line
records `CORRIDORKEY_TRT_CUDA_GRAPH`, `CORRIDORKEY_TORCHTRT_CUDA_GRAPH`,
`CORRIDORKEY_IO_BINDING`, and `CORRIDORKEY_TORCHTRT_INPUT_BOUNDARY` values. The
existing CUDA Graph control remains the graph-off diagnostic; set both CUDA
Graph environment variables to `0` before starting Resolve so the TorchTRT
alias cannot keep graph replay enabled. The host-roundtrip diagnostic is
`CORRIDORKEY_TORCHTRT_INPUT_BOUNDARY=host_roundtrip`, which bypasses the
device-input boundary without changing the default path.

Verification passed for the diagnostic package: `git diff --check`,
`scripts/windows.ps1 -Task build -Version 0.8.5 -Preset release`,
unit/regression/matrix-label tests, and integration tests. The clean OFX RPC
harness Green 2048 case with 3840x2160 plate input, source passthrough enabled,
`sp_erode=6`, `sp_blur=14`, bilinear upscale, and CUDA Graph enabled completed
20 iterations successfully. It averaged 578.36 ms roundtrip,
`ofx_client_render_rpc=495.32 ms`, `frame_prepare_inputs=17.11 ms`,
`torchtrt_work_stream_guard` present, `gpu_prepare_wait_over_device=0`,
`torchtrt_input_ready_wait=0`, `torchtrt_cuda_graph_input_copy_gpu=0.10 ms`,
`torchtrt_cuda_graph_input_copy_queue_wait=6.26 ms`, `torchtrt_forward=282.30
ms`, `torchtrt_cuda_graph_replay_gpu=273.49 ms`, `post_gpu_prepare=41.57 ms`,
and `torchtrt_output_d2h_direct=79.91 ms`. The automated path still does not
reproduce the Resolve-only ~800-1000 ms static-input-copy queue wait.

The local RTX online installer was produced and validated through
`scripts/windows.ps1 -Task package-ofx -Version 0.8.5 -Preset release -Track rtx
-Flavor online`. Installer:
`dist/CorridorKey_v0.8.5-win.1-65-g22b01d3-dirty-w59c6f8033046_Windows_online_Setup.exe`.
SHA256: `0bf3edb3e5ee12affb5acd515e4624e4fbf048a884f8a9e124da9e2adf154030`.

Resolve validation on the same package confirmed the diagnostic fields are
present in the installed plugin. The selected window used
`scripts/analyze_resolve_ofx_logs.ps1 -TailSummaries 20 -SinceLocalTime
"2026-05-09 22:24:12" -Pid 34048`, which selected 14 backend renders from a
single plugin PID. The runtime start line recorded `cuda_graph_env=1`,
`torchtrt_cuda_graph_env=unset`, `io_binding_env=on`, and
`torchtrt_input_boundary=unset`, so this was still the default graph-on
device-input path. The averages were `total_ms=1753.87`,
`ofx_client_render_rpc_ms=1425.77`, `frame_prepare_inputs_ms=13.87`,
`gpu_prepare_wait_over_device_ms=0`, `torchtrt_input_ready_wait_ms=0`,
`torchtrt_input_copy_queue_wait_ms=975.35`, `torchtrt_replay_gpu_ms=299.97`,
`post_gpu_prepare_ms=35.63`, `torchtrt_output_d2h_direct_ms=64.78`,
`ofx_client_readback_ms=38.99`, and `ofx_write_output_ms=17.73`. This locks the
next priority to P1: CUDA Graph off with device-input unchanged.

Automated preflight A/B outside Resolve completed with the same Green 2048
3840x2160 plate/source-passthrough settings. Graph-on device-input averaged
578.36 ms with `torchtrt_cuda_graph_input_copy_queue_wait=6.26 ms`. Graph-off
device-input averaged 480.82 ms, emitted `torchtrt_forward_direct`, removed the
static graph input-copy wait, and kept `frame_prepare_inputs=14.50 ms`. Graph-off
host-roundtrip averaged 595.17 ms; it also removed the static graph input-copy
wait, but moved work to CPU input prep and CPU post-process
(`frame_prepare_inputs=84.53 ms`, `post_source_passthrough=73.61 ms`). The
automated result supports prioritizing P1 over P2 for the real Resolve
classification.

Added `scripts/run_resolve_torchtrt_diagnostic.ps1` so P1/P2 Resolve launches
use the exact environment contract recorded in the priority table. The script
can either launch Resolve with process-scoped variables or set user-level
variables when the manual launch path is unavoidable.

Resolve P1 graph-off validation ran on the same package after launching through
`scripts/run_resolve_torchtrt_diagnostic.ps1 -Mode graph-off -LaunchResolve`.
The selected backend-render-only window used plugin PID `29864` and 40 samples.
The runtime start line recorded `cuda_graph_env=0`,
`torchtrt_cuda_graph_env=0`, `io_binding_env=off`, and
`torchtrt_input_boundary=unset`; the summaries recorded
`torchtrt_forward_direct_present=40`,
`torchtrt_cuda_graph_fallback_not_enabled_present=40`, and
`torchtrt_input_copy_queue_wait_ms=0`. This proves the prior
`torchtrt_cuda_graph_input_copy_queue_wait` stall is CUDA Graph specific.

The P1 run did not resolve the real Resolve latency. Backend renders averaged
`total_ms=1915.95`, `ofx_client_render_rpc_ms=1661.67`,
`frame_prepare_inputs_ms=12.30`, `torchtrt_forward_direct_ms=1386.74`,
`torchtrt_forward_direct_gpu_ms=427.02`, `post_gpu_prepare_ms=167.07`,
`torchtrt_output_d2h_direct_ms=61.54`, `ofx_client_readback_ms=32.40`, and
`ofx_write_output_ms=15.82`. Several runtime detail lines show direct-forward
wall time around 1200-1700 ms while direct GPU event time is around 280-350 ms,
with separate occasional GPU spikes up to about 1981 ms. The next diagnostic is
therefore P2 host-roundtrip with graph still off, to test whether the remaining
Resolve-only direct-forward wall time is tied to the device-input boundary or
to broader Resolve host/context contention.

Resolve P2 host-roundtrip validation ran on the same package after launching
through `scripts/run_resolve_torchtrt_diagnostic.ps1 -Mode host-roundtrip
-LaunchResolve`. The selected backend-render-only window used plugin PID
`27452` and 27 samples. The runtime start line recorded `cuda_graph_env=0`,
`torchtrt_cuda_graph_env=0`, `io_binding_env=off`, and
`torchtrt_input_boundary=host_roundtrip`; the summaries recorded
`torchtrt_input_boundary_host_roundtrip_present=27`,
`torchtrt_forward_direct_present=27`,
`torchtrt_cuda_graph_fallback_not_enabled_present=27`, and
`torchtrt_input_copy_queue_wait_ms=0`.

P2 did not improve total Resolve latency, so host-roundtrip is not a candidate
implementation fix. Backend renders averaged `total_ms=2144.02`,
`ofx_client_render_rpc_ms=1735.82`, `frame_prepare_inputs_ms=86.86`,
`torchtrt_forward_direct_ms=1016.99`,
`torchtrt_forward_direct_gpu_ms=287.85`,
`torchtrt_output_d2h_direct_ms=2.95`, `ofx_client_readback_ms=19.42`, and
`ofx_write_output_ms=15.08`. Runtime detail stages after the matching
`server_start` show the CPU fallback costs introduced by the diagnostic:
`frame_extract_outputs_resize` averaged 293.34 ms and
`post_source_passthrough` averaged 264.92 ms. The device-input boundary appears
to contribute to direct-forward wall time, but removing it shifts enough work
to CPU that the overall Resolve path regresses.

The diagnostic classification is now: the old static input-copy queue wait is
CUDA Graph specific; the remaining end-to-end Resolve slowness is not fixed by
disabling CUDA Graph or by forcing a main-style host input boundary. The next
implementation slice must target the Resolve-only host/context gap around
TorchTRT direct forward and the expensive CPU/output fallback stages, without
promoting host-roundtrip to the normal path.

The selected first implementation slice does not change execution topology, so
it does not require a follow-up ADR before code changes. Official PyTorch C++
guidance recommends `c10::InferenceMode` over `NoGradGuard` for pure inference
workloads, and its CUDA stream guidance supports the existing
`CUDAStreamGuard` integration point. Official CUDA stream/event guidance
confirms that events measure device progress while host wall time can include
queued prior work. The code change therefore keeps the owned work stream,
keeps host-roundtrip diagnostic-only, emits
`torchtrt_forward_direct_queue_wait`, emits `gpu_prepare_device` after the
frame has synchronized, and runs TorchTRT forward/materialization inside
`InferenceMode`.

Implemented the first non-topology slice. The TorchTRT forward/materialization
path now runs under `c10::InferenceMode`, direct fallback reports
`torchtrt_forward_direct_queue_wait`, the OFX render summary and analyzer expose
that field, and prepared-input runs emit `gpu_prepare_device` after the frame's
real synchronization point instead of hiding device-prep duration behind the
next synchronizing stage.

Verification passed: `git diff --check`,
`scripts/windows.ps1 -Task build -Version 0.8.5 -Preset release`,
`ctest --test-dir build/release -R "unit_tests_gpu|integration_tests|windows_torchtrt_matrix_case_coverage" --output-on-failure`,
and the focused OFX RPC Green 2048 plate/source-passthrough harness. Graph-off
direct averaged 492.76 ms after the change versus 480.82 ms in the prior JSON,
inside the 10% regression budget; the new split showed
`gpu_prepare_device=7.70 ms` and
`torchtrt_forward_direct_queue_wait=6.94 ms`, proving the automated path still
does not reproduce the Resolve-only ~1 second direct-forward wait. Graph-on
averaged 490.09 ms after the change versus 578.36 ms in the prior JSON;
`gpu_prepare_device=7.05 ms`,
`torchtrt_cuda_graph_input_copy_queue_wait=6.78 ms`,
`torchtrt_cuda_graph_replay_gpu=290.44 ms`, and
`torchtrt_output_d2h_direct=40.73 ms`.

The local RTX online installer for Resolve validation was produced and
validated through `scripts/windows.ps1 -Task package-ofx -Version 0.8.5 -Preset
release -Track rtx -Flavor online`. Installer:
`dist/CorridorKey_v0.8.5-win.1-72-g4b72798_Windows_online_Setup.exe`. SHA256:
`a9319de0f5505ef0203b6b9d8ed1e0a99c9ccef800f8ee8833a40b2d5b0e701`.

Resolve validation on that installer still used the graph-on product default.
The selected backend-render-only window used plugin PID `32688` and 22 samples
from line `213415` through `215432`. The runtime start line recorded
`cuda_graph_env=1`, `torchtrt_cuda_graph_env=unset`, `io_binding_env=on`, and
`torchtrt_input_boundary=unset`. Averages were `total_ms=2184.60`,
`ofx_client_render_rpc_ms=1618.07`, `frame_prepare_inputs_ms=13.65`,
`gpu_prepare_wait_over_device_ms=0`, `torchtrt_input_ready_wait_ms=0`,
`torchtrt_input_copy_queue_wait_ms=1055.34`, `torchtrt_replay_gpu_ms=294.13`,
`post_gpu_prepare_ms=151.48`, `torchtrt_output_d2h_direct_ms=67.24`,
`ofx_client_readback_ms=35.50`, and `ofx_write_output_ms=16.78`. The measured
input copy remained around 0.10 ms in the raw runtime details, so the default
graph-on policy is still inserting a Resolve-specific queue wait before replay.

ADR-0005 records the selected topology change: the OFX runtime server must
default CUDA Graph off unless `CORRIDORKEY_TRT_CUDA_GRAPH=1` or
`CORRIDORKEY_TORCHTRT_CUDA_GRAPH=1` is supplied explicitly. This does not solve
the remaining direct-forward wall-time gap, but it removes the proven graph
static-input-copy default regression before continuing with the next
instrumented slice.

Implemented ADR-0005 in the OFX runtime server entrypoint. With no graph-related
environment variables set, a direct server startup check recorded
`cuda_graph_env=0`, `torchtrt_cuda_graph_env=unset`, `io_binding_env=off`, and
`torchtrt_input_boundary=unset`.

Verification passed: `git diff --check`,
`scripts/windows.ps1 -Task build -Version 0.8.5 -Preset release`, and
`ctest --test-dir build/release -R
"unit_tests_gpu|integration_tests|windows_torchtrt_matrix_case_coverage"
--output-on-failure`. The integration suite now includes a regression guard
that launches the real Windows OFX runtime server with graph variables unset
and checks the startup log for `cuda_graph_env=0`.

The focused OFX RPC Green 2048 plate/source-passthrough harness was rerun with
the new default and no graph environment variables. Output:
`build/release/task0005_rpc_green_2048_default_graph_off_after_regression_test.json`.
It averaged `avg_latency_ms=494.08`, `ofx_client_render_rpc=429.99 ms`,
`frame_prepare_inputs=16.38 ms`, `gpu_prepare_device=8.92 ms`,
`gpu_prepare_wait_over_device=0`, `torchtrt_input_ready_wait=0`,
`torchtrt_forward_direct=295.89 ms`,
`torchtrt_forward_direct_gpu=287.81 ms`,
`torchtrt_forward_direct_queue_wait=8.08 ms`,
`post_gpu_prepare=37.01 ms`, and `torchtrt_output_d2h_direct=43.16 ms`.
The runtime log for that harness process recorded `cuda_graph_env=0`, proving
the package default now uses the direct TorchTRT path.

The local RTX online installer was produced and validated through
`scripts/windows.ps1 -Task package-ofx -Version 0.8.5 -Preset release -Track rtx
-Flavor online`. Installer:
`dist/CorridorKey_v0.8.5-win.1-74-g2c57c94_Windows_online_Setup.exe`.
SHA256: `7d3ba23f2770493298433f6e665b93d840fe25a4f238acc5f59f97e956e712cf`.

Resolve validation on the clean `0.8.5-win.1-74-g2c57c94` installer confirms
ADR-0005 is active in the host. The first new run used plugin PID `860`; the
second run, after clearing user-level diagnostic environment variables, used
plugin PID `42052`. Both runtime server starts in the matching runtime log
recorded `cuda_graph_env=0`, `torchtrt_cuda_graph_env=unset`,
`io_binding_env=off`, and `torchtrt_input_boundary=unset`.

The valid post-clear window is PID `42052`, backend renders only, 22 samples
from `2026-05-09 23:53:20` through `2026-05-09 23:54:23`. Averages were
`total_ms=1904.69`, `ofx_client_render_rpc_ms=1431.33`,
`frame_prepare_inputs_ms=13.26`, `gpu_prepare_wait_over_device_ms=0`,
`torchtrt_input_ready_wait_ms=0`, `torchtrt_input_copy_queue_wait_ms=0`,
`torchtrt_forward_direct_ms=1189.88`,
`torchtrt_forward_direct_gpu_ms=288.99`,
`torchtrt_forward_direct_queue_wait_ms=900.89`,
`post_gpu_prepare_ms=141.09`, `torchtrt_output_d2h_direct_ms=52.70`,
`ofx_client_readback_ms=28.64`, and `ofx_write_output_ms=16.49`. Excluding the
first backend-render outlier, average total time was still `1745.19 ms` and the
direct-forward queue wait averaged `943.74 ms`.

A matching automated OFX RPC harness was run with the Resolve-effective settings
now visible in logs: Green 2048, 3840x2160 plate input, Source Passthrough on,
Lanczos4, `sp_erode=12`, and `sp_blur=28`. Output:
`build/release/task0005_rpc_green_2048_default_graph_off_lanczos_sp12_28_after_resolve.json`.
It averaged `avg_latency_ms=701.15`, `ofx_client_render_rpc=610.17 ms`,
`torchtrt_forward_direct=281.04 ms`,
`torchtrt_forward_direct_gpu=274.12 ms`,
`torchtrt_forward_direct_queue_wait=6.92 ms`,
`post_source_passthrough_gpu=134.80 ms`,
`post_gpu_prepare=135.47 ms`, and `torchtrt_output_d2h_direct=97.65 ms`.

The new classification is narrower: CUDA Graph static-input-copy wait is fixed
by default-off policy, and Source Passthrough/post-process cost is real but
matches the automated harness when the effective 12/28 radii are reproduced.
The remaining Resolve-only gap is the queue before direct TorchTRT forward:
about `900 ms` in Resolve versus about `7 ms` in the matching harness.

The next slice follows the official TensorRT/trtexec diagnostic pattern rather
than treating `wall - gpu_event` as a single root cause. CUDA events measure the
device-timestamped interval between recorded events; TensorRT reports host
`Enqueue Time` separately from `GPU Compute Time` and warns when enqueue becomes
comparable to compute. The code therefore must emit
`torchtrt_forward_direct_enqueue_wall` around `module.forward` and
`torchtrt_forward_direct_event_sync_wait` around `cudaEventSynchronize`; it
also emits `torchtrt_forward_direct_event_sync_over_gpu` so the wait above the
measured GPU compute is visible while keeping the historical
`torchtrt_forward_direct_queue_wait` aggregate.

Implementation and automated validation completed. Verification passed:
`git diff --check`, `scripts/windows.ps1 -Task build -Version 0.8.5 -Preset
release`, and `ctest --test-dir build/release -R
"unit_tests_gpu|integration_tests|windows_torchtrt_matrix_case_coverage"
--output-on-failure`. The matching Green 2048 RPC harness with plate input,
Source Passthrough on, Lanczos4, `sp_erode=12`, and `sp_blur=28` wrote
`build/release/task0005_rpc_green_2048_direct_split_probe.json`. Across the
eight measured frames it averaged `617.58 ms` roundtrip,
`torchtrt_forward_direct=296.07 ms`,
`torchtrt_forward_direct_gpu=286.71 ms`,
`torchtrt_forward_direct_queue_wait=9.36 ms`,
`torchtrt_forward_direct_enqueue_wall=4.93 ms`,
`torchtrt_forward_direct_event_sync_wait=291.07 ms`,
`torchtrt_forward_direct_event_sync_over_gpu=4.74 ms`,
`post_gpu_prepare=140.81 ms`, and `torchtrt_output_d2h_direct=46.74 ms`. This
keeps the automated path in the same class as before and gives the next Resolve
run a precise classifier: high `enqueue_wall` means TorchTRT/PyTorch enqueue is
the local bottleneck; high `event_sync_over_gpu` with small `enqueue_wall`
means the runtime is waiting for the stream/event to be scheduled under the
Resolve GPU context.

The local RTX online installer for Resolve validation was produced and
validated through `scripts/windows.ps1 -Task package-ofx -Version 0.8.5 -Preset
release -Track rtx -Flavor online` after rebuilding with the clean commit label.
Installer:
`dist/CorridorKey_v0.8.5-win.1-77-g35adcf8_Windows_online_Setup.exe`. SHA256:
`969ccf5fa5a84fdfce22624c89a6037a4242e12f6b331c8e77bec4b557b32828`.

## Definition of Done

All Acceptance Criteria checked, plus:

- [x] Local tests pass (or N/A documented in Notes)
- [ ] Code review completed (human or fresh-context reviewer per WORKFLOW
  section 10)
- [x] No orphan `TODO`/`FIXME` introduced
- [ ] Status updated to `done` and Notes log closes the task
