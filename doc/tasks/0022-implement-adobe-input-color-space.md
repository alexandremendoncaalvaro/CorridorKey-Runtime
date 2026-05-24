# Task `0022`: Implement Adobe Input Color Space

**Status:** in-progress
**Created:** 2026-05-24
**Owner:** Runtime maintainers
**Spec ref:**
**ADR ref:** doc/adr/0007-add-adobe-host-plugins.md
**Board ref:**
**Depends on:** doc/tasks/0018-implement-after-effects-effect.md

## Context

After Effects users need the same explicit source interpretation control that
OFX exposes for plates that arrive as display-referred sRGB or scene-linear
Linear Rec.709 (sRGB). The Adobe bridge currently copies host pixels directly
into the runtime frame and leaves `InferenceParams::input_is_linear` at its
default value, so a linear plate cannot request the runtime's existing
linear-input path.

This task brings the manual `Input Color Space` behavior to After Effects
without claiming host-managed color support before the Adobe color callback
path is validated.

## Acceptance Criteria

- [x] The After Effects parameter setup exposes `Input Color Space` with `sRGB`
      and `Linear` choices.
- [x] The default Adobe input color-space choice maps to sRGB and keeps
      `InferenceParams::input_is_linear` false.
- [x] Selecting `Linear` maps the Adobe runtime request to
      `InferenceParams::input_is_linear` true.
- [x] Linear Adobe source frames are converted to the runtime's sRGB input
      domain before screen-color canonicalization and inference.
- [x] Adobe does not expose a `Host Managed` choice until the host color
      callback path is implemented and tested.

## Plan

- [x] Add a failing unit test for the Adobe parameter definition.
- [x] Add the `Input Color Space` popup with a stable disk ID and parameter
      slot.
- [x] Add a failing runtime-request unit test for the `Linear` choice.
- [x] Map the popup to `InferenceParams::input_is_linear`.
- [x] Add a failing bridge/render test for linear-to-sRGB source conversion.
- [x] Apply the conversion before screen-color canonicalization and runtime
      inference.
- [x] Run focused Adobe unit tests and formatting checks.

## Notes

Append-only log. Date each entry. Never rewrite past entries.

### 2026-05-24

- Grounding decision: this slice intentionally exposes only manual `sRGB` and
  `Linear`. OFX also offers `Host Managed`, but Adobe needs a validated
  `PF_ColorCallbacksSuite` or equivalent project-color-management path before
  the UI can claim host-managed behavior.
- TDD completed through three public surfaces: parameter setup via
  `EffectMain(PF_Cmd_PARAMS_SETUP)`, runtime-request mapping via
  `build_effect_runtime_request`, and bridge color conversion via
  `apply_adobe_input_color_space`.
- Verification passed:
  `scripts\windows.ps1 -Task build -Preset debug -EnableAdobePlugin -AdobeSdkRoot C:\Dev\CorridorKey-Runtime\vendor\adobe-after-effects-sdk`,
  `build\debug\tests\unit\test_unit.exe "[unit][adobe][effect]"`,
  `build\debug\tests\unit\test_unit.exe "[unit][adobe][runtime]"`,
  `build\debug\tests\unit\test_unit.exe "[unit][runtime][quality]"`,
  `ctest --test-dir build\debug -R "regression_adobe_(pipl_metadata|cmake_scaffold|package_scaffold)" --output-on-failure`,
  `scripts\verify_ci.ps1 -Mode Format`, and `git diff --check`.

## Definition of Done

All Acceptance Criteria checked, plus:

- [x] Local tests pass (or N/A documented in Notes)
- [ ] Code review completed (human or fresh-context reviewer per WORKFLOW section 10)
- [x] No orphan `TODO`/`FIXME` introduced
- [ ] Status updated to `done` and Notes log closes the task
