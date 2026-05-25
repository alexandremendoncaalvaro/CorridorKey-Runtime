# Task `0023`: Optimize Adobe Render Hot Path

**Status:** in-progress
**Created:** 2026-05-24
**Owner:** Runtime maintainers
**Spec ref:**
**ADR ref:** doc/adr/0007-add-adobe-host-plugins.md
**Board ref:**
**Depends on:** doc/tasks/0018-implement-after-effects-effect.md

## Context

After Effects users need render performance to match or beat the Resolve/OFX
experience while preserving the chroma-key quality validated by the Jordan 4K
and magenta-background references. Runtime logs show session reuse is now
working, but the Adobe host path still has unmeasured and likely avoidable CPU
work around source copies, alpha-hint copies, output writeback, and host-side
post-runtime steps.

This task makes the Adobe render workflow measurable first, then removes the
clearly identified host-side copy divergences from the OFX implementation.

## Acceptance Criteria

- [x] Adobe render logs include host-side stage timings for source copy, alpha
      hint resolution, runtime session prepare, runtime frame processing, matte
      adjustments, foreground domain restore, and Adobe output writeback.
- [x] Adobe source and output pixel-copy loops use row-level parallelism
      equivalent to the OFX host path.
- [x] External Adobe alpha hint layers can populate the runtime alpha hint
      without allocating and copying an unnecessary full RGB runtime frame when
      the alpha channel already contains a valid hint.
- [x] Adobe timing logs default to concise per-frame summaries, with detailed
      per-stage rows available only when explicitly requested for diagnosis.
- [x] Adobe host-side linear-to-sRGB conversion uses row-level parallelism for
      source frames and visible red-channel alpha hints.
- [x] External Adobe alpha hint resolution avoids redundant post-copy scans and
      final buffer copies once a valid external hint is selected.
- [x] Focused Adobe unit tests pass.
- [x] No visual-output behavior is intentionally changed by this slice.

## Plan

- [x] Add Adobe host-side timing helpers in
      `src/plugins/adobe/adobe_effect_render.cpp`.
- [x] Parallelize Adobe input frame copy in
      `src/plugins/adobe/adobe_bridge.cpp`.
- [x] Add an alpha-hint-only external layer path in
      `src/plugins/adobe/adobe_bridge.cpp`.
- [x] Parallelize Adobe output writeback in
      `src/plugins/adobe/adobe_frame_output.cpp`.
- [x] Replace default Adobe per-stage timing rows with host/runtime summary rows
      and keep raw rows behind `CORRIDORKEY_ADOBE_VERBOSE_TIMINGS`.
- [x] Parallelize Adobe-local linear-to-sRGB conversion without changing the
      shared `src/post_process/` implementation.
- [x] Fuse Adobe external alpha-hint channel copy, optional red-channel sRGB
      conversion, and bounds detection into one channel pass.
- [x] Run focused Adobe unit tests and formatting checks.

## Notes

Append-only log. Date each entry. Never rewrite past entries.

### 2026-05-24

- Grounding decision: this slice intentionally avoids changing source
  passthrough quality, edge matte defaults, runtime resize selection, or MFR
  flags. The first optimization target is the Adobe host copy path because it
  is divergent from OFX and absent from runtime server timings.
- Implementation completed for the first measurable optimization slice. Adobe
  render logs now emit `host_stage` timing rows for host-side work and
  `runtime_stage` rows replayed from the runtime callback.
- Adobe source copy and Adobe output writeback now use row-level parallelism via
  `common::parallel_for_rows`, matching the OFX host path shape.
- External alpha hint handling now validates dimensions and probes alpha/red
  channels through 1-channel buffers instead of first materializing a full
  RGB+alpha runtime frame.
- Verification passed:
  `scripts\windows.ps1 -Task build -Preset release -EnableAdobePlugin -AdobeSdkRoot C:\Dev\CorridorKey-Runtime\vendor\adobe-after-effects-sdk`,
  `build\release\tests\unit\test_unit.exe "[unit][adobe]"`,
  `scripts\verify_ci.ps1 -Mode Format`, and `git diff --check`.
- Adobe RTX online installer generated successfully with
  `scripts\windows.ps1 -Task package-adobe -Preset release -Track rtx -Flavor online`:
  `dist\CorridorKey_Adobe_v0.8.5-win.0-68-ga0782c9-dirty-b20260525T005245828Z_Windows_RTX_online_Setup.exe`.

### 2026-05-25

- Grounding decision: the latest Adobe log showed the strongest remaining
  Adobe-host CPU opportunities in `apply_input_color_space` at about 48 ms p50
  and `resolve_alpha_hint` at about 43 ms p50, after source/output copies had
  dropped below the dominant runtime costs. The first attackable slice is
  therefore Adobe-local linear-to-sRGB conversion, not model/runtime changes.
- Adobe render diagnostics now write `runtime_request` and `alpha_hint_source`
  rows with `render_index`, plus one `host_stage_summary` and one
  `runtime_stage_summary` row per successful frame. Detailed `host_stage` and
  `runtime_stage` rows remain available by setting
  `CORRIDORKEY_ADOBE_VERBOSE_TIMINGS=1`.
- Adobe-local linear-to-sRGB conversion now uses `common::parallel_for_rows`
  with the existing immutable `SrgbLut`, covering both the full RGB source
  conversion and the red-channel visible alpha-hint conversion.
- Verification passed:
  `scripts\windows.ps1 -Task build -Preset release -EnableAdobePlugin -AdobeSdkRoot C:\Dev\CorridorKey-Runtime\vendor\adobe-after-effects-sdk`,
  `build\release\tests\unit\test_unit.exe "[unit][adobe]"`,
  `scripts\verify_ci.ps1 -Mode Format`, and `git diff --check`.
- Adobe RTX online installer generated successfully with
  `scripts\windows.ps1 -Task package-adobe -Preset release -Track rtx -Flavor online`:
  `dist\CorridorKey_Adobe_v0.8.5-win.0-69-gf1a49ae-dirty-b20260525T014929809Z_Windows_RTX_online_Setup.exe`.
- Second optimization slice targets the measured `resolve_alpha_hint` p50 of
  about 39 ms. External alpha-hint channel extraction now computes bounds while
  copying, performs red-channel linear-to-sRGB conversion in that same pass, and
  moves the selected 1-channel buffer into `frame.alpha_hint` instead of copying
  it again.
- Added regression coverage that an unreadable external Alpha Hint layer does
  not clobber a meaningful source alpha hint before fallback selection.
- Verification passed:
  `scripts\windows.ps1 -Task build -Preset release -EnableAdobePlugin -AdobeSdkRoot C:\Dev\CorridorKey-Runtime\vendor\adobe-after-effects-sdk`,
  `build\release\tests\unit\test_unit.exe "[unit][adobe]"`,
  `scripts\verify_ci.ps1 -Mode Format`, and `git diff --check`.
- Adobe RTX online installer generated successfully with
  `scripts\windows.ps1 -Task package-adobe -Preset release -Track rtx -Flavor online`:
  `dist\CorridorKey_Adobe_v0.8.5-win.0-69-gf1a49ae-dirty-b20260525T020856211Z_Windows_RTX_online_Setup.exe`.

## Definition of Done

All Acceptance Criteria checked, plus:

- [x] Local tests pass (or N/A documented in Notes)
- [ ] Code review completed (human or fresh-context reviewer per WORKFLOW section 10)
- [x] No orphan `TODO`/`FIXME` introduced
- [ ] Status updated to `done` and Notes log closes the task
