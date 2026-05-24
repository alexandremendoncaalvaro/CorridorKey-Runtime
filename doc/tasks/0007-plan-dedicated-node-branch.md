# Task `0007`: Plan Dedicated Node Branch

**Status:** done
**Created:** 2026-05-10
**Owner:** Runtime maintainers
**Spec ref:** doc/specs/0002-dedicated-screen-nodes.md
**Board ref:**

## Context

The dedicated-node direction is accepted. Before implementation starts, the
team needs a new branch plan that starts from `main`, keeps Green on the ONNX
Runtime TensorRT path, treats the Green TorchTRT investigation branch as a
source for selective ports, and keeps the repository `agentic` workflow as the
planning and review mechanism.

The new branch must also carry the `agentic` tooling and the planning artifacts
that are directly needed to resume this work there. Without that bootstrap, the
branch would have the right code base but lose the decision trail that explains
why Green stays ONNX and Blue becomes the Torch-TensorRT node.

## Acceptance Criteria

Verifiable conditions. Each as a checkbox so progress is point-editable.

- [x] The new implementation branch base is recorded with evidence from `main`
  and the Green TorchTRT investigation branch.
- [x] `ad-ground` is run for the branch-base and port-classification
  decision before code changes begin.
- [x] Candidate improvements from the archived branch are classified as port,
  discard, or rewrite.
- [x] The `agentic` tooling is present on the new branch, including the
  repository `.agents/skills/ad-*` skills and the managed `AGENTS.md`
  skill entries.
- [x] Directly relevant planning artifacts are present on the new branch,
  including `doc/specs/0002-dedicated-screen-nodes.md` and this task.
- [x] Any unresolved Blue OFX identifier or bundle-strategy decision is captured
  through `ad-adr` before implementation depends on it.
- [x] Follow-up implementation work is split into `ad-task` slices for OFX
  descriptors, model selection, runtime isolation, packaging, and validation.
- [x] The task plan records that each implementation slice requires
  `ad-review` before being marked `done`.

## Plan

Concrete sequential steps. Each as a checkbox. Reference file paths where applicable.

- [x] Run `ad-ground` for the branch-base decision, using `main`, the
  current branch diff, in-repo OFX/runtime patterns, and git history.
- [x] Create or switch to the implementation branch from `main` after the base
  decision is recorded.
- [x] Port or reinstall the repository `agentic` tooling on the implementation
  branch before code changes.
- [x] Port `doc/specs/0002-dedicated-screen-nodes.md` and this task to the
  implementation branch, plus any archived spec/task evidence that the
  port-classification table references directly.
- [x] Build a port-classification table for branch changes touching
  `src/plugins/ofx/`, `src/app/`, `src/core/`, `scripts/`, `tests/`, and model
  packaging metadata.
- [x] Mark dynamic Green TorchTRT selection, TorchTRT-only packaging, and
  cross-color shared-node coercion as discarded unless a later accepted ADR
  reopens them.
- [x] Identify reusable changes for Blue Torch-TensorRT and mixed packaging,
  then open separate `ad-task` files for each implementation slice.
- [x] Run or prepare `ad-adr` for the Blue OFX identifier and one-bundle
  versus two-bundle decision if task context does not settle them.
- [x] Record the required `ad-review` gate in every follow-up task's
  Definition of Done before implementation begins.

## Notes

Append-only log. Date each entry. Never rewrite past entries.

### 2026-05-10

Prepared branch `codex/dedicated-screen-nodes` from `origin/main` at
`de419ea`. The Green TorchTRT investigation branch used for classification was
`codex/torchtrt-dynamic-windows-rtx` at `b7cd0a5`. The investigation branch
diff against `origin/main` spans 185 files, so the new branch intentionally
starts from `main` and ports only planning/tooling now.

`agentic-ground` record for branch-base and port classification:

- Scope: prepare the dedicated-node implementation branch without code changes.
- Source A, project workflow: `AGENTS.md` requires feature branches, canonical
  Windows entrypoint usage, and preserving repository structure; `.agents`
  skills define `agentic-ground`, `agentic-task`, `agentic-adr`, and
  `agentic-review` as the planning and review workflow.
- Source B, external examples: not consulted because this is a branch
  preparation and planning-artifact migration, not an implementation pattern or
  library choice. The deviation is recorded here and must not be reused for code
  changes.
- Source C, in-repo product shape: `src/plugins/ofx/ofx_plugin.cpp` on `main`
  exposes one OFX descriptor; `src/plugins/ofx/ofx_shared.hpp` on `main` keeps
  `com.corridorkey.resolve` because changing it would orphan saved Resolve
  projects.
- Source C, model selection: the investigation diff changes
  `src/app/runtime_contracts.cpp` and `src/plugins/ofx/ofx_model_selection.hpp`
  to introduce `corridorkey_dynamic_green_fp16.ts`; this conflicts with the
  accepted Green ONNX direction.
- Source C, packaging: the investigation diff changes `scripts/package_ofx.ps1`
  toward TorchTRT-only Windows RTX packaging; the dedicated-node release needs a
  mixed Green ONNX and Blue Torch-TensorRT package.
- Source C, multi-instance policy: the investigation branch adds
  `tests/unit/test_ofx_shared_node_policy.cpp` and shared-node policy code that
  can coerce screen color and quality across instances; dedicated nodes must not
  coerce Green and Blue together.
- Source D, git history: `origin/main` at `de419ea` is the branch base and
  `codex/torchtrt-dynamic-windows-rtx` at `b7cd0a5` is the source branch for
  selective ports.

Happy path: start from `origin/main`, port the `agentic` tooling and planning
artifacts, keep Green behavior from `main`, and use the investigation branch
only as a classified source for Blue Torch-TensorRT and mixed-package work.

Port classification:

| Area | Action | Notes |
| --- | --- | --- |
| `.agents/**` and managed `AGENTS.md` entries | port now | Required to resume with `agentic` in the clean session. |
| `doc/specs/0002-dedicated-screen-nodes.md` | port now | Accepted product direction for Green ONNX and Blue Torch-TensorRT. |
| `doc/specs/0001-torchtrt-resolve-performance.md` and tasks `0002`-`0006` | port as archive evidence | Keeps the rejected Green TorchTRT path and NPP probe closed. |
| `doc/tasks/0008`-`0012` | port now | Follow-up slices for implementation, packaging, and validation. |
| `src/plugins/ofx/ofx_plugin.cpp` descriptor split | rewrite | Main has one descriptor; implementation must add two descriptors under task `0008`. |
| `src/app/runtime_contracts.cpp` dynamic Green changes | discard | Green must keep ONNX Runtime TensorRT from `main`. |
| `src/plugins/ofx/ofx_model_selection.hpp` dynamic Green changes | discard | Blue may use dynamic `.ts`; Green must not select `corridorkey_dynamic_green_fp16.ts`. |
| `src/plugins/ofx/ofx_runtime_family.hpp` and session-broker isolation | port or adapt under task `0010` | Useful only after grounding against `main`; must include node identity. |
| Shared-node policy changes | discard or rewrite under task `0010` | Cross-color coercion conflicts with dedicated node independence. |
| TorchTRT-only Windows RTX packaging | discard | Dedicated package must be mixed or node-family aware. |
| Packaging validation and inventory reporting improvements | rewrite under task `0011` | Keep the reporting idea, not the TorchTRT-only default. |
| Runtime diagnostics and analyzer improvements | port selectively | Useful when they identify node identity and backend family. |
| NPP foreground output-pack probe | discard | Task `0006` measured and rejected it. |

Created ADR-0006 as the proposed architecture checkpoint for one OFX bundle with
two descriptors. It must be accepted or replaced before task `0008` implements
descriptor code. Created follow-up tasks `0008` through `0012`, each with an
explicit `agentic-ground` step and `agentic-review` gate.

## Definition of Done

All Acceptance Criteria checked, plus:

- [x] Local tests pass (or N/A documented in Notes)
- [x] Code review N/A because this task only prepares planning artifacts; every
  implementation slice requires `agentic-review`
- [x] No orphan `TODO`/`FIXME` introduced
- [x] Status updated to `done` and Notes log closes the task
