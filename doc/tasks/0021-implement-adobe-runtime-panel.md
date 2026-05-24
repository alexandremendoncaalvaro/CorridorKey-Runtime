# Task `0021`: Implement Adobe Runtime Panel

**Status:** in_progress
**Created:** 2026-05-23
**Owner:** Runtime maintainers
**Spec ref:**
**ADR ref:** doc/adr/0007-add-adobe-host-plugins.md
**Board ref:**
**Depends on:** doc/tasks/0018-implement-after-effects-effect.md

## Context

After Effects users need the same operational confidence that OFX hosts expose:
which runtime path was used, whether the Alpha Hint layer or fallback guide was
used, the effective quality, and the last-frame timing. Standard Adobe effect
parameters are not a safe live telemetry surface because render callbacks and
UI-update selectors have strict mutation rules. The Adobe runtime panel must use
host-supported UI mechanisms rather than writing parameter values from the render
path.

This task owns the After Effects runtime telemetry surface. It does not change
inference, packaging, or Premiere validation behavior.

## Acceptance Criteria

- [ ] The After Effects Effect Controls panel exposes the CorridorKey display
      version, runtime status, guide source, effective quality, and last-frame
      timing.
- [ ] Last-frame timing includes a derived processing FPS label based on measured
      wall time, not the composition frame rate.
- [ ] Composition timing, when shown, is labeled separately from processing FPS
      and is derived from `PF_InData::time_step` and `PF_InData::time_scale`.
- [ ] The render path records telemetry without mutating Adobe parameters from a
      render callback.
- [ ] UI refresh uses Adobe-supported custom ECW/event or UI-update mechanisms
      and does not checkout layer pixels during `PF_Cmd_UPDATE_PARAMS_UI`.
- [ ] Unit coverage verifies telemetry formatting, state lifetime, and selector
      handling through public `EffectMain` calls.
- [ ] A local After Effects smoke verifies the panel after applying the effect,
      rendering a frame, changing the Alpha Hint layer, and reopening the
      project.

## Plan

- [ ] Implement a small telemetry state owned by Adobe sequence or instance
      lifetime rather than global static state.
- [ ] Share or mirror the OFX runtime label formatting for status, guide source,
      last-frame time, average time, and derived FPS.
- [ ] Add the minimal Adobe custom ECW UI parameter needed to draw runtime
      telemetry in the Effect Controls panel.
- [ ] Handle `PF_Cmd_EVENT` for the custom control and keep `PF_OutFlag_CUSTOM_UI`
      aligned between PiPL and `PF_Cmd_GLOBAL_SETUP`.
- [ ] Add tests before each behavior slice and run Adobe-enabled debug build,
      focused unit tests, and PiPL scaffold regression.
- [ ] Run a manual After Effects smoke and record the host version and observed
      panel labels in Notes.

## Notes

Append-only log. Date each entry. Never rewrite past entries.

### 2026-05-23

- Grounding decision: do not add a fake FPS slider or try to write standard
  parameter values from render. Adobe documentation states custom Effect
  Controls Window UI should set `PF_OutFlag_CUSTOM_UI`, use a parameter with
  `PF_PUI_CONTROL`, and handle `PF_Cmd_EVENT`; parameter value mutation is
  honored only during `PF_Cmd_USER_CHANGED_PARAM` and specific event handling.
- Adobe SDK headers define `current_time / time_step` as the current frame
  number and `time_step / time_scale` as composition timing. That is distinct
  from CorridorKey processing FPS, which must come from measured render wall
  time already captured by the OFX runtime panel path.
- In-repo OFX precedent: `runtime_timings_runtime_label` reports last-frame and
  average render durations, while the render path records wall time separately
  from host frame timing. The Adobe panel should reuse the same semantics and
  add derived processing FPS as a label.

## Definition of Done

All Acceptance Criteria checked, plus:

- [ ] Local tests pass (or N/A documented in Notes)
- [ ] Code review completed (human or fresh-context reviewer per WORKFLOW section 10)
- [ ] No orphan `TODO`/`FIXME` introduced
- [ ] Status updated to `done` and Notes log closes the task
