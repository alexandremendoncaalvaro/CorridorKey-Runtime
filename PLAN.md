# CorridorKey OFX Plugin - Development Plan

## Business Context

The OFX plugin is the primary delivery surface for CorridorKey. The first
functional macOS Apple Silicon build is live (resolve-v0.1.4-mac), with basic
keying, despill, despeckle, and refiner controls. The immediate goal is to
close the quality gap with the community Python app (EZ-CorridorKey) and
establish the plugin as a production-ready tool inside DaVinci Resolve.

## Current State

**Available in the current test builds:**
- Functional OFX plugin for DaVinci Resolve on macOS Apple Silicon and Windows
- Runtime panel that exposes the effective backend, device, requested quality,
  effective quality, and loaded artifact
- Visible plugin version in the OFX panel header to confirm the installed build
- Quality modes exposed as Auto, Preview (512), Standard (768), High (1024),
  Ultra (1536), and Maximum (2048)
- Lanczos4 output upscaling for maximum edge detail
- Source passthrough controls for restoring original source detail in opaque
  regions
- Alpha Hint external matte input
- Output Mode selector (Processed, Matte Only, Foreground Only, Source+Matte)
- Alpha Edge Controls (Erode/Dilate, Softness, Black/White Point)
- Color Correction (Brightness, Saturation)
- Tiling behavior aligned with the Python reference, including external frame
  edge weighting and overlap semantics
- Correct color space pipeline (sRGB in, linear premultiplied out, sRGB
  conversion in write)
- Official Windows OFX packaging now includes the `fp16 512` RTX artifact and
  the `int8 768/1024` non-RTX artifacts required by the current quality
  selection logic
- Windows OFX validation now fails a "Universal" bundle that lacks the actual
  non-TensorRT execution provider DLLs needed for AMD/Intel GPU acceleration
- Windows OFX GPU backends now disable silent CPU fallback during session
  creation and render execution; backend drift is treated as an explicit OFX
  failure instead of a hidden success path
- Windows OFX logging now captures structured `session_create`, `warmup`,
  `first_frame`, and `subsequent_frame` timing/fallback events to support real
  Resolve diagnostics on user machines

**Known gaps confirmed by Resolve field testing:**
- Reliable GPU residency is still the top release requirement. On real Windows
  RTX hardware, the plugin can detect the correct GPU and still rebuild or fall
  to CPU when rendering the first frame at `1536` or `2048`.
- The current branch now exposes requested and effective quality separately,
  but that improved runtime feedback still needs fresh Resolve validation on
  user machines.
- The current branch packages a valid TensorRT `512` artifact and updates the
  runtime panel correctly, but this needs confirmation from the next Windows
  test round on real Resolve systems.
- High-resolution Windows RTX switches (`1536` and `2048`) can rebuild onto the
  CPU path on real hardware and remain a release blocker until the effective
  backend remains stable. The current branch now fails explicitly instead of
  silently continuing on CPU, but the root cause still needs fixing.
- The current Windows universal bundle is still not AMD-ready. Field logs show
  Radeon systems falling back to `Windows CPU Baseline`, and the vendored
  Windows runtime still does not contain the DirectML/WinML/OpenVINO provider
  DLLs needed for real non-NVIDIA GPU execution.
- Alpha Hint semantics are still under-explained in the UI: RGBA inputs use the
  alpha channel, single-channel inputs use that channel directly, and RGB-only
  hint expectations are currently mismatched with user intuition.
- The OFX node still generates a rough matte internally when no Alpha Hint is
  connected. If the product direction is to keep matte generation outside the
  node, that fallback behavior needs a deliberate decision.
- Copy/paste and first-instance latency still feel too heavy in Resolve and
  likely correlate with per-instance engine creation and warmup.

## Phase 1 - Quality Parity with EZ-CorridorKey

Goal: match or exceed the visual quality of the Python standalone app.

- [x] **1.1 Source Detail Restoration** -- blend original source pixels back
  into opaque interior regions using chamfer distance transform. Avoids green
  spill contamination with per-pixel check. Smooth blend ramp at edges.
  (`src/post_process/restore_source.cpp`)
- [x] **1.2 Despeckle Size Threshold** -- expose existing engine parameter as
  integer slider (50-2000px, default 400) in OFX panel. Replaces binary toggle.
- [x] **1.3 Refiner Scale Range** -- extended from [0.5, 2.0] to [0.0, 3.0].
  Setting 0.0 disables the CNN edge refiner entirely.
- [x] **1.4 Despill Algorithm** -- verified: already matches EZ-CorridorKey
  (green limit = (R+B)/2, redistribute spill to R and B).

## Phase 1.5 - Core Workflow (community-driven, high priority)

Features requested by the community and critical for real-world compositing.

- [x] **1.5.1 Alpha Hint Input Clip** -- add a second optional OFX input clip
  ("Alpha Hint") so users can feed an external matte from another Fusion node
  (keyer, roto, painted mask) instead of the auto-generated rough matte.
  If not connected, falls back to `generate_rough_matte` as today.
- [ ] **1.5.2 Color Space Auto-Detection** -- OFX 1.5 colour management
  extensions exist but DaVinci Resolve does not implement them. Possible
  heuristic: detect linear footage by checking if float pixel values
  exceed 1.0. For now, the "Input Is Linear" checkbox remains the
  reliable manual fallback.
- [x] **1.5.3 Alpha Hint UX Clarification** -- make the OFX UI explicit about
  how hint inputs are interpreted: RGBA uses `A`, single-channel inputs use the
  provided channel directly, and RGB-only hint expectations must either be
  documented or handled with an explicit fallback policy.
- [ ] **1.5.4 Alpha Hint Product Boundary** -- decide whether the OFX node
  should keep generating an internal rough matte when no hint is connected, or
  whether hint generation belongs strictly outside the node so CorridorKey stays
  focused on keying.
- [x] **1.5.5 Alpha Hint RGB Fallback Policy** -- the Alpha Hint clip now
  accepts RGB in addition to RGBA and Alpha, and RGB hints explicitly fall back
  to channel `R` instead of being host-dependent or under-documented.

## Phase 2 - Resolution and Quality Control

Goal: proper 4K support and user control over quality/speed tradeoff.

Research finding (2026-03-15): All reference implementations (original
CorridorKey, EZ-CorridorKey, CorridorKey-Engine) process at 2048x2048 on
CUDA and 1024x1024 on MPS/Apple Silicon. No repo does native-resolution
inference. All downscale to model resolution, process, then Lanczos4
upscale. Tiling is only applied to the CNN refiner (never the backbone),
with 512px tiles and 128px overlap. Our 1024 bridge already matches what
EZ-CorridorKey achieves on Apple Silicon.

- [x] **2.1 Quality Mode Parameter** -- choice parameter exposing inference
  resolution: Auto (default), Preview (512), Standard (768), High (1024).
  Auto selects based on input resolution. Changing quality recreates the
  engine with the appropriate MLX bridge or ONNX model. The MLX safetensors
  pack supports dynamic bridge compilation up to 2048px.
- [x] **2.2 Smart Resolution Selection** -- Auto mode selects the optimal
  MLX bridge based on input size (<=1000px: 512, <=2000px: 768, >2000px:
  1024). Single-pass inference with no tiling overhead. Engine recreated
  only when target resolution changes.
- [x] **2.3 Lanczos4 Output Upscaling** -- replaced bilinear upscaling of
  inference results with separable Lanczos4 interpolation when resizing
  back to source resolution. All reference repos use Lanczos4 for this
  step. Bilinear retained for downscaling in `fit_pad` where it is
  appropriate. (`src/post_process/color_utils.cpp`)
- [x] **2.4 Effective Quality Feedback** -- expose the requested quality and the
  effective loaded artifact/resolution distinctly in the OFX panel so users can
  trust Auto mode and understand when a fixed/manual switch fails.
- [x] **2.5 CPU Guardrails for Quality Selection** -- block or downgrade
  high-resolution manual quality requests when the effective backend is CPU so
  Resolve does not become unusable on accidental backend fallback.

## Phase 3 - Output Control and Workflow

Goal: give compositors the control they expect from a professional keyer.

- [x] **3.1 Output Mode Selector** -- choice parameter: Processed (default),
  Matte Only, Foreground Only, Source + Matte.
- [x] **3.2 Alpha Edge Controls** -- Erode/Dilate (-10 to +10 px), Edge
  Softness (0-5 px), Black/White Point (0-1 each). Post-inference alpha
  manipulation, no re-run needed.
- [x] **3.3 Color Correction** -- Brightness (0.5-2.0) and Saturation (0-2.0)
  controls applied to foreground in linear space after inference. Uses
  Rec. 709 luminance weights for saturation. (`src/plugins/ofx/ofx_render.cpp`)
- [x] **3.4 Color Pipeline Audit** -- fixed double-gamma bug in all output
  modes when alpha edge controls or color correction were active. The
  foreground (sRGB from model) was being premultiplied without linearization,
  then `write_output_image` applied `to_srgb()` again, causing gray banding
  at transparency edges. Now all output paths convert FG to linear before
  premultiplication, matching the reference repos exactly.
- [x] **3.5 Processed Output Presentation** -- clarify the expected appearance
  of the `Processed` output in Resolve so users are not surprised by viewing
  linear premultiplied RGB directly without a display transform.
- [ ] **3.6 Panel Simplification** -- review whether Brightness and Saturation
  belong inside CorridorKey or should be removed in favor of downstream color
  nodes.
- [ ] **3.7 Multiple Simultaneous Outputs** -- support emitting foreground,
  matte, processed, and composite outputs from the same node without forcing
  users to duplicate inference-heavy nodes just to inspect another result.
- [ ] **3.8 Post-Inference Output Switching** -- keep output selection changes
  in the post-process stage whenever possible so changing presentation does not
  rerun inference.

## Phase 4 - Platform Parity (NVIDIA RTX)

Goal: ensure the Windows version matches macOS quality and performance.

- [x] **4.1 TensorRT-RTX Integration** -- fully implemented with FP16 support.
- [x] **4.2 FP16 Input/Output Conversion** -- hardware-accelerated half-precision
  handling for TensorRT engines.
- [x] **4.3 MSVC Compilation & Vendor ORT** -- stabilized build pipeline with
  Visual Studio 2022 and managed ONNX Runtime dependencies.
- [x] **4.4 Portable Installer** -- relative path installation script for
  professional distribution.
- [x] **4.5 Preview (512) on Windows RTX** -- package and select a valid `512`
  TensorRT artifact so the Preview mode actually switches engine state instead
  of failing and leaving stale runtime information behind.
- [x] **4.6 Strict OFX GPU Residency Policy** -- disable silent CPU fallback
  for OFX GPU backends, disable ORT CPU EP fallback for those paths, and fail
  explicitly if render execution drifts away from the requested GPU backend.
- [ ] **4.7 Stable High-Resolution RTX Path** -- investigate why `1536` and
  `2048` can recreate the engine onto the CPU backend on real RTX systems, then
  either keep those resolutions on GPU or fail explicitly without an implicit
  backend downgrade.
- [ ] **4.8 First-Frame GPU Fallback Diagnostics** -- capture and explain the
  case where a correctly detected RTX card falls to CPU only after the first
  render, not during bootstrap. Structured OFX logging is now in place, but
  diagnosis on real user logs is still pending.
- [ ] **4.9 Copy/Paste Instance Cost** -- reduce the cold-start and copy/paste
  cost of OFX instances so duplicating the node does not stall Resolve for
  tens of seconds.

## Phase 5 - Universal Windows AI (AMD, Intel, Qualcomm)

Goal: target the March 2026 Windows AI stack for maximum compatibility.

- [x] **5.1 Windows Universal Packaging Audit** -- make the official Windows
  OFX package ship and validate the actual non-TensorRT execution-provider
  stack required for AMD/Intel compatibility instead of assuming `DirectML.dll`
  alone is sufficient.
- [ ] **5.2 DirectML Compatibility Baseline** -- guarantee that AMD Radeon
  systems can detect a GPU backend, load the correct provider path, and run
  inference without falling immediately to `Windows CPU Baseline`.
- [x] **5.3 Non-RTX Artifact Pack** -- include the `int8` artifacts required by
  the non-TensorRT Windows path (`512`, `768`, and `1024` at minimum) so Auto
  and manual quality selection are not pinned to the CPU-safe `512` fallback.
- [ ] **5.4 Windows AI Platform (WinML) Backend** -- migrate from manual
  backend selection to the Windows ML orchestrator. This allows the OS to
  automatically route to the NPU on Copilot+ PCs or the GPU on AMD/Intel.
- [ ] **5.5 NPU-First Strategy (QNN & OpenVINO 2026.0)** -- optimize models for
  the Snapdragon X2 Elite (Qualcomm QNN) and Intel Core Ultra (OpenVINO)
  NPUs to offload the GPU during video rendering.
- [ ] **5.6 DirectML Fallback (Legacy Compatibility)** -- maintain DirectML for
  pre-2024 AMD/NVIDIA hardware where NPUs are unavailable.
- [ ] **5.7 Blue Screen Support** -- channel-swap approach (swap B and G
  channels before inference, swap back after). Requires adapting despill
  and rough matte generation to work on the blue channel. No model
  retraining needed.

## Phase 6 - Performance & Workflow Refinement

- [ ] **6.1 GPU/NPU Zero-Copy Pipeline** -- implement direct memory link between
  the AI backend and DaVinci Resolve's GPU buffers to eliminate CPU staging.
- [ ] **6.2 INT8 Quantization** -- utilize 2026-era quantization tools (Olive)
  to achieve 2x speedup on NPUs with minimal quality loss.
- [ ] **6.3 Temporal Consistency (Video-Native)** -- frame-to-frame matte
  consistency to reduce flickering.
- [ ] **6.4 2048px Windows Stability** -- keep the Windows `2048` path usable on
  supported hardware without degrading to CPU or making Resolve effectively
  unusable during interactive work.
- [ ] **6.5 Instance Reuse and Warmup Strategy** -- investigate whether engine
  caches, warmup reuse, or shared artifact state can reduce the heavy cost of
  first placement and copy/paste in Resolve.
- [ ] **6.6 Log-Guided Resolve Validation** -- keep using `ofx.log` and
  `corridorkey_ofx_delayload.log` as required evidence for Windows test builds,
  especially for backend fallback and provider-loading failures.

## Phase 7 - Out-of-Process Runtime for OFX

Goal: move Resolve OFX execution from per-instance in-process inference to a
packaged local runtime service that isolates backend and VRAM failures, reuses
warmup and session state across instances, and preserves one-step installation.

- [ ] **7.1 Product Boundary** -- make the OFX plugin a thin local client while
  a packaged runtime server owns backend selection, model/session lifetime,
  warmup, VRAM admission, and structured diagnostics. Scope stays strictly
  local-machine; this is not a remote or distributed service feature.
- [ ] **7.2 Stable IPC Contract** -- define a versioned request/reply and event
  contract for instance registration, render submission, quality switching,
  cancellation, health checks, and diagnostics. Reuse the existing JSON/NDJSON
  runtime vocabulary where it already fits instead of inventing a second
  incompatible protocol surface.
- [ ] **7.3 High-Bandwidth Frame Transport** -- move frame payloads through
  shared memory or memory-mapped files with a lightweight local control channel
  (`Named Pipe` on Windows, `Unix Domain Socket` on macOS). Do not serialize
  full-resolution frame buffers through text transport.
- [ ] **7.4 Session Broker and Residency Policy** -- keep pooled runtime
  sessions keyed by backend, artifact, and resolution so first-frame warmup and
  copy/paste duplication no longer recreate full inference state per OFX
  instance. The broker must enforce explicit VRAM budgeting and refuse or
  downgrade work before Resolve reaches unsafe memory pressure.
- [ ] **7.5 OFX Thin-Client Refactor** -- keep clip fetch, alpha-hint
  interpretation, OFX parameter reads, and output image writes inside the
  plugin, but replace direct `Engine` ownership with request/response calls to
  the local runtime process.
- [ ] **7.6 Packaging and Lifecycle** -- ship the runtime server executable
  inside the same OFX installer and bundle layout, discover it relative to the
  plugin at runtime, launch on demand, reuse an already-running instance, and
  stop after an idle timeout. No extra installer, service registration, or
  admin-only setup is allowed.
- [ ] **7.7 Crash Containment and Recovery** -- detect server crashes, hung
  renders, protocol mismatches, and startup failures, then surface deterministic
  OFX errors while allowing the runtime process to be restarted without
  restarting Resolve.
- [ ] **7.8 Diagnostics Parity** -- preserve and extend the current runtime
  panel and log evidence so the user can still see requested backend, effective
  backend, artifact, quality, warmup state, fallback reason, and stage timings
  when execution is out-of-process.
- [ ] **7.9 Rollout Gates** -- before making this the default architecture,
  validate first-frame latency, copy/paste latency, multi-instance behavior, 4K
  throughput, VRAM exhaustion behavior, server restart flow, and installer
  simplicity on both macOS and Windows Resolve builds.
- [ ] **7.10 Temporary Compatibility Path** -- keep the current in-process
  architecture available as a temporary development fallback until the
  out-of-process path reaches feature parity, then explicitly decide whether to
  remove it or quarantine it behind a debug-only switch.

## Reference

- **Original CorridorKey:** github.com/nikopueringer/CorridorKey (creator's
  repo, GreenFormer model)
- **EZ-CorridorKey:** github.com/edenaion/EZ-CorridorKey (Python standalone,
  same AI model, community-driven)
- **CorridorKey-Engine:** github.com/99oblivius/CorridorKey-Engine (Python
  engine with optimizations, FlashAttention, deferred DMA)
- **Archived plans:** docs/archive/PLAN_product_direction.md,
  docs/archive/PLAN_OFX_MAC_v1.md
