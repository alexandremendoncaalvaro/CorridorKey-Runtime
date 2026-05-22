# Task `0020`: Package Adobe Plugins

**Status:** proposed
**Created:** 2026-05-22
**Owner:** Runtime maintainers
**Spec ref:**
**ADR ref:** doc/adr/0007-add-adobe-host-plugins.md
**Board ref:**
**Depends on:** doc/tasks/0018-implement-after-effects-effect.md, doc/tasks/0019-validate-premiere-compatibility.md

## Context

After the Adobe effect builds and passes host smoke tests, users still need a
repeatable way to install it with the same runtime payload, model inventory,
diagnostics, and validation discipline as the existing product surfaces. The
repository rules require Windows build, package, certification, and release
work to go through `scripts/windows.ps1`, not through internal delegate scripts.

The package must stage the Adobe `.aex` effect, the out-of-process runtime
service, required app-local runtime libraries, and model artifacts without
turning missing optional models into silent success. The support matrix must not
claim a support level beyond the validation evidence produced by this task.

This task owns packaging and certification. It must not add new render behavior
or change Adobe parameter semantics.

## Acceptance Criteria

Verifiable conditions. Each as a checkbox so progress is point-editable.

- [ ] `scripts/windows.ps1` exposes a canonical Adobe packaging or release task,
      and no Adobe packaging workflow requires calling an internal delegate
      script directly.
- [ ] The Windows Adobe package stages the `.aex` effect into the Adobe Common
      Plug-ins MediaCore path or into an installer payload that targets that
      path.
- [ ] The package stages the runtime service, app-local dependencies, model
      inventory, validation reports, and diagnostics expected by the Adobe
      bridge.
- [ ] Generated inventory and validation reports identify the Adobe plugin
      binary, PiPL/effect identity, runtime payload, and packaged model state.
- [ ] Missing packaged models are surfaced as reportable package state, while
      invalid packaged models still block the package flow.
- [ ] Clean install and upgrade install both leave After Effects and Premiere
      able to discover the effect after host restart.
- [ ] The support matrix, README, and user help are updated only to the support
      designation justified by the completed host validation evidence.

## Plan

Concrete sequential steps. Each as a checkbox. Reference file paths where applicable.

- [ ] Extend `scripts/windows.ps1` with the canonical Adobe packaging entrypoint
      after agreeing the task name and parameters.
- [ ] Add installer or package staging for the Adobe plugin module, runtime
      service, app-local dependencies, and model packs.
- [ ] Extend package inventory and validation reporting for Adobe artifacts.
- [ ] Add clean-install and upgrade-install smoke checks for Adobe host plugin
      discovery.
- [ ] Run the canonical Windows package task and record artifact paths, hashes,
      and validation reports in Notes.
- [ ] Update user-facing docs after the package and host validation evidence
      exists.
- [ ] Run fresh-context review before marking this task done.

## Notes

Append-only log. Date each entry. Never rewrite past entries.

### 2026-05-22

Grounding highlights for packaging:

- `AGENTS.md` requires Windows build, package, certification, and release flows
  to run through `scripts/windows.ps1`.
- After Effects "Sample Projects" documents the Adobe Common Plug-ins `7.0`
  MediaCore development path for Adobe effect discovery.
- Existing package behavior in `src/plugins/ofx/CMakeLists.txt` stages the OFX
  bundle and runtime payload, while `docs/SPEC.md:120-132` requires the
  host-plugin runtime to stay out of process and versioned.
- `help/SUPPORT_MATRIX.md` currently says Adobe hosts are unsupported; this
  task must update that only after host validation creates evidence.

## Definition of Done

All Acceptance Criteria checked, plus:

- [ ] Local tests pass (or N/A documented in Notes)
- [ ] Code review completed (human or fresh-context reviewer per WORKFLOW section 10)
- [ ] No orphan `TODO`/`FIXME` introduced
- [ ] Status updated to `done` and Notes log closes the task
