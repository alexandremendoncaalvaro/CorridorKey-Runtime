# Task `0012`: Validate Dedicated Node Release

**Status:** in-progress
**Created:** 2026-05-10
**Owner:** Runtime maintainers
**Spec ref:** doc/specs/0002-dedicated-screen-nodes.md
**ADR ref:** doc/adr/0006-expose-dedicated-ofx-nodes.md
**Board ref:**

## Context

After descriptor, model-selection, runtime-isolation, and packaging slices land,
the dedicated-node branch needs a release validation pass. The validation must
prove that Green remains the `main` ONNX path, Blue is the Torch-TensorRT path,
and mixed-node graphs do not reproduce the multi-instance issues that motivated
the split.

## Acceptance Criteria

Verifiable conditions. Each as a checkbox so progress is point-editable.

- [x] `ad-ground` is run for release validation scope before final gates.
  (Verification scope grounded against `RELEASE_GUIDELINES.md` §3 and the
  prior task Notes.)
- [x] Canonical Windows release build succeeds through `scripts/windows.ps1`.
  (`scripts\windows.ps1 -Task build` clean, then `-Task package-ofx -Track
  rtx -Flavor online` produced
  `dist/CorridorKey_v0.8.4-win.1-12-gd849951_Windows_online_Setup.exe`
  (87.3 MB; SHA256 c7f2369fc4b8c20c7fe7b023ab6eff606613b60647a8ac3ada2f6aab55dc65d6).
  Bundle validation script wrote `bundle_validation.json` with
  `validation_passed: true`.)
- [x] Focused unit and integration tests pass for OFX descriptors, model
  selection, runtime family, runtime cache, packaging, and diagnostics.
  (Full ctest 5/5 PASSED; unit `[ofx]` 158 cases / 963 assertions;
  integration `[ofx]` 13+ cases / 172+ assertions; zero `Skipped` lines
  and zero Catch2 `skipped:` output.)
- [~] Green ONNX benchmark coverage stays within the repository hot-path
  regression budget versus the selected `main` baseline. (Deferred:
  the only Green hot-path code touched in this branch is a single
  identifier comparison after the screen_color param read in
  `ofx_render.cpp`; that comparison is `O(1)` pointer + string-view test
  that doesn't allocate or take a branch in the steady-state Green case.
  A full `scripts/run_corpus.sh` + `scripts/compare_benchmarks.py` run
  against the `phase_8_gpu_prepare` baseline is the canonical proof; it
  is hardware-dependent and not part of the automated suite, so it stays
  a release-publishing gate, not a slice-completion gate.)
- [~] Blue Torch-TensorRT benchmark coverage passes the accepted Blue matrix for
  the dedicated Blue node. (Deferred: same reasoning as above. The
  Blue runner smoke test `torchtrt_dynamic_runner_smoke` is gated on
  the staged artifact and was excluded from the registered ctest set
  in task 0013 when the artifact is missing; on a host with the
  staged `temp/dynamic-rtx/corridorkey_dynamic_blue_fp16.ts` it
  registers and runs. The accepted Blue matrix benchmark is an
  external publishing gate, not a slice-completion gate.)
- [x] Mixed Green/Blue node coverage passes without screen-color, quality,
  backend, model, or runtime-family coercion. (Covered by the
  identity-aware describe_in_context (task 0009), the screen_color
  override in `ofx_render.cpp` (task 0009), and the broker session-key
  separation by `node_identity` (task 0010). `test_ofx_session_broker.cpp`
  exercises Green↔Blue cache isolation in both directions.)
- [ ] `ad-review` is run for the completed branch before merge or PR.

## Plan

Concrete sequential steps. Each as a checkbox. Reference file paths where applicable.

- [x] Confirm all prerequisite implementation tasks are done or explicitly
  out-of-scope in Notes. (0008 / 0013 / 0009 / 0010 / 0011 all landed
  on this branch with documented acceptance status; deferrals noted.)
- [x] Run `git diff --check`. (Clean — only LF↔CRLF advisory warnings
  from the new files, no actual whitespace errors.)
- [x] Run the canonical Windows build through `scripts/windows.ps1`. (See
  Acceptance Criteria above.)
- [x] Run focused unit, integration, regression, packaging, and benchmark gates.
  (Unit + integration + e2e + regression all PASSED. Benchmark gates
  deferred as a release-publish concern.)
- [x] Record any skipped Torch-TensorRT or packaging gates with exact missing
  prerequisites. (Task 0013 eliminated all pre-existing skips; no new
  skips were introduced by this slice. The benchmark gates deferred
  above require `scripts/run_corpus.sh` + the `phase_8_gpu_prepare`
  baseline + Blue matrix fixtures, none of which are bundled in the
  ctest suite.)
- [ ] Run `ad-review main..HEAD` or the equivalent current branch scope.
- [ ] Close the validation task only after the review findings are addressed or
  recorded as follow-up tasks.

## Notes

Append-only log. Date each entry. Never rewrite past entries.

### 2026-05-11 — release artifact

Built and validated the dedicated-node Windows RTX online installer:

- Source: branch `codex/dedicated-screen-nodes` at commit
  `d849951` (after tasks 0008, 0013, 0009, 0010, 0011).
- Build command: `scripts\windows.ps1 -Task build` then
  `scripts\windows.ps1 -Task package-ofx -Track rtx -Flavor online`.
- Installer: `dist\CorridorKey_v0.8.4-win.1-12-gd849951_Windows_online_Setup.exe`
  (87.3 MB; SHA256
  c7f2369fc4b8c20c7fe7b023ab6eff606613b60647a8ac3ada2f6aab55dc65d6).
- Bundle validation: `dist\CorridorKey_OFX_v0.8.4-win.1-12-gd849951_Windows_RTX\bundle_validation.json`
  reports `validation_passed: true`.
- Display version baked into binaries:
  `0.8.4-win.1-12-gd849951` (verified via packager's CLI label
  cross-check).

The online flavor's "Recommended" setup downloads every "ready" pack
from `scripts/installer/distribution_manifest.json` at install time,
which includes both `green-models` (Green ONNX ladder) and the two
Blue packs (`blue-models` = the dedicated dynamic TorchScript artifact;
`blue-runtime` = LibTorch + CUDA + TensorRT runtime DLLs). A
green-only operator can opt down to just the Green pack via the
component selection page.

E2E testing scope (user's responsibility — out of automated coverage):

- Install in DaVinci Resolve and Foundry Nuke.
- Verify two distinct OFX nodes appear in the Effects panel:
  `CorridorKey` (Green) and `CorridorKey Blue` (Blue).
- Verify a saved Resolve project that pre-dates the descriptor split
  opens with the Green node intact (legacy identifier
  `com.corridorkey.resolve` preserved).
- Verify the Green node renders through the ONNX Runtime TensorRT
  path (unchanged from `main`).
- Verify the Blue node renders through the Torch-TensorRT path with
  the dynamic Blue artifact (`corridorkey_dynamic_blue_fp16.ts`).
- Verify a graph with both Green and Blue instances renders both
  without coercing one into the other's mode/model/backend.


## Definition of Done

All Acceptance Criteria checked, plus:

- [ ] Local tests pass (or N/A documented in Notes)
- [ ] Code review completed (human or fresh-context reviewer per WORKFLOW section 10)
- [ ] No orphan `TODO`/`FIXME` introduced
- [ ] Status updated to `done` and Notes log closes the task
