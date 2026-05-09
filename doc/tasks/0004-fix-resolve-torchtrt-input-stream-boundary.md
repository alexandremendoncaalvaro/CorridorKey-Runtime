# Task `0004`: Fix Resolve TorchTRT Input Stream Boundary

**Status:** in-progress
**Created:** 2026-05-09
**Owner:** Runtime maintainers
**Board ref:**

## Context

Resolve manual renders remain slower than `main` and the branch harness even
though TorchTRT model replay is not the dominant measured cost. Task `0003`
isolated the missing time at the producer and consumer boundary between GPU
input preparation and TorchTRT inference. ADR-0003 defines the selected fix:
enqueue TorchTRT input preparation on the Torch/PyTorch current CUDA stream
instead of consuming an independent GPU-prep stream through a CUDA event wait.

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
- P2: Only after P0/P1, run CUDA Graph on/off and other topology A/B tests if
  Resolve logs still show an unexplained dominant wait.

## Acceptance Criteria

Verifiable conditions. Each as a checkbox so progress is point-editable.

- [x] The TorchTRT prepared-input path can enqueue GPU input preparation on the
  Torch current CUDA stream.
- [x] NPP input-prep calls use an `NppStreamContext` bound to the stream that
  owns the TorchTRT input work.
- [x] The TorchTRT prepared-input path no longer requires
  `cudaStreamWaitEvent` on an independent GPU-prep completion event.
- [x] Public headers still do not expose CUDA, NPP, or LibTorch types.
- [x] Error handling remains `Result<T>` based for expected failures.
- [x] Source passthrough still uses device-to-device copy when the prepared
  source RGB device pointer is available.
- [ ] Resolve logs show `gpu_prepare_wait_over_device` removed from the
  prepared-input path or near zero, and the missing wait does not reappear under
  another pinned stage.
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
- [ ] Validate with Resolve logs and record the before/after evidence in Notes.
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
stream, while preserving `torchtrt_input_ready_wait`, `gpu_prepare_device`, and
`gpu_prepare_wait_over_device` telemetry.

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

## Definition of Done

All Acceptance Criteria checked, plus:

- [x] Local tests pass (or N/A documented in Notes)
- [ ] Code review completed (human or fresh-context reviewer per WORKFLOW
  section 10)
- [x] No orphan `TODO`/`FIXME` introduced
- [ ] Status updated to `done` and Notes log closes the task
