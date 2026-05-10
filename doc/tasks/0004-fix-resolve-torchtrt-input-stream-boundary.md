# Task `0004`: Fix Resolve TorchTRT Input Stream Boundary

**Status:** blocked
**Created:** 2026-05-09
**Owner:** Runtime maintainers
**Spec ref:** doc/specs/0001-torchtrt-resolve-performance.md
**Board ref:**

## Context

Resolve manual renders remain slower than `main` and the branch harness even
though TorchTRT model replay is not the dominant measured cost. Task `0003`
isolated the missing time at the producer and consumer boundary between GPU
input preparation and TorchTRT inference. ADR-0003 removed the independent
GPU-prep stream wait. ADR-0004 closes the remaining default-stream gap by
making the TorchTRT session own and guard its work stream end to end.

ADR-0004 is implemented and accepted, but real Resolve validation did not close
the full user-visible performance issue. The older waits at
`gpu_prepare_wait_over_device` and `torchtrt_input_ready_wait` are gone in the
latest filtered Resolve window. The remaining dominant wait is now
`torchtrt_input_copy_queue_wait`, tracked separately by Task `0005`.

This task is P0 because it blocks meaningful performance comparison of model,
post-process, readback, and OFX writeback costs. No broader OFX or
post-processing optimization should be attempted until this boundary is removed
or falsified by Resolve logs.

## Evidence

- Latest Resolve logs show `gpu_prepare_device` around 7 to 13 ms and
  `gpu_prepare_wait_over_device` around 834 to 1445 ms.
- Latest Resolve logs show `torchtrt_replay_gpu` usually around 276 to 289 ms
  and `torchtrt_replay_queue_wait` near zero.
- The same build in the OFX RPC harness shows `gpu_prepare_wait_over_device` at
  0 ms and total processed Green 2048 frames near 508 to 518 ms.
- Resolve logs from package `0.8.5-win.1-54-g6c4dad5` show
  `GPU TorchScript prep failed, falling back to CPU: TorchTRT current CUDA
  stream is unavailable for GPU prep`, followed by CPU `frame_prepare_inputs`
  and `torchtrt_prepare_pack`.
- The same logs show `torchtrt_cuda_graph_input_copy_gpu` near 0.10 ms while
  `torchtrt_cuda_graph_input_copy_queue_wait` is about 734 to 807 ms, proving
  the reported copy wall time is queue wait, not the device copy itself.
- Official CUDA Runtime documentation defines `cudaStream_t` value `0` as the
  default stream. The code treated `nullptr` as unavailable, so a valid default
  current stream was rejected.
- Resolve logs from package `0.8.5-win.1-55-g746ab59` show that CPU fallback
  is gone and `torchtrt_cuda_graph_input_copy_queue_wait` is about 0.15 ms, but
  `torchtrt_input_ready_wait` still blocks around 0.84 to 1.38 seconds while
  `gpu_prepare_device` is only about 7 to 13 ms.
- Resolve logs from package `0.8.5-win.1-59-g7f5514a` show
  `torchtrt_input_ready_wait` and `gpu_prepare_wait_over_device` at 0 ms, while
  `torchtrt_cuda_graph_input_copy_queue_wait` dominates at about 840 to 1249 ms.
- Filtered Resolve logs from package `0.8.5-win.1-61-g6c6df24`, single PID
  `40112`, show `gpu_prepare_wait_over_device=0`,
  `torchtrt_input_ready_wait=0`, and `torchtrt_input_copy_queue_wait`
  averaging about 806 ms with a maximum about 1249 ms. The old stream-boundary
  waits are removed, but the frame is still blocked before CUDA Graph replay.
- The PyTorch CUDAStream implementation initializes thread-local current
  streams to the default stream and creates pooled streams with
  `cudaStreamNonBlocking`. The current code queried the current stream before
  installing a guard, so the prepared-input path used the default stream under
  Resolve.
- `main` avoids this exact boundary by synchronizing GPU prep and returning a
  host tensor before inference, but that path loses the branch's device-input
  optimization.
- Official CUDA documentation supports event waits but does not make stream
  priority a guarantee for this failure. Official PyTorch C++ CUDA stream
  documentation establishes the current stream as the integration point for
  adjacent custom CUDA work. Official NPP documentation supports stream
  contexts, so the NPP context must be created for the stream that owns this
  work.

## Priority

- P0: Remove the Resolve-specific GPU-prep producer wait from the TorchTRT
  prepared-input path.
- P0: Preserve correctness, device-input ownership, and source passthrough
  device source where available.
- P0: Verify with Resolve logs, because the harness does not reproduce the
  failing wait.
- P1: After P0, measure remaining peripheral costs: post-process, direct output
  copy, client readback, foreground conversion, and OFX writeback.
- P0 follow-up: Task `0005` owns the remaining CUDA Graph static-input copy
  queue wait exposed after this task's stream-boundary fixes.

## Acceptance Criteria

Verifiable conditions. Each as a checkbox so progress is point-editable.

- [x] The TorchTRT prepared-input path can enqueue GPU input preparation on the
  Torch current CUDA stream.
- [x] The TorchTRT prepared-input path uses an owned PyTorch work stream guarded
  by `CUDAStreamGuard`, not the unguarded default current stream.
- [x] NPP input-prep calls use an `NppStreamContext` bound to the stream that
  owns the TorchTRT input work.
- [x] The TorchTRT prepared-input path no longer requires
  `cudaStreamWaitEvent` on an independent GPU-prep completion event.
- [x] Public headers still do not expose CUDA, NPP, or LibTorch types.
- [x] The current-stream contract treats CUDA stream handle `0` as valid when
  the TorchTRT session reports stream availability.
- [x] Error handling remains `Result<T>` based for expected failures.
- [x] Source passthrough still uses device-to-device copy when the prepared
  source RGB device pointer is available.
- [x] Resolve logs show `gpu_prepare_wait_over_device` removed from the
  prepared-input path or near zero, and the missing wait does not reappear under
  another pinned stage.
- [x] Resolve logs show `torchtrt_input_ready_wait` remains zero for the
  current-stream prepared-input path.
- [ ] Resolve logs show `torchtrt_input_copy_queue_wait` no longer dominates
  the prepared-input CUDA Graph path.
- [ ] Resolve logs include `torchtrt_work_stream_guard_ms` for backend renders.
- [x] The OFX RPC harness remains green and does not regress its already-fast
  input-ready wait.
- [x] Canonical Windows build, relevant tests, and package flow run through
  `scripts/windows.ps1`.

## Plan

Concrete sequential steps. Each as a checkbox. Reference file paths where
applicable.

- [x] Add an internal-only GPU-prep entry point in `src/core/gpu_prep.cpp` and
  `src/core/gpu_prep.hpp` that accepts the owning CUDA stream as an opaque
  internal value and builds `NppStreamContext` for that stream.
- [x] Route the TorchTRT prepared path in `src/core/inference_session.cpp` and
  `src/core/torch_trt_session.cpp` so input prep and tensor consumption are
  ordered on the Torch current CUDA stream.
- [x] Route the TorchTRT prepared path through an owned work stream and guard it
  before tensor wrapping, cast, graph input copy, forward, and GPU
  post-process.
- [x] Separate current-stream availability from the opaque handle value so the
  CUDA default stream does not trigger CPU fallback.
- [x] Keep the existing independent-stream path only where it remains required
  by non-TorchTRT callers or tests, and keep telemetry able to prove which path
  ran.
- [x] Update focused tests or integration assertions so the selected path and
  telemetry contract are covered where the test environment can observe them.
- [x] Run `git diff --check`.
- [x] Run the canonical Windows release build through `scripts/windows.ps1`.
- [x] Run unit, regression, and integration tests relevant to GPU prep,
  TorchTRT, and OFX lifecycle.
- [x] Run the OFX RPC harness with Green 2048, processed output, source
  passthrough, and the same `sp_erode`/`sp_blur` parameters used in manual
  Resolve testing.
- [x] Produce a local RTX OFX package through `scripts/windows.ps1`.
- [x] Validate with Resolve logs and record the before/after evidence in Notes.
- [ ] If Resolve still shows a dominant wait, run the defined fallback A/B:
  main-style synchronized device prep as diagnostic only, then CUDA Graph on/off
  with the same parameters.

## Notes

### 2026-05-09

Task opened from the post-instrumentation Resolve evidence. The high-priority
independent GPU-prep stream is not the fix path because the latest Resolve logs
still show `gpu_prepare_wait_over_device` around 0.8 to 1.4 seconds.

Implemented the current-stream prepared-input path. `GpuInputPrep` now has an
internal stream-bound entry point that accepts an opaque CUDA stream, builds the
NPP stream context for that stream, records readiness events on that stream, and
marks the returned readiness event as current-stream owned. `InferenceSession`
routes the TorchTRT prepared-input path through the Torch current stream exposed
as an opaque pointer by `TorchTrtSession`. `TorchTrtSession` now skips
`cudaStreamWaitEvent` when the prepared-input event belongs to the current
stream.

Verification passed: `git diff --check`,
`scripts/windows.ps1 -Task build -Version 0.8.5 -Preset release`, unit tests,
regression tests, integration tests, and
`scripts/windows.ps1 -Task package-ofx -Version 0.8.5 -Preset release -Track
rtx -Flavor online`. The package output records the exact display label,
installer path, and SHA256 for each generated build.

The local OFX RPC harness remained green with Green 2048, 3840x2160 plate input,
source passthrough enabled, `sp_erode=6`, `sp_blur=14`, and bilinear upscale.
Average roundtrip was 633.30 ms over three iterations. This local harness run
did not exercise the prepared-input GPU path because the available packaged
model took the CPU pack/upload TorchTRT path and the external-pos dynamic
TorchTRT test artifact is absent from this workspace. Resolve manual logs remain
the required validation for the P0 wait removal.

Resolve logs from package `0.8.5-win.1-54-g6c4dad5` falsified the first
current-stream implementation. The runtime entered GPU prep, but the TorchTRT
current stream handle was `0`; `GpuInputPrep` interpreted that as unavailable
and forced CPU fallback. CUDA defines stream `0` as the default stream, so the
fix separates stream availability from the opaque handle and allows
`prepare_inputs_device_on_stream(..., nullptr, ...)` when the caller has already
proved that the stream is available.

Resolve logs from package `0.8.5-win.1-55-g746ab59` confirmed the default-stream
fix: GPU prep ran, CPU `torchtrt_prepare_pack` disappeared, and CUDA Graph input
copy queue wait fell to about 0.15 ms. The same logs exposed a second barrier:
`wait_for_external_cuda_event` still synchronized the Torch current stream only
to measure input readiness. The measured device prep was about 7 to 13 ms, while
the host wait was about 0.84 to 1.38 seconds. The selected follow-up fix removes
that host synchronization and keeps CUDA ordering through same-stream sequencing
or the enqueued event wait.

Resolve logs from package `0.8.5-win.1-59-g7f5514a` confirmed the host readiness
sync fix: `torchtrt_input_ready_wait` and `gpu_prepare_wait_over_device` are
zero. The dominant wait moved to `torchtrt_cuda_graph_input_copy_queue_wait`,
which measures about 840 to 1249 ms while the device copy itself remains around
0.10 ms. ADR-0004 selects a TorchTRT-owned PyTorch pooled work stream guarded
before the prepared-input tensor path, instead of querying the unguarded default
current stream.

Implemented ADR-0004. `TorchTrtSession` now owns a PyTorch pooled work stream,
returns that stream to GPU input preparation, and guards that stream before
prepared CUDA input consumption and regular TorchTRT inference paths. The OFX
render summary and log analyzer expose `torchtrt_work_stream_guard_ms`.

Local verification passed: `git diff --check`,
`scripts/windows.ps1 -Task build -Version 0.8.5 -Preset release`, relevant
unit/regression/integration tests, the OFX RPC harness with Green 2048
3840x2160 plate input/source passthrough/`sp_erode=6`/`sp_blur=14`, the
readiness TorchTRT matrix, and
`scripts/windows.ps1 -Task package-ofx -Version 0.8.5 -Preset release -Track
rtx -Flavor online`. The same focused harness case averaged 485.87 ms over 20
iterations, with `torchtrt_work_stream_guard` present,
`torchtrt_input_ready_wait=0`, `gpu_prepare_wait_over_device=0`, and
`torchtrt_cuda_graph_input_copy_queue_wait` averaging about 6.79 ms.

Filtered Resolve validation on package `0.8.5-win.1-61-g6c6df24` used
`scripts/analyze_resolve_ofx_logs.ps1 -TailSummaries 20 -SinceLocalTime
"2026-05-09 21:03:00"`. The analyzer selected one plugin PID (`40112`) from
`2026-05-09 21:03:04` through `2026-05-09 21:03:44`, with 12 backend renders
and 2 shared-cache summaries. The old waits are removed:
`gpu_prepare_wait_over_device=0` and `torchtrt_input_ready_wait=0`. The
remaining wait is `torchtrt_input_copy_queue_wait`, averaging about 806 ms and
peaking around 1249 ms while `torchtrt_replay_gpu` averages about 307 ms.

Outcome: ADR-0004's owned work-stream implementation is kept, but this task is
blocked for final closure because the remaining Resolve-only queue wait is not
resolved. Task `0005` owns the next diagnostic slice: CUDA Graph on/off and
main-style host-roundtrip A/B under the same Resolve settings.

## Definition of Done

All Acceptance Criteria checked, plus:

- [x] Local tests pass (or N/A documented in Notes)
- [x] Code review completed (human or fresh-context reviewer per WORKFLOW
  section 10)
- [x] No orphan `TODO`/`FIXME` introduced
- [ ] Status updated to `done` and Notes log closes the task
