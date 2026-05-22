# Task `0015`: Align Adobe Plugin Scope

**Status:** in-progress
**Created:** 2026-05-22
**Owner:** Runtime maintainers
**Spec ref:**
**ADR ref:** doc/adr/0007-add-adobe-host-plugins.md
**Board ref:**

## Context

CorridorKey now has a proposed architecture decision to add native Adobe host
plugins for After Effects and Premiere, but the current product and support
documents still say Adobe hosts are outside the supported scope. The code cannot
add new plugin directories or ship Adobe-facing artifacts while the canonical
scope documents contradict the decision.

The grounding pass points to one initial host strategy: build an Adobe effect
plugin on the After Effects SDK and validate it in both After Effects and
Premiere. Adobe's Premiere Pro C++ SDK guide says effect plugins should use the
After Effects SDK, while the After Effects SDK sample-project guide identifies
Skeleton as the starting point for effects and marks it Premiere Pro compatible.
The same docs also warn that Premiere has important pixel-format differences,
so Premiere compatibility must be tracked separately from the basic After
Effects plugin.

This task owns only the documentation and architecture alignment needed before
code lands. It must not implement the plugin, SDK detection, runtime bridge, or
installer work.

## Acceptance Criteria

Verifiable conditions. Each as a checkbox so progress is point-editable.

- [x] ADR-0007 is accepted or superseded before `docs/SPEC.md`,
      `ARCHITECTURE.md`, or `help/SUPPORT_MATRIX.md` claim Adobe plugin work is
      in implementation scope.
- [x] `docs/SPEC.md` no longer contradicts ADR-0007: Adobe After Effects and
      Premiere are either recorded as the active implementation scope or remain
      explicitly out of supported release scope until validation evidence exists.
- [x] `ARCHITECTURE.md` defines the Adobe plugin directory or directories before
      any new `src/plugins/...` Adobe source files are added.
- [x] `ARCHITECTURE.md` preserves the Library First and Interface Segregation
      rules: Adobe plugin code is an Interface-layer adapter over App/Core
      contracts, not a runtime fork.
- [x] `help/SUPPORT_MATRIX.md` names the Adobe host support designation and does
      not claim official support until After Effects and Premiere validation
      tasks pass.
- [x] The task decomposition for Adobe work remains linked from this task or
      from the accepted ADR so future implementers do not rediscover the same
      scope decision.

## Plan

Concrete sequential steps. Each as a checkbox. Reference file paths where applicable.

- [x] Get human review on `doc/adr/0007-add-adobe-host-plugins.md` and update
      this task if the decision is superseded.
- [x] Update `docs/SPEC.md` so Adobe host work is no longer described as an
      untouched non-goal once the ADR is accepted.
- [x] Update `ARCHITECTURE.md` with the chosen Adobe plugin directory layout,
      dependency rules, and target ownership.
- [x] Update `help/SUPPORT_MATRIX.md` with the initial Adobe host support
      designation and validation gate.
- [x] Run `git diff --check`.
- [ ] Run fresh-context review before marking this task done.

## Notes

Append-only log. Date each entry. Never rewrite past entries.

### 2026-05-22

Grounding citations consulted before opening this task:

- Official docs: After Effects C++ SDK Guide, "How To Start Creating Plug-ins",
  "Sample Projects", "PiPL Resources", "Command Selectors", "SmartFX", and
  "Premiere Pro and Other Hosts / Bigger Differences"; Premiere Pro C++ SDK
  Guide, "Video Filters" and "Selector Table".
- Implementation references: `Wunkolo/Vulkanator` uses a CMake `MODULE` target,
  PiPL resource generation, and a single Adobe effect entry point dispatching
  selectors; `bryful/F-s-PluginsProjects/_Skeleton` keeps PiPL metadata,
  effect match name, `.aex` output, and selector dispatch in a production-style
  After Effects effect project.
- In-repo patterns: `ARCHITECTURE.md` defines interfaces as thin clients over
  App/Core; `docs/SPEC.md` and `help/SUPPORT_MATRIX.md` currently mark Adobe
  hosts out of scope; `src/plugins/ofx` is the only current host-plugin
  implementation.
- Git history: commits `005dda1`, `4817b56`, `6f3f309`, `a73dc4d`, `64415c7`,
  and `bcec35d` show prior product-surface, host-specific, descriptor, and
  runtime-isolation work. Searches for Adobe, Premiere, After Effects, and
  MediaCore found documentation references but no prior Adobe plugin
  implementation attempt.

### 2026-05-22

Implementation started after user approval to begin development. ADR-0007 was
accepted and added to `ARCHITECTURE.md` Active ADRs. `ARCHITECTURE.md`,
`docs/SPEC.md`, and `help/SUPPORT_MATRIX.md` now define Adobe After Effects
and Premiere as accepted implementation targets while keeping their public
support designation `Unsupported` until build, host validation, and packaging
tasks pass.

Task chain:

- `doc/tasks/0016-add-adobe-sdk-build-scaffold.md`
- `doc/tasks/0017-build-adobe-runtime-bridge.md`
- `doc/tasks/0018-implement-after-effects-effect.md`
- `doc/tasks/0019-validate-premiere-compatibility.md`
- `doc/tasks/0020-package-adobe-plugins.md`

`git diff --check` passed with only line-ending normalization warnings from
tracked files.

## Definition of Done

All Acceptance Criteria checked, plus:

- [ ] Local tests pass (or N/A documented in Notes)
- [ ] Code review completed (human or fresh-context reviewer per WORKFLOW section 10)
- [ ] No orphan `TODO`/`FIXME` introduced
- [ ] Status updated to `done` and Notes log closes the task
