# Task `0002`: Validate Resolve Panel Timing

**Status:** in-progress
**Created:** 2026-05-09
**Owner:** Runtime maintainers
**Board ref:**

## Context

Resolve shows a stuck "last frame render" value around 1.8 seconds in the plugin
panel after recent changes. Current logs show `update_runtime_panel_values`
performing `flush=full` after `end_sequence_render`, so the task is to validate
whether the panel regression is fixed, still stale in Resolve UI, or caused by a
different lifecycle path.

## Acceptance Criteria

Verifiable conditions. Each as a checkbox so progress is point-editable.

- [x] Resolve logs show a full runtime-panel flush after every completed render
  sequence that updates render timing.
- [ ] The plugin panel "last frame render" value changes after a new manual
  Resolve render instead of staying pinned to the previous slow frame.
- [x] Foundry Nuke keeps deferred panel behavior where required by the existing
  host lifecycle contract.
- [x] Regression coverage includes Resolve end-sequence flush and Nuke deferred
  behavior.
- [x] A local Windows OFX package is produced through `scripts/windows.ps1`
  after the fix is validated.

## Plan

Concrete sequential steps. Each as a checkbox. Reference file paths where
applicable.

- [x] Inspect current Resolve logs in `%LOCALAPPDATA%/CorridorKey/Logs` for
  `end_sequence_render`, `update_runtime_panel_values`, and
  `ofx_render_summary`.
- [x] Verify or adjust lifecycle flushing in
  `src/plugins/ofx/ofx_instance.cpp` and `src/plugins/ofx/ofx_render.cpp`.
- [x] Verify host-specific tests in `tests/unit/test_ofx_lifecycle.cpp`.
- [x] Run `scripts/windows.ps1 -Task build -Preset release`.
- [x] Run relevant unit, regression, and integration tests.
- [x] Run `scripts/windows.ps1 -Task package-ofx -Preset release -Track rtx
  -Flavor online`.
- [ ] Record final Resolve log evidence in Notes.

## Notes

### 2026-05-09

Current logs show `update_runtime_panel_values enter flush=full` followed by
`exit ok` after `end_sequence_render` in Resolve. The local build, unit,
regression, integration, and OFX package steps passed before the queue-wait
instrumentation work began. Manual Resolve panel confirmation is still open.

The instrumented local installer was produced through `scripts/windows.ps1` at
`dist/CorridorKey_v0.8.5-win.1-49-g1d07bb2-dirty-w1268cb106f3f_Windows_online_Setup.exe`.
SHA256: `834d14f6e580f5577e25e29f82a07898c4e2974820e99c6bc6f119cb6b4c96b8`.

## Definition of Done

All Acceptance Criteria checked, plus:

- [ ] Local tests pass (or N/A documented in Notes)
- [ ] Code review completed (human or fresh-context reviewer per WORKFLOW
  section 10)
- [ ] No orphan `TODO`/`FIXME` introduced
- [ ] Status updated to `done` and Notes log closes the task
