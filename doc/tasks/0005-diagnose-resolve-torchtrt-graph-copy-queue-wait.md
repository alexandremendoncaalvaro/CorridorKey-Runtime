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

This task owns the next diagnostic slice. It must separate a CUDA Graph
static-input-copy interaction from Resolve host/context contention before any
broader post-process or OFX writeback optimization is attempted.

## Acceptance Criteria

Verifiable conditions. Each as a checkbox so progress is point-editable.

- [ ] Resolve log analysis uses a single-PID, time-filtered window and records
  `work_origin` counts so cached frames cannot hide backend-render behavior.
- [ ] `ofx_render_summary` and the analyzer distinguish a missing
  `torchtrt_work_stream_guard_ms` field from a real zero-duration stage.
- [ ] A diagnostic CUDA Graph disabled run is measured in Resolve with the same
  Green 2048 settings and source-passthrough parameters as the failing window.
- [ ] A diagnostic main-style host-roundtrip input path is measured in Resolve
  with the same settings, without replacing the primary device-input path.
- [ ] The OFX RPC harness still runs the comparable Green 2048 case so the
  Resolve-only gap is visible beside the automated path.
- [ ] The outcome identifies whether the remaining wait is CUDA Graph specific,
  host/context specific, or still unattributed.
- [ ] Any selected implementation fix is captured in a follow-up ADR before code
  changes if it changes execution topology.

## Plan

Concrete sequential steps. Each as a checkbox. Reference file paths where
applicable.

- [ ] Update `scripts/analyze_resolve_ofx_logs.ps1` if needed so missing stage
  fields are reported separately from numeric zero.
- [ ] Verify `src/plugins/ofx/ofx_render.cpp` emits
  `torchtrt_work_stream_guard_ms` in the installed package used for Resolve
  testing.
- [ ] Add a diagnostic-only way to run the TorchTRT Resolve path with CUDA Graph
  disabled while preserving all other settings.
- [ ] Add a diagnostic-only way to run the TorchTRT Resolve path with a
  main-style synchronized host-roundtrip input boundary.
- [ ] Build and package through `scripts/windows.ps1`.
- [ ] Run the clean OFX RPC harness case for Green 2048 processed/source
  passthrough.
- [ ] Run the Resolve manual A/B windows and capture them with
  `scripts/analyze_resolve_ofx_logs.ps1 -SinceLocalTime`.
- [ ] Record the comparison in Notes and either close this task or open the
  implementation ADR/task for the selected fix.

## Notes

### 2026-05-09

Task opened from the post-ADR-0004 Resolve validation. The accepted owned work
stream remains useful because it removed the previous readiness waits in both
the harness and the real Resolve window. The unresolved evidence is now narrower:
the static-input copy queue wait dominates only in Resolve, while the same
stage is small in the clean OFX RPC harness.

## Definition of Done

All Acceptance Criteria checked, plus:

- [ ] Local tests pass (or N/A documented in Notes)
- [ ] Code review completed (human or fresh-context reviewer per WORKFLOW
  section 10)
- [ ] No orphan `TODO`/`FIXME` introduced
- [ ] Status updated to `done` and Notes log closes the task
