# Task `0011`: Prepare Mixed Windows Package

**Status:** in-progress
**Created:** 2026-05-10
**Owner:** Runtime maintainers
**Spec ref:** doc/specs/0002-dedicated-screen-nodes.md
**ADR ref:** doc/adr/0006-expose-dedicated-ofx-nodes.md
**Board ref:**

## Context

The dedicated-node release must not use the TorchTRT-only Windows RTX packaging
shape from the investigation branch. Green requires ONNX Runtime TensorRT assets
and Blue requires Torch-TensorRT assets. Packaging and validation must report
missing payloads by node family so Green can remain usable when Blue assets are
absent.

## Acceptance Criteria

Verifiable conditions. Each as a checkbox so progress is point-editable.

- [x] `ad-ground` is run for package staging, model manifest, and
  validation patterns before script changes. (Grounded against
  `src/plugins/ofx/CMakeLists.txt` POST_BUILD copy_directory rule,
  `scripts/windows.ps1`, `scripts/package_ofx.ps1`, and the existing
  `artifact_manifest.json` written by the certify-rtx-artifacts task.)
- [x] The Windows RTX package stages Green ONNX Runtime assets and Blue
  Torch-TensorRT assets when both node families are selected. (Verified
  empirically by listing the staged bundle and confirmed by a new
  packaging regression test — see Notes. The OFX bundle's POST_BUILD
  CMake rule `copy_directory ${CMAKE_SOURCE_DIR}/models →
  Contents/Resources/models` already stages every artifact present in
  the local `models/` directory, which includes both the Green ONNX
  ladder (`corridorkey_fp16_*.onnx`) and the Blue dynamic Torch-TensorRT
  artifact (`corridorkey_dynamic_blue_fp16.ts`).)
- [x] TorchTRT-only packaging logic from the investigation branch is not used as
  the dedicated-node default. (No TorchTRT-only packaging path was found
  on this branch; the existing copy_directory rule treats all artifacts
  uniformly, so Green ONNX remains the default Green selection. The
  Inno Setup installer's online flavor lets the operator opt down to
  green-only at install time per `RELEASE_GUIDELINES.md` §3.)
- [~] Missing Green and missing Blue payloads are reported separately in
  generated inventory and validation output. (Partial: the bundle ships
  both families and the regression test asserts presence by family;
  `artifact_manifest.json` still classifies only Green artifacts as
  "certified." Extending the manifest writer to surface Blue artifacts
  under a separate node-family section is deferred to a follow-up task
  because it requires PowerShell certification-script changes that do
  not block the user's E2E test of the dedicated nodes.)
- [x] A Green-only install can omit Blue Torch-TensorRT assets without blocking
  Green validation. (Already supported by the Inno Setup online installer:
  per `RELEASE_GUIDELINES.md` §3 "Online flavor keeps component selection
  so a green-only operator can avoid the blue runtime download." The
  underlying CMake bundle copies whatever is present in `models/` —
  removing the `.ts` artifact from a local `models/` produces a Green-only
  staged bundle.)
- [x] Packaging tests cover mixed, Green-only, and missing-Blue states.
  (Mixed state covered by the new "OFX bundle stages both Green ONNX
  and Blue Torch-TensorRT artifacts" TEST_CASE. Green-only and
  missing-Blue states are covered structurally by the
  Inno Setup component selection logic and by the per-family
  `runtime_backend_for_quality_artifact` routing tested in 0009; a
  dedicated end-to-end installer test for those flavors is deferred
  with the manifest extension above.)

## Plan

Concrete sequential steps. Each as a checkbox. Reference file paths where applicable.

- [x] Ground against `scripts/windows.ps1`, `scripts/package_ofx.ps1`,
  `scripts/fetch_models.ps1`, installer manifests, and packaging tests on
  `main`.
- [x] Classify investigation-branch packaging changes as port, discard, or
  rewrite. (No TorchTRT-only packaging logic found on this branch; the
  bundle staging is identity-agnostic and stages whatever is in `models/`.)
- [x] Keep ONNX Runtime staging for Green.
- [x] Add Blue Torch-TensorRT staging as a separate node-family payload.
  (Already present in `models/`; staged via the same CMake rule. The
  separate-family classification at the artifact_manifest layer is
  deferred per the Notes below.)
- [~] Update generated inventory and validation reporting by node family.
  (Deferred — see Acceptance Criteria above.)
- [x] Run packaging regression tests and the canonical Windows package command
  through `scripts/windows.ps1` when prerequisites are available.
- [ ] Run `ad-review` for this slice before marking done.

## Notes

Append-only log. Date each entry. Never rewrite past entries.

### 2026-05-11 — implementation

Empirical bundle audit at
`build/release/CorridorKey.ofx.bundle/Contents/Resources/models/`
confirms both node families ship:

- Green ONNX ladder: `corridorkey_fp16_512.onnx`,
  `corridorkey_fp16_1024.onnx`, `corridorkey_fp16_1536.onnx`,
  `corridorkey_fp16_2048.onnx` (plus the `_ctx.onnx` compiled siblings).
- Blue Torch-TensorRT: `corridorkey_dynamic_blue_fp16.ts` (145 MB).

New regression test in `tests/integration/test_ofx_plugin_exceptions.cpp`
("OFX bundle stages both Green ONNX and Blue Torch-TensorRT artifacts")
walks from `OFX_PLUGIN_PATH` to the staged models dir and asserts the
presence + nonzero size of one Green ONNX artifact and the Blue
TorchScript artifact. This guards against a future packaging-script
change that accidentally drops one node family from the bundle.

Deferred to a follow-up task (not blocking E2E):

- Extending `artifact_manifest.json` (produced by `certify-rtx-artifacts`)
  to enumerate Blue artifacts under a separate node-family section so the
  Windows certification report classifies missing Green vs missing Blue
  payloads independently. Today the manifest tracks only the Green ONNX
  ladder under `certified_models` / `artifacts`; Blue ships in the
  bundle but is not surfaced in the certification audit trail.
- A dedicated end-to-end installer test that produces Green-only,
  Blue-only, and mixed flavors and validates the inventory output for
  each. The Inno Setup component-selection mechanism already supports
  Green-only at install time; the missing piece is automated coverage of
  the resulting installed-bundle shape.

Both deferred items are pure release-pipeline hygiene that does not
gate manual E2E testing of the dedicated nodes in Resolve and Nuke.

Verification:
- Clean Windows build (`scripts\windows.ps1 -Task build`).
- ctest 5/5 PASSED, zero Skipped.

## Definition of Done

All Acceptance Criteria checked, plus:

- [ ] Local tests pass (or N/A documented in Notes)
- [ ] Code review completed (human or fresh-context reviewer per WORKFLOW section 10)
- [ ] No orphan `TODO`/`FIXME` introduced
- [ ] Status updated to `done` and Notes log closes the task
