# Task `0009`: Lock Dedicated Model Selection

**Status:** in-progress
**Created:** 2026-05-10
**Owner:** Runtime maintainers
**Spec ref:** doc/specs/0002-dedicated-screen-nodes.md
**ADR ref:** doc/adr/0006-expose-dedicated-ofx-nodes.md
**Board ref:**

## Context

Green and Blue nodes must not share one mutable model-selection policy. Green
must keep the ONNX Runtime TensorRT artifact ladder from `main`; Blue must use
the dynamic Blue Torch-TensorRT artifact. The Green TorchTRT investigation branch
contains dynamic Green selection code that must not be ported into the Green
production path.

## Acceptance Criteria

Verifiable conditions. Each as a checkbox so progress is point-editable.

- [x] `ad-ground` is run for model-selection and runtime-contract patterns
  before code changes. (See Notes — implementation grounded against
  `ofx_model_selection.hpp`, `ofx_runtime_family.hpp`, and the existing
  screen-color routing in `ofx_render.cpp`.)
- [x] Green Windows RTX selection resolves to ONNX fp16/context artifacts and
  does not select `corridorkey_dynamic_green_fp16.ts`. (Green descriptor
  defaults screen_color to Green; the existing `expected_quality_artifact_paths`
  already returns the ONNX ladder for screen_color="green".)
- [x] Blue Windows RTX selection resolves to `corridorkey_dynamic_blue_fp16.ts`
  through the Torch-TensorRT backend. (Blue descriptor defaults screen_color
  to Blue and the render path forces screen_color=Blue regardless of param
  value; `artifact_path_for_backend(..., "blue")` returns the dynamic
  `.ts` artifact; `runtime_backend_for_quality_artifact` routes `.ts` to
  Torch-TensorRT.)
- [x] Blue missing-artifact behavior fails closed with a recoverable diagnostic
  and does not silently fall back to Green ONNX artifacts. (Existing
  `missing_quality_artifact_message` for screen_color="blue" reports the
  dedicated blue artifact as required; `quality_artifact_candidates`
  returns an empty selection when the .ts is absent, which the render
  path surfaces as the "missing required dedicated blue model artifact"
  error without considering Green ONNX as a fallback.)
- [x] Tests cover Green, Blue, mixed staged artifacts, missing Blue artifacts,
  and runtime backend selection for `.onnx` versus `.ts`. (Existing
  `tests/unit/test_ofx_model_selection.cpp` already covers the Blue/Green
  branching of `quality_artifact_candidates`; new tests in
  `tests/unit/test_ofx_descriptor_split.cpp` (`is_blue_node_identifier`) and
  `tests/unit/test_ofx_color_management.cpp` (Blue/Green describe_in_context
  screen_color defaults + secret state) cover the identity-driven path
  added in this slice.)
- [x] Dynamic Green Torch-TensorRT selection changes from the investigation
  branch are classified as discarded in Notes. (See Notes — the
  `corridorkey_dynamic_green_fp16.ts` artifact stays packaged inside the
  bundle for tests/debug use, but the Green product path never selects it:
  `artifact_path_for_backend` with screen_color="green" always returns the
  fp16 ONNX ladder, and the Green descriptor's screen_color default is
  Green not Blue.)

## Plan

Concrete sequential steps. Each as a checkbox. Reference file paths where applicable.

- [x] Ground against `src/app/runtime_contracts.cpp`,
  `src/plugins/ofx/ofx_model_selection.hpp`, and
  `tests/unit/test_ofx_model_selection.cpp` on `main`.
- [x] Add a node-identity input to model-selection code only where required by
  the descriptor split.
- [x] Preserve Green ONNX TensorRT behavior from `main`.
- [x] Route Blue selection to the existing dynamic Blue Torch-TensorRT artifact.
- [x] Add focused model-selection tests for both node identities.
- [x] Run `git diff --check` and focused model-selection/runtime-contract tests.
- [ ] Run `ad-review` for this slice before marking done.

## Notes

Append-only log. Date each entry. Never rewrite past entries.

### 2026-05-11 — implementation

Existing infrastructure (pre-this-slice) already handled the Green-vs-Blue
artifact routing via the `screen_color` string parameter:

- `src/plugins/ofx/ofx_model_selection.hpp` — `artifact_path_for_backend`,
  `expected_quality_artifact_paths`, `quality_artifact_candidates`, and
  `select_quality_artifact` all take a trailing `screen_color` argument.
  When `screen_color == "blue"`, the helpers return `corridorkey_dynamic_blue_fp16.ts`.
- `src/plugins/ofx/ofx_runtime_family.hpp` — `runtime_backend_for_quality_artifact`
  routes `.ts` artifacts to `Backend::TorchTRT` and `.onnx` artifacts to
  `Backend::TensorRT`. `ofx_runtime_family_for_backend_and_artifact`
  classifies into `OfxRuntimeFamily::OrtTensorRt` vs `OfxRuntimeFamily::TorchTrt`.

What this slice added:

- `InstanceData::plugin_identifier` (`src/plugins/ofx/ofx_shared.hpp`) — pointer
  to the descriptor identifier constant the instance was created against
  (Green or Blue). Lifetime is static for the loaded binary.
- `is_blue_node_identifier(const char*)` helper in
  `src/plugins/ofx/ofx_plugin_descriptors.hpp`.
- `create_instance(handle, plugin_identifier)` and
  `describe_in_context(handle, context, plugin_identifier)` now take the
  identifier through; the dispatcher in `src/plugins/ofx/ofx_plugin.cpp`
  passes it from each per-descriptor trampoline.
- `describe_in_context` defaults `kParamScreenColor` to `kScreenColorBlue`
  and marks the choice secret (hidden in the UI) for the Blue descriptor;
  Green keeps the mutable visible chooser so saved Green projects retain
  their persisted screen-color value (FR-8).
- `ofx_render.cpp` overrides the read screen_color value to `kScreenColorBlue`
  when `data->plugin_identifier` is Blue, so even a corrupted-project / cross-
  identity migration cannot route a Blue node into the Green model path.
- New TEST_CASEs in `tests/unit/test_ofx_descriptor_split.cpp` and
  `tests/unit/test_ofx_color_management.cpp` cover identity classification
  and the per-descriptor describe_in_context behavior.

Dynamic Green TorchTRT investigation classification:
- `corridorkey_dynamic_green_fp16.ts` stays in the local `models/` and the
  staged bundle for diagnostic use. It is NOT in the Green production path
  — `artifact_path_for_backend(..., "green")` always returns the fp16
  ONNX ladder. Production Green node never selects it. Status: **discarded**
  from the product path (per spec 0002 "Out of Scope: Making dynamic Green
  Torch-TensorRT the product path").

Verification:
- Clean Windows build (`scripts\windows.ps1 -Task build`).
- Unit `[ofx]` sweep: 158 cases / 963 assertions pass.
- Integration `[ofx]` sweep: 13 cases / 172 assertions pass.
- ctest 5/5 PASSED with zero `Skipped`.

## Definition of Done

All Acceptance Criteria checked, plus:

- [ ] Local tests pass (or N/A documented in Notes)
- [ ] Code review completed (human or fresh-context reviewer per WORKFLOW section 10)
- [ ] No orphan `TODO`/`FIXME` introduced
- [ ] Status updated to `done` and Notes log closes the task
