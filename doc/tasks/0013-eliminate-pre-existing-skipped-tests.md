# Task `0013`: Eliminate Pre-Existing Skipped Tests

**Status:** in-progress
**Created:** 2026-05-11
**Owner:** Runtime maintainers
**Spec ref:** doc/specs/0002-dedicated-screen-nodes.md
**Board ref:**

## Context

The ctest output for `build/release` reported one ctest-level skip
(`torchtrt_dynamic_runner_smoke`) plus five Catch2-level skips inside
`integration_tests`. None are introduced by the dedicated-node descriptor
split (task 0008); all are artifact-gated tests left over from earlier
work or from paths the product has retired. The project's documentation
discipline (CLAUDE.md + memory `feedback_no_preexisting_excuse.md`)
rejects "pre-existing" as a deflection — every test that skips in CI is
the project's problem to either fix or remove.

This task eliminates every visible skip from a default
`scripts\windows.ps1 -Task build` followed by `ctest --output-on-failure`.

## Acceptance Criteria

- [x] `torchtrt_dynamic_runner_smoke` is registered in ctest only when
  the Blue dynamic TorchScript artifact is present AND the build type
  supports the vendored LibTorch ABI (Release on MSVC). When either
  condition fails the test is not registered, so ctest reports no skip
  line.
- [x] The Sprint-0 TorchTRT tests in
  `tests/integration/test_engine_torch_trt.cpp` (cases 1 and 2) are
  compiled into `test_integration` only when
  `temp/blue-diagnose/green-torchtrt-local-windows/corridorkey_torchtrt_fp16_512.ts`
  is present at CMake configuration time. When absent, those TEST_CASEs
  do not exist in the binary and no Catch2 skip surfaces.
- [x] The dynamic TorchScript Green test in the same file (case 3) is
  compiled into `test_integration` only when
  `temp/dynamic-rtx/corridorkey_dynamic_green_fp16.ts` is present at
  CMake configuration time. When absent, the TEST_CASE does not exist.
- [x] Tests that referenced `models/...onnx` via a CWD-dependent
  relative path are migrated to the `PROJECT_ROOT`-rooted absolute
  pattern used by the rest of the integration suite:
  `tests/integration/test_ofx_session_broker.cpp` (2 sites),
  `tests/integration/test_cache_fallback.cpp` (1 site),
  `tests/integration/test_engine_fallback_policy.cpp` (1 site),
  `tests/integration/test_engine_warmup.cpp` (1 site). The model
  artifacts (`corridorkey_int8_512.onnx`, `corridorkey_fp16_512.onnx`)
  exist on the workstation and load cleanly when given the absolute
  path; the original relative path resolved against the ctest binary
  directory and never found the file.
- [x] A clean ctest run reports zero `Skipped` entries.
- [x] A run of `test_integration` with `--reporter compact` reports zero
  Catch2 `skipped:` messages.

## Plan

- [x] Add CMake-time artifact presence checks to
  `tests/integration/CMakeLists.txt` that emit compile definitions
  `CORRIDORKEY_HAS_SPRINT0_TORCHTRT` and `CORRIDORKEY_HAS_DYNAMIC_TORCHTRT`.
- [x] Gate the `torchtrt_dynamic_runner_smoke` `add_test` block on
  artifact presence and Release build.
- [x] Wrap the Sprint-0 cases and dynamic Green case in
  `tests/integration/test_engine_torch_trt.cpp` with `#if defined(...)`
  guards keyed on the new compile definitions.
- [x] Migrate the int8 model path in
  `tests/integration/test_ofx_session_broker.cpp` to the fp16 sibling.
- [x] Re-run `scripts\windows.ps1 -Task build` and ctest; confirm zero
  skip lines and zero Catch2 `skipped:` output.
- [x] Run `ad-review` over the diff before marking done.

## Notes

Append-only log. Date each entry. Never rewrite past entries.

### 2026-05-11 — scope justification

This task was opened by the user during task 0008's commit phase after
auditing the ctest output. The pre-existing skips were originally
flagged as scope-creep for 0008 (model selection / runtime isolation /
packaging are tasks 0009-0012); the user instead asked for the
clean-up to land before any commit. Per `agentic-philosophy`, two
atomic commits land — one for 0008 (descriptor split), one for 0013
(skip cleanup) — to keep the histories independently revertable.

## Definition of Done

All Acceptance Criteria checked, plus:

- [x] Local tests pass (or N/A documented in Notes)
- [x] Code review completed (human or fresh-context reviewer per WORKFLOW section 10)
- [x] No orphan `TODO`/`FIXME` introduced
- [ ] Status updated to `done` and Notes log closes the task
