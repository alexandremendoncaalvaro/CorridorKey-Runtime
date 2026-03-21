# CorridorKey OFX Plugin - Delivery Plan

## Business Context & Document Purpose

> **Documentation Exception:** Unlike other documents which strictly forbidden historical planning or tracking, `PLAN.md` serves explicitly as the **implementation baseline and checklist**. Its purpose is to preserve context for active and upcoming workstreams so developers can follow established conclusions without re-litigating them.

The Resolve OFX plugin is the primary delivery surface for CorridorKey. The
goal is a production-ready plugin on macOS Apple Silicon and Windows with
one-step installation, predictable quality selection, stable GPU execution, and
diagnostics that make remote tester feedback actionable.

The plan is organized to avoid duplicated work. Engineering time should favor
the architecture that solves the largest number of real user problems at once,
not incremental improvements that will be rewritten later.

## Final Objective

CorridorKey should ship as a thin OFX client backed by a packaged local runtime
service that:

- isolates backend, VRAM, and session failures from Resolve;
- reuses model warmup and inference state across OFX instances;
- keeps diagnostics, quality selection, and packaging consistent across macOS
  Apple Silicon and Windows;
- preserves a one-step installer for end users.

## Planning Rules

- Prefer work that directly contributes to the final out-of-process OFX
  architecture.
- Allow tactical in-process fixes only when they unblock current testing or are
  directly reusable in the final architecture.
- Defer non-critical panel polish and secondary workflow features until the
  runtime architecture is stable.
- Keep the official packaging and validation flows. Do not replace them with
  ad-hoc installers or manual deployment paths.
- Reuse the shared runtime contracts, model catalog, and JSON/NDJSON vocabulary
  where they already fit. Avoid inventing parallel protocol or packaging
  surfaces.

## Current Baseline

*Note: The vast majority of the foundational Out-of-Process architecture and Windows/Mac packaging has been completed. See `docs/archive/PLAN_OFX_v2.md` for historical completions.*

The current test builds already provide:
- Resolve OFX builds for macOS Apple Silicon and Windows.
- A packaged out-of-process OFX runtime path backed by the `corridorkey` runtime server.
- Deferred runtime bootstrap for out-of-process OFX instances.
- Quality modes from `Preview (512)` through `Maximum (2048)`.
- Explicit Alpha Hint semantics and output modes.
- Windows packaging for `TensorRT` and official `DirectML` runtimes.
- Strict OFX GPU residency policy and explicit failures instead of silent CPU fallbacks.
- Structured OFX logging, shared-frame transport, and auto-recovery.

## Release Blockers

These validation steps and missing features still block the final Resolve goal:

- **Field Validation (1.7):** The project now has an official Windows `DirectML`
  package path, but a single Windows bundle should not be called universal
  until AMD/Intel test systems validate the non-NVIDIA path in real sessions.
- **Rollout Gates (2.9):** Copy/paste and first-instance latency need another
  field-validation pass after deferred bootstrap; we still need Resolve-side
  confirmation that duplication is now acceptably fast.
- **Preview Throughput (3.6):** `Preview (512)` now has a lower-allocation
  single-frame path, but still needs Resolve-side validation that the
  steady-state staging cost is low enough.

## Active Workstreams

### Workstream 1 - Resolve Test Baseline

Goal: keep shipping useful test builds while the final architecture is being implemented.

- [ ] **1.7 Current Field Validation** -- keep validating the live macOS and
  Windows test builds on real Resolve systems before each release candidate.

### Workstream 2 - Out-of-Process OFX Runtime

Goal: move Resolve execution from per-instance in-process inference to a local runtime service.

- [ ] **2.9 Rollout Gates** -- validate first-frame latency, copy/paste
  latency after deferred bootstrap, multi-instance behavior, 4K throughput,
  VRAM exhaustion behavior, restart flow, and installer simplicity before
  making this the default path.

### Workstream 3 - Windows GPU Reliability

Goal: keep Windows reliable enough for current testing while converging toward the out-of-process architecture.

- [ ] **3.6 Steady-State Preview Throughput** -- validate and, if needed,
  further reduce the remaining CPU-side cost in the `512` render path. A
  single-frame fast path is now in place, but Resolve-side responsiveness still
  needs field validation.

### Workstream 4 - Deferred Until After the Runtime Architecture

Goal: keep advanced ideas visible without letting them compete with the work that actually unlocks the final plugin.

- [ ] **4.1 Windows AI Platform and NPU Strategy** -- WinML, QNN, OpenVINO, and
  broader NPU routing.
- [ ] **4.2 DirectML Legacy Compatibility** -- pre-NPU AMD/Intel fallback path
  once the universal provider stack is real.
- [ ] **4.3 Zero-Copy Resolve Integration** -- eliminate CPU staging where the
  host/backend combination allows it.

## Reference

- **Original CorridorKey:** github.com/nikopueringer/CorridorKey
- **EZ-CorridorKey:** github.com/edenaion/EZ-CorridorKey
- **CorridorKey-Engine:** github.com/99oblivius/CorridorKey-Engine
- **Archived plans:** docs/archive/PLAN_product_direction.md, docs/archive/PLAN_OFX_MAC_v1.md, docs/archive/PLAN_OFX_v2.md
