# CorridorKey OFX Plugin - Delivery Plan

## Business Context

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

The current test builds already provide:

- Resolve OFX builds for macOS Apple Silicon and Windows.
- A packaged out-of-process OFX runtime path backed by the `corridorkey`
  runtime server.
- Requested versus effective quality feedback in the runtime panel.
- Visible plugin version in the panel header.
- Quality modes from `Preview (512)` through `Maximum (2048)`.
- Python-aligned tiling and output upscaling behavior.
- Source passthrough, Alpha Hint input, alpha edge controls, and output modes.
- Correct `Processed` output pipeline with linear premultiplied output.
- Explicit Alpha Hint semantics in the UI:
  - `RGBA -> A`
  - `Alpha -> single channel`
  - `RGB -> R`
- Windows packaging that includes the currently required `TensorRT` and `int8`
  artifacts.
- Windows validation that refuses to treat a bundle as truly universal when the
  required non-TensorRT provider DLLs are missing.
- Strict OFX GPU residency policy on Windows GPU paths, with explicit failure
  instead of silent success on a drifted backend.
- Structured OFX logging for bootstrap, quality switches, render stages, and
  lifecycle timing.
- Shared-frame transport, session reuse, and a packaged runtime server binary
  inside the OFX bundle/installers.

## Release Blockers

These issues still block the final Resolve goal:

- Windows RTX can still fall from GPU to CPU at `1536` or `2048`, especially on
  the first rendered frame.
- Copy/paste and first-instance latency remain too expensive because inference
  state is still owned per OFX instance.
- The current Windows universal path is not honestly AMD/Intel GPU-ready until
  the required provider DLLs and a validated non-NVIDIA execution path exist.
- The current in-process architecture still couples Resolve stability to runtime
  crashes, VRAM spikes, and warmup behavior.

## Workstream 1 - Resolve Test Baseline

Goal: keep shipping useful test builds while the final architecture is being
implemented.

- [x] **1.1 Official Packaging and Validation** -- keep official package and
  validation flows for macOS and Windows OFX builds.
- [x] **1.2 Runtime Visibility** -- expose requested quality, effective
  quality, effective backend, and loaded artifact in the OFX panel.
- [x] **1.3 Required Windows Artifacts** -- ship the currently required Windows
  RTX and non-RTX model artifacts for the existing quality ladder.
- [x] **1.4 Log-First Debugging** -- keep `ofx.log` and
  `corridorkey_ofx_delayload.log` as required evidence for test builds.
- [ ] **1.5 Current Field Validation** -- keep validating the live macOS and
  Windows test builds on real Resolve systems before each release candidate.

## Workstream 2 - Out-of-Process OFX Runtime

Goal: move Resolve execution from per-instance in-process inference to a local
runtime service. This is the primary implementation track because it resolves
copy/paste cost, first-frame warmup duplication, crash containment, and VRAM
policy together.

- [x] **2.1 Product Boundary** -- make the OFX plugin a thin local client while
  a packaged runtime process owns backend selection, model/session lifetime,
  warmup, VRAM admission, and structured diagnostics.
- [x] **2.2 Stable IPC Contract** -- define a versioned request/reply and event
  contract for registration, render submission, quality switching, cancellation,
  health checks, and diagnostics.
- [x] **2.3 High-Bandwidth Frame Transport** -- move frame payloads through
  shared memory or memory-mapped files with a lightweight local control channel.
- [x] **2.4 Session Broker and Residency Policy** -- pool sessions by backend,
  artifact, and resolution so duplicate OFX instances do not recreate full
  inference state.
- [x] **2.5 OFX Thin-Client Refactor** -- keep clip fetch, Alpha Hint
  interpretation, parameter reads, and output writes inside the plugin, but
  replace direct `Engine` ownership with calls to the local runtime process.
- [x] **2.6 Packaging and Lifecycle** -- ship the runtime server inside the
  same OFX installer and bundle layout, launch it on demand, reuse an active
  instance, and stop it after an idle timeout.
- [ ] **2.7 Crash Containment and Recovery** -- detect server crashes, hung
  renders, protocol mismatches, and startup failures, then surface deterministic
  OFX errors without requiring a Resolve restart.
- [x] **2.8 Diagnostics Parity** -- preserve the current panel/log visibility
  for backend, artifact, quality, warmup state, fallback reason, and stage
  timings when execution is out-of-process.
- [ ] **2.9 Rollout Gates** -- validate first-frame latency, copy/paste
  latency, multi-instance behavior, 4K throughput, VRAM exhaustion behavior,
  restart flow, and installer simplicity before making this the default path.
- [x] **2.10 Temporary Compatibility Path** -- keep the current in-process path
  available only as a temporary development fallback until the out-of-process
  path reaches feature parity.

## Workstream 3 - Windows GPU Reliability

Goal: keep Windows reliable enough for current testing while converging toward
the out-of-process architecture.

- [x] **3.1 Explicit GPU Failure Policy** -- disable silent CPU fallback for
  OFX GPU paths and fail explicitly on backend drift.
- [x] **3.2 CPU Guardrails** -- cap unsupported CPU interactive quality modes
  instead of letting Resolve become unusable.
- [ ] **3.3 First-Frame RTX Fallback Diagnosis** -- explain and eliminate the
  path where a correctly detected RTX card falls to CPU only after render
  begins.
- [ ] **3.4 Stable High-Resolution RTX Path** -- keep `1536` and `2048` on the
  requested GPU backend or fail explicitly without backend migration.
- [ ] **3.5 Honest Universal Claim** -- only claim Windows universal GPU
  compatibility once AMD/Intel systems can load the required provider path and
  actually execute on GPU.

## Workstream 4 - Compositing Workflow Surface

Goal: finish only the workflow features that still matter after the architecture
stabilizes.

- [ ] **4.1 Alpha Hint Product Boundary** -- decide whether rough matte
  generation belongs inside CorridorKey or stays outside the node.
- [ ] **4.2 Color Space Auto-Detection** -- add automatic linear detection only
  if it can be made reliable enough to reduce mistakes rather than create them.
- [ ] **4.3 Post-Inference Output Switching** -- keep output presentation
  changes in post-process whenever possible so users do not rerun inference for
  simple inspection changes.
- [ ] **4.4 Multiple Outputs from One Inference** -- expose foreground, matte,
  processed, and composite results from one inference result instead of forcing
  duplicate OFX nodes.
- [ ] **4.5 Panel Simplification** -- review whether brightness and saturation
  controls belong inside CorridorKey or should move to downstream color nodes.

## Workstream 5 - Deferred Until After the Runtime Architecture

Goal: keep advanced ideas visible without letting them compete with the work
that actually unlocks the final plugin.

- [ ] **5.1 Windows AI Platform and NPU Strategy** -- WinML, QNN, OpenVINO, and
  broader NPU routing.
- [ ] **5.2 DirectML Legacy Compatibility** -- pre-NPU AMD/Intel fallback path
  once the universal provider stack is real.
- [ ] **5.3 Blue Screen Support** -- channel-swapped workflow for blue screen
  footage.
- [ ] **5.4 Zero-Copy Resolve Integration** -- eliminate CPU staging where the
  host/backend combination allows it.
- [ ] **5.5 Quantization and Throughput Research** -- `INT8` and similar model
  optimization passes once architecture and diagnostics are stable.
- [ ] **5.6 Temporal Consistency** -- video-native matte stability across
  frames.

## Reference

- **Original CorridorKey:** github.com/nikopueringer/CorridorKey
- **EZ-CorridorKey:** github.com/edenaion/EZ-CorridorKey
- **CorridorKey-Engine:** github.com/99oblivius/CorridorKey-Engine
- **Archived plans:** docs/archive/PLAN_product_direction.md,
  docs/archive/PLAN_OFX_MAC_v1.md
