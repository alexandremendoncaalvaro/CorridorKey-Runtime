# Task `0003`: Instrument TorchTRT Queue Wait

**Status:** done
**Created:** 2026-05-09
**Owner:** Runtime maintainers
**Spec ref:** doc/specs/0001-torchtrt-resolve-performance.md
**Board ref:**

## Context

Resolve-driven TorchTRT renders are slow even though the model replay GPU event
is much lower than the total render time. Boundary logs now show the dominant
wait at `torchtrt_input_ready_wait`, before static graph input copy and model
replay. Harness runs with the same model and source passthrough settings remain
much faster, which means this task's scope is to make the stream boundary
measurable from logs. The execution-topology fix is tracked separately in task
`0004`.

## Acceptance Criteria

Verifiable conditions. Each as a checkbox so progress is point-editable.

- [x] Render logs expose the input preparation producer cost, external CUDA
  event wait, static input copy, CUDA Graph replay wall time, CUDA Graph replay
  GPU time, replay queue wait, output copy, readback, color conversion, and OFX
  writeback.
- [x] Resolve logs identify whether the dominant wait is before CUDA Graph
  replay, during replay, during output materialization, during readback, or
  during OFX writeback.
- [x] Resolve logs distinguish GPU-prep device work from producer-stream wait
  with `gpu_prepare_device` and `gpu_prepare_wait_over_device`.
- [x] The selected fix path is justified against official CUDA/PyTorch/OpenFX
  docs, relevant open-source examples, internal examples, and repository
  history.
- [x] The next fix path is captured in a separate task and ADR after this
  instrumentation isolated the failing boundary.

## Plan

Concrete sequential steps. Each as a checkbox. Reference file paths where
applicable.

- [x] Capture the current slow Resolve evidence from
  `%LOCALAPPDATA%/CorridorKey/Logs`.
- [x] Record the official documentation and open-source examples used to reason
  about CUDA events, stream waits, CUDA Graph replay, PyTorch graph streams, and
  OpenFX render sequencing.
- [x] Compare internal patterns in `src/core/gpu_prep.cpp`,
  `src/core/torch_trt_session.cpp`, `src/plugins/ofx/ofx_render.cpp`, and
  `src/app/ofx_runtime_service.cpp`.
- [x] Compare historical commits that touched TorchTRT graph replay, output
  synchronization, GPU prep, OFX serialization, and prior wait-bound runtime
  regressions.
- [x] Add missing stage timings in `src/core/torch_trt_session.cpp`,
  `src/core/gpu_prep.cpp`, `src/plugins/ofx/ofx_render.cpp`, and
  `src/app/ofx_runtime_service.cpp` as needed.
- [x] Run the OFX RPC benchmark harness with matching Resolve parameters.
- [x] Run Resolve manual captures with the instrumented package.
- [x] Record that high-priority independent GPU-prep stream did not resolve the
  Resolve wait.
- [x] Run canonical Windows build, relevant tests, and packaging through
  `scripts/windows.ps1`.

## Notes

### 2026-05-09

Resolve logs show slow frames dominated by `torchtrt_cuda_graph_replay_queue_wait`
while `torchtrt_cuda_graph_replay_gpu` remains much lower. Official CUDA and
PyTorch docs, NVIDIA CUDA sample patterns, internal GPU prep/TorchTRT code, and
historical wait-bound fixes all support instrumenting stream boundaries before
changing stream topology.

Instrumentation added boundary stages for `torchtrt_input_ready_wait`,
`torchtrt_cuda_graph_input_copy_gpu`,
`torchtrt_cuda_graph_input_copy_queue_wait`,
`torchtrt_cuda_graph_capture_stream_wait`, and
`torchtrt_cuda_graph_current_stream_wait`. The OFX render summary now surfaces
those stages next to replay, readback, color conversion, and writeback timings.

The OFX RPC harness with Green 2048, 3840x2160 plate input, source passthrough
enabled, `sp_erode=6`, and `sp_blur=14` completed successfully. Average
roundtrip was 492.41 ms; per-frame replay GPU stayed near 282 ms, replay queue
wait stayed near 0.07 ms average, and input-ready wait stayed under 9 ms. This
keeps the Resolve-specific queue wait as the open variable for manual A/B.

The next Resolve run with the instrumented package moved the missing time out of
`torchtrt_cuda_graph_replay_queue_wait` and into `torchtrt_input_ready_wait`.
Sixteen backend renders averaged 2004.91 ms total, 1356.32 ms RPC, 902.82 ms
input-ready wait, 294.18 ms TorchTRT forward, 293.42 ms replay GPU, 36.40 ms
GPU post-process, 71.20 ms output D2H, 37.38 ms OFX readback, 15.12 ms
foreground color conversion, and 17.16 ms OFX write. Runtime details show
`frame_prepare_inputs` only enqueues about 12 ms of CPU work, so the slow stage
is waiting for the GPU-prep stream completion event rather than model replay.

Official CUDA Runtime documentation defines `cudaStreamCreateWithPriority`,
`cudaDeviceGetStreamPriorityRange`, `cudaStreamWaitEvent`, and event elapsed
timing for this exact stream-boundary problem. NVIDIA's CUDA Samples
StreamPriorities example uses the same priority-range query and highest-priority
stream creation pattern. The internal pattern is `src/core/gpu_prep.cpp`
recording a completion event consumed by `src/core/torch_trt_session.cpp`.
Repository history shows commit `4369213` introduced asynchronous GPU-prep
event handoff and a nonblocking default-priority prep stream; the minimal fix is
to create the GPU-prep stream at highest available CUDA priority with a safe
fallback and to report `gpu_prepare_device` versus
`gpu_prepare_wait_over_device` in Resolve logs.

The high-priority GPU-prep stream and timing-event telemetry build passed with
display label `0.8.5-win.1-49-g1d07bb2-dirty-w18a0577a9512`. Verification
passed: `git diff --check`, canonical Windows release build, unit tests,
integration tests, regression tests, OFX RPC harness, and
`scripts/windows.ps1 -Task package-ofx -Version 0.8.5 -Preset release -Track rtx
-Flavor online`. The harness averaged 508.12 ms over five 3840x2160 Green 2048
plate iterations with `gpu_prepare_device` near 7 ms,
`gpu_prepare_wait_over_device` at 0 ms, input-ready wait near 5 ms, and replay
GPU near 296 ms. The new installer is
`dist/CorridorKey_v0.8.5-win.1-49-g1d07bb2-dirty-w18a0577a9512_Windows_online_Setup.exe`.
SHA256: `f3f8604b9f653afadd01627ab85f5be6960069915b52edb33882158a908ca311`.

TorchTRT smoke matrix passed at
`build/release/torchtrt_matrix_smoke_w18a0577a9512/torchtrt_matrix_report.json`
with zero failures and zero regressions. Green 2048 model mean was 284.3 ms;
Green processed 2048 RPC averaged 518.31 ms; Green alpha-only 2048 RPC averaged
384.31 ms; Blue-Green 2048 RPC averaged 525.66 ms. The matrix classifies the
dominant stage as `torchtrt_forward` for all smoke RPC cases, which matches the
harness result that input-ready wait is not the automated-path bottleneck.

The subsequent Resolve logs with the same build show that the high-priority
independent prep stream did not resolve the manual-render wait. Backend render
summaries show `gpu_prepare_device` around 7 to 13 ms while
`gpu_prepare_wait_over_device` remains around 834 to 1445 ms, with
`torchtrt_input_ready_wait` matching that wait plus device prep. Replay GPU
remains around 276 to 289 ms and replay queue wait remains near zero. This
closes the instrumentation task: the next work is not more attribution, but the
execution-topology change recorded in ADR-0003 and task `0004`.

## Definition of Done

All Acceptance Criteria checked, plus:

- [x] Local tests pass (or N/A documented in Notes)
- [ ] Code review completed (human or fresh-context reviewer per WORKFLOW
  section 10)
- [x] No orphan `TODO`/`FIXME` introduced
- [x] Status updated to `done` and Notes log closes the task
