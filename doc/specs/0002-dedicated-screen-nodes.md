# Dedicated Screen Nodes

**Status:** accepted
**Owner:** Runtime maintainers
**Created:** 2026-05-10

## Context

CorridorKey needs a release path that lets users choose the stable Green workflow and the accelerated Blue workflow without backend auto-selection changing behavior between node instances. The Green implementation on `main` uses the ONNX Runtime TensorRT path and remains the compatibility path for existing Resolve projects. The Blue implementation on this branch is the viable Torch-TensorRT path and should be exposed as its own node instead of sharing the legacy node's runtime selection surface.

The branch investigation showed that treating Green and Blue as one mutable node creates product risk. A single OFX descriptor currently exposes the legacy identifier `com.corridorkey.resolve`, and saved Resolve projects depend on that identifier. This branch also added dynamic Green Torch-TensorRT selection and shared-node policy behavior that can coerce settings across live node instances. Those changes solve some runtime conflicts by reducing variability, but they undermine the goal of allowing users to place multiple independent nodes in a graph.

The product direction is to expose dedicated nodes:

- Green keeps the legacy OFX identity and uses the ONNX Runtime TensorRT core as on `main`.
- Blue uses a new OFX identity and the Torch-TensorRT core with the dynamic Blue artifact.
- Improvements from this branch are ported only when they improve the dedicated-node architecture without requiring dynamic Green Torch-TensorRT as the product path.

## User Scenarios

### Existing Projects Load As Green

Given a saved Resolve project that contains the existing CorridorKey node, when the updated OFX plugin is installed, then the project opens with the legacy plugin identifier unchanged and renders through the Green ONNX Runtime TensorRT path.

### New Green Node Uses ONNX

Given a user adds the Green node on a Windows RTX install, when the node resolves its model and backend, then it selects the ONNX fp16/context artifact ladder and does not require the dynamic Green Torch-TensorRT artifact.

### New Blue Node Uses Torch-TensorRT

Given a user adds the Blue node on a Windows RTX install with the Blue dynamic Torch-TensorRT artifact present, when the node resolves its model and backend, then it selects `corridorkey_dynamic_blue_fp16.ts` and runs through the Torch-TensorRT runtime family.

### Mixed Nodes Remain Independent

Given a Resolve graph contains multiple Green and Blue node instances, when the host renders them in sequence or concurrently, then session selection, cache keys, runtime process state, and parameter policy do not coerce screen color, quality, backend, or model selection across node identities.

### Partial Installs Are Recoverable

Given a package has the Green ONNX runtime and models but is missing the Blue Torch-TensorRT runtime or model, when the user renders a Green node, then Green still works. When the user renders a Blue node, the node reports a clear missing-runtime or missing-model state without crashing the host.

## Functional Requirements

1. The OFX bundle MUST expose two user-visible plugin descriptors: the legacy Green descriptor and a new Blue descriptor.
2. The legacy Green descriptor MUST keep the existing identifier `com.corridorkey.resolve` to preserve saved Resolve projects.
3. The Blue descriptor MUST use a new stable reverse-DNS identifier and a distinct label so Resolve can persist it independently.
4. Green node instances MUST default to Green screen behavior and resolve Windows RTX artifacts through the ONNX Runtime TensorRT path used on `main`.
5. Green node instances MUST NOT select `corridorkey_dynamic_green_fp16.ts` in the production Windows RTX path.
6. Blue node instances MUST default to Blue screen behavior and resolve Windows RTX artifacts through the Torch-TensorRT path using `corridorkey_dynamic_blue_fp16.ts`.
7. Blue node instances MUST NOT silently fall back to Green ONNX artifacts when the Blue Torch-TensorRT artifact or runtime is missing.
8. The screen-color parameter surface MUST prevent accidental cross-node mode changes. This may be implemented by hiding, disabling, or removing mutable screen selection from dedicated nodes while preserving compatibility for saved Green projects.
9. Session cache keys MUST include enough node identity, backend family, artifact, and screen state to prevent Green and Blue sessions from sharing incompatible runtime state.
10. Runtime family handling MUST keep ONNX Runtime TensorRT and Torch-TensorRT process state isolated whenever sharing would make backend switches unsafe.
11. Shared-node policy from this branch MUST be removed, disabled, or scoped per dedicated node identity so Green and Blue instances cannot force each other into one screen or quality policy.
12. Packaging MUST be able to stage a mixed Windows RTX package containing the Green ONNX Runtime assets and the Blue Torch-TensorRT assets.
13. Packaging validation MUST report missing Green and Blue payloads by node family so partial installs are actionable.
14. Doctor output, runtime diagnostics, panel state, and logs MUST identify node identity, backend family, selected artifact, and missing dependency state.
15. Branch improvements MUST be classified before porting. Improvements that depend on dynamic Green Torch-TensorRT MUST NOT be ported into the Green production path.

## Non-Functional Requirements

1. Existing projects that use the legacy OFX identifier MUST remain loadable without manual node replacement.
2. Missing Torch-TensorRT dependencies MUST NOT prevent the Green node from loading or rendering when Green dependencies are present.
3. Green ONNX performance MUST stay within the render-hot-path regression budget documented for this repository.
4. Blue Torch-TensorRT performance MUST be validated against the latest accepted Blue Torch-TensorRT benchmark matrix before release.
5. The implementation MUST keep public headers free of external runtime types and MUST keep backend-specific code inside `src/`.
6. The implementation MUST not add new top-level directories or new `src/` subdirectories unless the architecture document is updated in the same change.

## Operational Requirements

1. Planning and implementation MUST continue to use the repository `agentic` workflow.
2. Before code is written for each non-trivial implementation slice, the slice MUST run through `agentic-ground` and record the grounding outcome in the relevant task Notes.
3. Work MUST be decomposed through `agentic-task` into checkbox-tracked task files under `doc/tasks/`.
4. A binding architecture or product-identity decision, including the Blue OFX identifier or bundle strategy, MUST be captured through `agentic-adr` before implementation depends on it.
5. Each implementation task MUST run `agentic-review` before its status is changed to `done`.
6. Branch improvements from the archived Green TorchTRT investigation MUST be classified in an `agentic` task before porting, with each item marked as port, discard, or rewrite.
7. The new implementation branch MUST carry the repository `agentic` tooling and the accepted planning artifacts needed to resume this work from that branch.

## Implementation Plan

1. Start from `main` so the legacy Green node inherits the ONNX Runtime TensorRT behavior and saved-project compatibility.
2. Use the Green TorchTRT investigation branch only as a source for explicitly classified ports.
3. Port the `agentic` tooling and directly relevant specs/tasks into the new branch before implementation changes.
4. Open an `agentic-task` for branch setup and branch-diff classification before implementation changes.
5. Resolve the Blue node identity and bundle strategy through `agentic-adr` if the answer is not already accepted in task context.
6. Implement the node split, model selection, runtime isolation, packaging, and validation as separate `agentic-task` slices.
7. Run `agentic-review` against each completed slice before merging.

## Success Criteria

1. `OfxGetNumberOfPlugins` exposes both dedicated descriptors, and tests assert the legacy Green identifier remains `com.corridorkey.resolve`.
2. Unit tests prove Green Windows RTX model selection resolves to ONNX artifacts and Blue Windows RTX model selection resolves to `corridorkey_dynamic_blue_fp16.ts`.
3. Mixed Green and Blue OFX instance tests pass without shared screen-color, quality, backend, or model coercion.
4. Runtime family tests prove ONNX Runtime TensorRT and Torch-TensorRT sessions restart, partition, or cache independently when required.
5. Windows packaging validation passes for a full mixed package and reports clear node-family errors for missing Green or Blue payloads.
6. A Green-only install renders Green successfully even when Blue Torch-TensorRT assets are absent.
7. A Blue node with missing Torch-TensorRT assets fails with a recoverable user-facing diagnostic.
8. The canonical Windows release build succeeds through `scripts/windows.ps1 -Task build`.
9. Focused unit, integration, packaging, and benchmark checks pass for the files touched by the node split.
10. Relevant task files show `agentic-ground` evidence, accepted ADR references where needed, and fresh-context review completion before closure.

## Edge Cases

- Resolve may cache plugin descriptors by identifier, so the legacy Green identifier cannot be renamed.
- Saved Green projects may contain historical screen-color parameter values that no longer appear in the dedicated-node UI.
- Users may copy and paste nodes between projects with different installed payload sets.
- A Resolve timeline may contain multiple Green nodes, multiple Blue nodes, or both node types in one render pass.
- The Torch-TensorRT runtime may be absent while ONNX Runtime TensorRT is installed.
- The Blue dynamic artifact may be absent while Green ONNX artifacts are installed.
- Non-RTX Windows, DML, Linux, and macOS builds may not support the Blue Torch-TensorRT node; unsupported states must be explicit and recoverable.

## Out of Scope

- Making dynamic Green Torch-TensorRT the product path.
- Continuing the NPP foreground output-pack probe after its acceptance criteria were falsified.
- Training or publishing new model artifacts.
- Reworking the public C++ API for screen selection.
- Creating separate installer products unless an ADR chooses that packaging model.

## Open Questions

1. What exact identifier and label should the Blue OFX descriptor use?
2. Should the product ship one OFX bundle exposing two descriptors, or separate Green and Blue bundles?
3. Should the legacy screen-color parameter remain visible on Green for compatibility, or become hidden once saved projects migrate?
4. Which branch improvements are mandatory for the first dedicated-node task batch: runtime family isolation, shared-frame transport, diagnostics, packaging validation, or all of them?
5. Should installer defaults include both node families, or install Green by default and Blue as an optional component?
6. Should the current shared-node policy be deleted entirely or retained only within one dedicated node identity?

## Related

- Supersedes: `doc/specs/0001-torchtrt-resolve-performance.md`
- `doc/adr/0006-expose-dedicated-ofx-nodes.md`
- `doc/tasks/0006-npp-torchtrt-output-pack.md`
- `doc/tasks/0007-plan-dedicated-node-branch.md`
- `doc/tasks/0008-implement-ofx-descriptor-split.md`
- `doc/tasks/0009-lock-dedicated-model-selection.md`
- `doc/tasks/0010-isolate-runtime-node-state.md`
- `doc/tasks/0011-prepare-mixed-windows-package.md`
- `doc/tasks/0012-validate-dedicated-node-release.md`
- `.agents/skills/agentic-ground/SKILL.md`
- `.agents/skills/agentic-task/SKILL.md`
- `.agents/skills/agentic-adr/SKILL.md`
- `.agents/skills/agentic-review/SKILL.md`
- `src/plugins/ofx/ofx_plugin.cpp`
- `src/plugins/ofx/ofx_shared.hpp`
- `src/plugins/ofx/ofx_model_selection.hpp`
- `src/app/host_plugin_runtime_family.hpp`
- `src/app/runtime_contracts.cpp`
- `scripts/package_ofx.ps1`
- `scripts/fetch_models.ps1`
