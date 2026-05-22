# Task `0019`: Validate Premiere Compatibility

**Status:** proposed
**Created:** 2026-05-22
**Owner:** Runtime maintainers
**Spec ref:**
**ADR ref:** doc/adr/0007-add-adobe-host-plugins.md
**Board ref:**
**Depends on:** doc/tasks/0018-implement-after-effects-effect.md

## Context

The Premiere Pro C++ SDK guide strongly recommends using the After Effects SDK
for effect plugins, and Adobe's After Effects sample list marks Skeleton as
Premiere Pro compatible. That makes the After Effects effect the first
implementation path for Premiere rather than a separate legacy Video Filter SDK
plugin.

Premiere is not identical to After Effects. The After Effects guide for
Premiere hosts states that Premiere does not support After Effects 16-bit
rendering or SmartFX, and 32-bit rendering in Premiere requires declaring
Premiere pixel-format support and implementing 32-bit rendering through
`PF_Cmd_RENDER`. Premiere-specific compatibility therefore needs its own task
and validation gate.

This task owns Premiere host compatibility for the Adobe effect. It must not
switch to Premiere's legacy Video Filter SDK unless a later grounding pass and
ADR/task explicitly justify that deviation.

## Acceptance Criteria

Verifiable conditions. Each as a checkbox so progress is point-editable.

- [ ] The Adobe effect installs to a MediaCore scan path and appears in
      Premiere Pro's effects UI with the stable CorridorKey effect identity.
- [ ] Premiere rendering does not depend on SmartFX selectors.
- [ ] `PF_Cmd_RENDER` handles Premiere's supported 8-bit and 32-bit pixel
      formats for the initial support target, or rejects unsupported formats
      with a visible error and test coverage.
- [ ] Pixel aspect ratio, field/interlaced input, and alpha handling are either
      implemented or explicitly rejected with user-visible host messages and
      regression tests.
- [ ] Premiere-specific behavior is gated behind host detection or pixel-format
      checks and does not change the After Effects render path.
- [ ] A Premiere smoke imports a clip, applies CorridorKey, renders or exports
      a short range through the runtime service, and closes the project without
      a host crash.
- [ ] The task records the tested Premiere version, OS, GPU track, input format,
      render settings, and output artifact in Notes.

## Plan

Concrete sequential steps. Each as a checkbox. Reference file paths where applicable.

- [ ] Add Premiere host detection and capability logging to the Adobe plugin
      layer.
- [ ] Add Premiere pixel-format registration and render-path handling where the
      SDK requires it.
- [ ] Add focused tests for Premiere-specific format decisions and unsupported
      cases.
- [ ] Install the effect to a Premiere-visible MediaCore location for local
      smoke testing.
- [ ] Run the Premiere smoke and record host/version/render evidence in Notes.
- [ ] Compare After Effects smoke behavior after Premiere changes to confirm no
      AE regression.

## Notes

Append-only log. Date each entry. Never rewrite past entries.

### 2026-05-22

Grounding highlights for Premiere:

- Premiere Pro "Video Filters" says Adobe strongly recommends the After Effects
  SDK for effects plugins and that future effect development is based on the
  After Effects API.
- After Effects "Sample Projects" marks Skeleton and several other effects as
  Premiere Pro compatible and documents the Adobe Common Plug-ins `7.0`
  MediaCore development path.
- After Effects "Bigger Differences" says Premiere does not support After
  Effects 16-bit rendering or SmartFX, and that Premiere 32-bit rendering must
  be implemented through `PF_Cmd_RENDER` with Premiere pixel-format support.
- Premiere Pro "Selector Table" is the main rejected alternative for this task:
  the legacy Video Filter path centers on `fsExecute`, but the official
  Premiere guidance sends effect plugins toward the After Effects SDK first.

## Definition of Done

All Acceptance Criteria checked, plus:

- [ ] Local tests pass (or N/A documented in Notes)
- [ ] Code review completed (human or fresh-context reviewer per WORKFLOW section 10)
- [ ] No orphan `TODO`/`FIXME` introduced
- [ ] Status updated to `done` and Notes log closes the task
