# Spec `0001`: Stabilize TorchTRT Resolve Performance

**Status:** draft
**Created:** 2026-05-09
**Owner:** Runtime maintainers

## Context

CorridorKey's Windows RTX prerelease must make the dynamic TorchTRT Green path
usable in DaVinci Resolve without regressing the optimized ONNX runtime
experience on `main`. Manual Resolve renders currently show user-visible frame
times around the 1.8 second class while model-only and OFX RPC harness runs are
substantially faster. If this does not ship, the TorchTRT artifact cannot be
judged fairly because host integration, stream scheduling, post-processing, and
output transport costs remain mixed together.

The feature is a measurable Resolve performance track: the plugin panel,
runtime logs, automated harnesses, and release gates must agree on where time is
spent, and the Green TorchTRT path must remove peripheral waits that are not
raw model execution.

## User Scenarios

- **Scenario 1:** Resolve render timing is trustworthy
  - Given a user renders CorridorKey in Resolve with the RTX package installed
  - When a backend render completes or a cached frame is reused
  - Then the plugin panel and `ofx_render_summary` show the correct render
    origin and timing attribution for that frame

- **Scenario 2:** TorchTRT queue waits are attributable
  - Given the Green 2048 TorchTRT path is slower in Resolve than in the harness
  - When the runtime processes a frame
  - Then logs distinguish input preparation, CUDA stream waits, static graph
    input copy queue wait, graph replay, output copy, readback, color
    conversion, and OFX writeback

- **Scenario 3:** Automated gates protect the manual path
  - Given a code change touches the TorchTRT or OFX render hot path
  - When the release build, focused tests, and readiness matrix run
  - Then the automated path proves model validity, non-constant output coverage,
    and comparable OFX RPC timing before manual Resolve testing is requested

## Requirements

### Functional

- [ ] Resolve-visible render timing records cache origin, backend timing, and
  stage attribution without leaving stale panel values pinned to an old frame.
- [ ] TorchTRT Green 2048 logs distinguish model replay from input readiness,
  static graph input copy queue wait, output materialization, readback, and OFX
  writeback.
- [ ] The analyzer can isolate a single Resolve test window by timestamp and
  plugin process id.
- [ ] The Green TorchTRT prepared-input path preserves device-input operation
  and source-passthrough device-to-device copies where available.
- [ ] Diagnostic A/B runs can compare CUDA Graph enabled, CUDA Graph disabled,
  and a main-style host-roundtrip input boundary under the same Resolve
  settings.
- [ ] Task and ADR records identify which observed wait each change is meant to
  remove.

### Non-functional

- [ ] Render hot-path changes follow the `AGENTS.md` baseline rule for
  `phase_8_gpu_prepare` and the Windows wrapper rule for builds and packages.
- [ ] Public headers do not expose CUDA, NPP, LibTorch, or host-specific OFX
  implementation types.
- [ ] Expected failures continue to use repository `Result<T>` patterns instead
  of process exits or unhandled expected exceptions.
- [ ] Logs remain useful for manual Resolve diagnosis without requiring a
  debugger or profiler for every validation pass.

## Success Criteria

Measurable conditions. Each as a checkbox; pass/fail observable, not aspirational.

- [ ] The plugin panel "last frame render" value changes after a new backend
  Resolve render and cached frames are identifiable as cached.
- [ ] In the selected Resolve window, `gpu_prepare_wait_over_device_ms` and
  `torchtrt_input_ready_wait_ms` remain within the configured analyzer budget.
- [ ] The remaining `torchtrt_input_copy_queue_wait_ms` behavior is classified
  as CUDA Graph specific, Resolve host/context specific, or fixed by an accepted
  implementation change.
- [ ] The clean OFX RPC readiness matrix passes for Green, Blue, and Blue-Green
  2048 cases without constant-output regressions.
- [ ] Any final implementation fix has a matching task, accepted ADR when it
  changes execution topology, and focused regression or integration coverage.
- [ ] The local RTX OFX package is produced through `scripts/windows.ps1` for
  the manual Resolve validation build.

## Edge Cases

- Resolve may reuse cached frames; analysis must not treat cached frame timing
  as backend render timing.
- `ofx.log` can contain multiple plugin processes and multiple test windows;
  analyzer output must expose the selected window and process ids.
- Automated harnesses can append runtime server logs outside Resolve; real
  Resolve evidence must come from the matching plugin log window.
- Constant input can hide source passthrough, despeckle, and output-path work;
  readiness cases must include plate or random input where that matters.
- Missing TorchTRT artifacts or CUDA prerequisites must skip or fail through the
  canonical gates, not through ad hoc local scripts.
- Blue artifacts can be model-dominated while Green is queue or host dominated;
  reports must keep color modes separate.

## Out of Scope

This spec does not require replacing the Blue TorchScript/LibTorch artifact with
a true TorchTRT artifact, changing Resolve host behavior, or shipping a broad
OFX architecture rewrite. A main-style host roundtrip is allowed only as a
diagnostic comparison unless a later ADR accepts it as the product path.

## Open Questions

- Does disabling CUDA Graph in the same Resolve window remove the remaining
  static-input copy queue wait?
- Does a main-style synchronized host-roundtrip diagnostic match `main`
  behavior closely enough to isolate Resolve host/context contention?
- Does the missing `torchtrt_work_stream_guard_ms` value in current analyzer
  summaries represent an absent log field, a zero-duration measured stage, or an
  older plugin log window?
- After the queue wait is classified, which peripheral cost is next:
  post-processing, output D2H, OFX client readback, foreground conversion, or
  OFX writeback?

## Related

- ADRs: `doc/adr/0002-measure-torchtrt-stream-boundaries.md`,
  `doc/adr/0003-run-torchtrt-input-prep-on-torch-current-stream.md`,
  `doc/adr/0004-own-torchtrt-work-stream.md`
- Tasks: `doc/tasks/0002-validate-resolve-panel-timing.md`,
  `doc/tasks/0003-instrument-torchtrt-queue-wait.md`,
  `doc/tasks/0004-fix-resolve-torchtrt-input-stream-boundary.md`,
  `doc/tasks/0005-diagnose-resolve-torchtrt-graph-copy-queue-wait.md`
- Supersedes / Depends on: depends on `AGENTS.md`,
  `.planning/TORCHTRT_PIPELINE_READINESS_PLAN.md`, and
  `docs/OPTIMIZATION_MEASUREMENTS.md`
