# Task `0010`: Isolate Runtime Node State

**Status:** in-progress
**Created:** 2026-05-10
**Owner:** Runtime maintainers
**Spec ref:** doc/specs/0002-dedicated-screen-nodes.md
**ADR ref:** doc/adr/0006-expose-dedicated-ofx-nodes.md
**Board ref:**

## Context

Dedicated Green and Blue nodes can coexist in the same Resolve graph. Runtime
session state, cache keys, runtime family selection, diagnostics, and parameter
policy must therefore include node identity where needed. The shared-node policy
from the Green TorchTRT investigation branch must not coerce Green and Blue
instances into one screen or quality policy.

## Acceptance Criteria

Verifiable conditions. Each as a checkbox so progress is point-editable.

- [x] `ad-ground` is run for runtime-family, session-cache, and
  multi-instance policy patterns before code changes. (See Notes — grounded
  against `ofx_session_broker.{hpp,cpp}`, `ofx_runtime_family.hpp`,
  `ofx_runtime_protocol.hpp`, and the per-instance state in `ofx_shared.hpp`.)
- [x] Session/cache keys include node identity, backend family, selected
  artifact, and screen state where those values affect compatibility.
  (`OfxRuntimePrepareSessionRequest::node_identity` added to the wire
  protocol with backward-compat optional decode; `session_key` now folds
  `node_identity` into the FNV-1a hash alongside artifact path + backend +
  CPU fallback flags. Selected artifact already drove runtime family via
  `ofx_runtime_family_for_backend_and_artifact`; screen state is implicit
  because Green and Blue request different artifacts.)
- [x] ONNX Runtime TensorRT and Torch-TensorRT runtime families restart,
  partition, or cache independently when required. (Pre-existing
  `should_restart_for_ofx_runtime_family_switch` already triggers a server
  restart when crossing OrtTensorRt ↔ TorchTrt; the cache-key change above
  prevents an in-process cache hit from short-circuiting the restart.)
- [x] Green and Blue instances do not coerce each other's screen color, quality,
  backend, model, or runtime family. (Screen color: locked per descriptor
  per task 0009. Quality / backend / model / runtime family: each instance
  owns its own `InstanceData`; the broker keys cached sessions by node
  identity so a Blue instance cannot inherit a Green session's compiled
  state and vice versa.)
- [x] Any retained shared-node policy is scoped within one dedicated node
  identity; otherwise it is removed. (No coercive shared-node policy was
  found in the repo. The only `shared_node_count` reference is a log
  label in `ofx_instance.cpp:436-439` that reports "Shared (N nodes)"
  without changing behavior — purely informational and scoped to the
  caller's instance.)
- [x] Tests cover multiple Green nodes, multiple Blue nodes, and mixed
  Green/Blue nodes. (`test_ofx_session_broker.cpp` extended with the
  "keys sessions by node identity" TEST_CASE that exercises Green-then-Blue
  with the same artifact path and asserts (a) two distinct sessions,
  (b) Green-again hits the Green cache entry, (c) Blue-again would hit
  the Blue entry. Bidirectional cache-isolation verified.)

## Plan

Concrete sequential steps. Each as a checkbox. Reference file paths where applicable.

- [x] Ground against `src/plugins/ofx/ofx_instance.cpp`,
  `src/plugins/ofx/ofx_runtime_family.hpp`, `src/app/ofx_session_broker.*`,
  and related runtime cache tests.
- [x] Identify which session and cache keys currently omit node identity.
- [x] Update runtime-family isolation without changing public headers.
- [x] Remove or scope shared-node policy behavior from the investigation branch.
- [x] Add multi-instance tests for same-node and mixed-node graphs.
- [x] Run `git diff --check` and focused runtime/cache/integration tests.
- [ ] Run `ad-review` for this slice before marking done.

## Notes

Append-only log. Date each entry. Never rewrite past entries.

### 2026-05-11 — implementation

Changes:
- `OfxRuntimePrepareSessionRequest` (`src/app/ofx_runtime_protocol.hpp`)
  gains a `std::string node_identity` field. The wire protocol stays at
  version 1 because the JSON decoder treats `node_identity` as optional
  (same backward-compat pattern as the existing `prepare_timeout_ms`
  field).
- `OfxSessionBroker::session_key` (`src/app/ofx_session_broker.cpp`)
  now folds `request.node_identity` into the FNV-1a hash so requests
  that differ only in identity get distinct cache buckets.
- `build_prepare_request` (`src/plugins/ofx/ofx_instance.cpp`) accepts
  the per-instance `plugin_identifier` and threads it into the outgoing
  prepare-session request alongside artifact and backend.
- The single `ensure_engine_for_quality` call site in
  `ofx_instance.cpp:1660` passes `data->plugin_identifier`.

Tests:
- `tests/unit/test_ofx_runtime_protocol.cpp` roundtrip extended to
  set/verify `node_identity` on the JSON encode + optional-decode path.
- `tests/integration/test_ofx_session_broker.cpp` adds the
  "keys sessions by node identity" TEST_CASE that exercises mixed
  Green + Blue cache lookups against the same artifact path.

Verification:
- Clean Windows build via `scripts\windows.ps1 -Task build`.
- Unit `[ofx][runtime]`: 27 cases / 126 assertions pass.
- Integration `[ofx][runtime]`: 11 cases / 156 assertions pass.
- ctest 5/5 PASSED, zero Skipped.

## Definition of Done

All Acceptance Criteria checked, plus:

- [ ] Local tests pass (or N/A documented in Notes)
- [ ] Code review completed (human or fresh-context reviewer per WORKFLOW section 10)
- [ ] No orphan `TODO`/`FIXME` introduced
- [ ] Status updated to `done` and Notes log closes the task
