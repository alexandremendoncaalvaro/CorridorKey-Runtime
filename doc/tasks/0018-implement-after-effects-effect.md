# Task `0018`: Implement After Effects Effect

**Status:** in_progress
**Created:** 2026-05-22
**Owner:** Runtime maintainers
**Spec ref:**
**ADR ref:** doc/adr/0007-add-adobe-host-plugins.md
**Board ref:**
**Depends on:** doc/tasks/0017-build-adobe-runtime-bridge.md

## Context

After Effects communicates with effect plugins through one entrypoint selected
by the PiPL resource. That entrypoint receives command selectors such as global
setup, parameter setup, sequence setup, render, SmartFX pre-render, and SmartFX
render. All effect plugins must support render, and SmartFX is the path for
32-bit-per-channel support in After Effects.

CorridorKey needs an After Effects effect that presents the same keying product
behavior as the existing host surfaces while keeping the host process thin. The
effect should register stable metadata and parameters, use the Adobe runtime
bridge for inference, and return Adobe error codes/messages without allowing
C++ exceptions to escape the SDK entrypoint.

This task owns the After Effects host behavior only. Premiere-specific
compatibility and packaging are separate tasks.

## Acceptance Criteria

Verifiable conditions. Each as a checkbox so progress is point-editable.

- [x] The effect entrypoint handles `PF_Cmd_ABOUT`, `PF_Cmd_GLOBAL_SETUP`,
      `PF_Cmd_GLOBAL_SETDOWN`, `PF_Cmd_PARAMS_SETUP`, sequence setup/setdown,
      `PF_Cmd_RENDER`, and the SmartFX selectors needed for supported
      After Effects bit depths.
- [x] All supported bit depths and pixel layouts are explicitly documented in
      code-level tests or host-smoke expectations; unsupported formats return a
      visible Adobe error instead of producing output.
- [x] The effect registers CorridorKey controls needed to build a complete
      prepare request, including model or node identity, quality, screen color,
      optional alpha hint layer, and post-process controls that have OFX
      equivalents.
- [x] Per-instance and per-sequence state uses Adobe-managed handles or RAII
      wrappers and contains no mutable static render state.
- [x] Multi-frame rendering safety is either enabled with focused concurrency
      tests or explicitly disabled with a documented reason in the task Notes.
- [ ] A local After Effects smoke applies the effect to a clip, renders at least
      one frame through the runtime service, preserves alpha output, and closes
      the project without a host crash.
- [ ] A saved After Effects project with keyframed CorridorKey parameters
      reopens with stable effect identity and parameter values.

## Plan

Concrete sequential steps. Each as a checkbox. Reference file paths where applicable.

- [x] Implement the thin Adobe entrypoint dispatcher with explicit render stubs.
- [x] Register PiPL metadata, stable match name, category, support URL, and
      effect display name.
- [x] Register CorridorKey effect parameters and map them into bridge requests.
- [x] Implement render and SmartFX paths for the supported After Effects pixel
      formats.
- [x] Add unit tests for parameter mapping, selector dispatch, state lifetime,
      and unsupported formats.
- [x] Run the Adobe-enabled build and focused unit tests.
- [ ] Run manual After Effects smoke and record host version, project path, and
      render result in Notes.

## Notes

Append-only log. Date each entry. Never rewrite past entries.

### 2026-05-22

Grounding highlights for the After Effects effect:

- After Effects "Entry Point" defines the effect entrypoint signature and says
  the host initiates communication by calling that one function.
- After Effects "Command Selectors" defines the initial setup sequence and says
  all effect plugins must respond to render.
- After Effects "SmartFX" says SmartFX is the way to implement
  32-bit-per-channel support in After Effects and describes the pre-render plus
  render split.
- `Wunkolo/Vulkanator:source/Vulkanator.cpp:1852-1885` dispatches one Adobe
  effect entrypoint across global setup, sequence setup, params setup, and
  SmartFX selectors.
- `bryful/F-s-PluginsProjects/_Skeleton/_Skeleton.cpp:827-932` shows a
  Skeleton-derived effect dispatching the same selector family while catching
  Adobe error exceptions inside the entrypoint.

TDD slice completed for the thin After Effects entrypoint:

- Added unit coverage that calls `EffectMain` through the SDK signature,
  verifies `PF_Cmd_GLOBAL_SETUP` publishes the generated version and Deep Color
  capability, and verifies SmartFX is not advertised until SmartFX render is
  implemented.
- Added unit coverage for `PF_Cmd_ABOUT`, global setdown, sequence setup,
  sequence resetup, sequence setdown, and explicit Adobe errors for
  unimplemented render and SmartFX render selectors.
- Added unit coverage that drives `PF_Cmd_PARAMS_SETUP` through the SDK
  `add_param` callback and captures stable disk IDs, names, types, defaults,
  ranges, popup choices, and flags for the registered CorridorKey controls:
  node identity, quality, screen color, alpha hint policy, despill, spill
  method, recover-details, output mode, and host runtime timeouts.
- Render and SmartFX still intentionally return `PF_Err_BAD_CALLBACK_PARAM`
  with a visible message until the render path is implemented.

Fresh-context review corrections applied in the same TDD slice:

- SmartFX is no longer advertised in global out flags while the SmartFX
  selectors remain explicit unsupported-render stubs.
- `PF_Cmd_PARAMS_SETUP` now rejects missing `output_data`, missing `input_data`,
  and missing `add_param` callback instead of reporting success without
  registering controls.
- `ARCHITECTURE.md` lists the Adobe parameter setup module so the directory map
  remains the structural source of truth.
- Multi-frame rendering stays disabled in this slice. The effect advertises no
  Smart Render or MFR capability until the runtime-backed render path has
  focused concurrency coverage; After Effects may still call the SmartFX
  selectors, but they remain visible unsupported-render errors until that
  coverage exists.
- Added the parameter-to-runtime request mapper used by render. Unit coverage
  verifies model path resolution, host/effect/client identity, quality
  resolution, CPU fallback policy, alpha hint policy, despill, spill method,
  recover-details, edge shrink/feather, output mode, and host runtime timeouts.
- `PF_Cmd_RENDER` now builds an Adobe runtime request, prepares a session
  through `AdobeRuntimeBridge`, processes the source frame through the shared
  host-plugin runtime service, and writes the result back into the Adobe output
  world. The covered direct-render path supports ARGB32 and ARGB64 worlds;
  SmartFX remains disabled and returns a visible unsupported message because
  the effect does not advertise Smart Render in this slice.
- The bridge now has SDK-agnostic output writing coverage for runtime
  `FrameResult` data into Adobe mutable frame views, including processed
  premultiplied output and matte-only output without foreground buffers.
- Verification passed:
  `scripts\verify_ci.ps1 -Mode Format`,
  `git diff --check`,
  `scripts\windows.ps1 -Task build -Preset debug -EnableAdobePlugin -AdobeSdkRoot C:\Dev\CorridorKey-Runtime\vendor\adobe-after-effects-sdk`,
  `build\debug\tests\unit\test_unit.exe "[unit][adobe][effect]"`,
  `build\debug\tests\unit\test_unit.exe "[adobe][runtime]"`,
  `build\debug\tests\integration\test_integration.exe "[integration][adobe][runtime]"`,
  and `ctest --test-dir build\debug -R regression_adobe_cmake_scaffold --output-on-failure`.
- Fresh-context review follow-up keeps SmartFX unchecked until implemented,
  maps Green to the ONNX quality ladder and Blue to the dynamic blue TorchTRT
  artifact through the App-layer runtime contract, clamps corrupted host slider
  values to the declared UI ranges, rejects unknown output modes, detects
  truncated Windows module paths before resolving the sidecar, adds output
  writer coverage for ARGB128 and BGRA32, and makes `Node Identity` the single
  source of Green/Blue domain for both artifact selection and
  `InferenceParams.despill_screen_channel`.
- TDD sequence-lifecycle slice now allocates sequence state through the Adobe
  host handle callbacks during `PF_Cmd_SEQUENCE_SETUP`, unlocks it after
  initialization, and disposes it during `PF_Cmd_SEQUENCE_SETDOWN` and
  `PF_Cmd_SEQUENCE_RESETUP`. Unit coverage drives the behavior through
  `EffectMain` with fake host callbacks, so the effect has per-sequence state
  without mutable static render state.

### 2026-05-23

- TDD SmartFX pre-render slice now accepts `PF_Cmd_SMART_PRE_RENDER` with valid
  Adobe `PF_PreRenderExtra` callbacks, checks out the source layer through the
  host `checkout_layer` callback, and propagates the source result and maximum
  result rectangles back to the host. `PF_Cmd_SMART_RENDER` remains a visible
  unsupported-render error and Smart Render remains unadvertised until the
  pixels checkout plus runtime-backed 32-bpc path is covered.
- TDD SmartFX render checkout slice now accepts `PF_Cmd_SMART_RENDER` with valid
  `PF_SmartRenderExtra` callbacks, checks out input pixels and output pixels
  through the host, injects the checked-out source world into the existing
  render path, and guarantees input pixel checkin when render validation fails.
  Smart Render remains unadvertised until the 32-bpc runtime path and host smoke
  coverage are complete.
- Fresh-context review blocker follow-up keeps `Node Identity` as the Adobe
  session identity while making `Screen Color` drive the runtime screen domain
  for artifact selection and `InferenceParams.despill_screen_channel` when the
  parameter is present. If older host state omits `Screen Color`, `Node
  Identity` remains the compatibility fallback.
- TDD SmartFX 32-bpc format slice now acquires `PF_WorldSuite2` through PICA,
  maps `PF_PixelFormat_ARGB128` to the Adobe bridge's ARGB128 frame format, and
  prevalidates source/output worlds before launching the runtime sidecar. Unit
  coverage proves a 32-bpc SmartFX world is no longer misread as ARGB64 and that
  checked-out pixels are still checked in after validation failure.
- TDD sequence-state hardening now rejects `PF_Cmd_SEQUENCE_SETUP` when the
  Adobe host handle callbacks are unavailable, returning a visible message
  instead of reporting successful lifecycle setup without owned sequence state.
- Verification passed for the review fixes:
  `scripts\windows.ps1 -Task build -Preset debug -EnableAdobePlugin -AdobeSdkRoot C:\Dev\CorridorKey-Runtime\vendor\adobe-after-effects-sdk`,
  `scripts\verify_ci.ps1 -Mode Format`,
  `git diff --check`,
  `build\debug\tests\unit\test_unit.exe "[unit][adobe][effect]"`,
  `build\debug\tests\unit\test_unit.exe "[adobe][runtime]"`, and
  `build\debug\tests\integration\test_integration.exe "[integration][adobe][runtime]"`.
- Diagnose plus TDD follow-up for the fresh-review SmartFX blocker reproduced the
  symptom with a public `EffectMain(PF_Cmd_SMART_RENDER)` unit test: without an
  exact `PF_WorldSuite2` pixel format, a 32-bpc SmartFX world still reached Adobe
  bridge row-byte validation through the `PF_WORLD_IS_DEEP` fallback. The winning
  hypothesis was that pixel-format fallback lived in the shared render helper
  without a SmartFX-specific exact-format policy. SmartFX render now requires an
  exact host pixel format before runtime launch, while direct render keeps the
  depth fallback for the classic render path. Regression coverage rejects both a
  missing world suite and a failed `PF_GetPixelFormat` lookup, and confirms checked
  out pixels are still checked in on rejection.
- Verification passed for the SmartFX exact-format follow-up:
  `scripts\verify_ci.ps1 -Mode Format`,
  `git diff --check`,
  `scripts\windows.ps1 -Task build -Preset debug -EnableAdobePlugin -AdobeSdkRoot C:\Dev\CorridorKey-Runtime\vendor\adobe-after-effects-sdk`,
  `build\debug\tests\unit\test_unit.exe "[unit][adobe][effect]"`,
  `build\debug\tests\unit\test_unit.exe "[adobe][runtime]"`, and
  `build\debug\tests\integration\test_integration.exe "[integration][adobe][runtime]"`.
- TDD global-capabilities slice now advertises Smart Render and Float Color
  Aware through the generated Adobe metadata used by both `PF_Cmd_GLOBAL_SETUP`
  and the PiPL resource. The same public `EffectMain(PF_Cmd_GLOBAL_SETUP)` test
  keeps `PF_OutFlag2_SUPPORTS_THREADED_RENDERING` unset because Multi-Frame
  Rendering still lacks focused concurrency coverage.
- Verification passed for the global-capabilities slice:
  `scripts\windows.ps1 -Task build -Preset debug -EnableAdobePlugin -AdobeSdkRoot C:\Dev\CorridorKey-Runtime\vendor\adobe-after-effects-sdk`,
  `build\debug\tests\unit\test_unit.exe "[unit][adobe][effect]"`,
  `build\debug\tests\unit\test_unit.exe "[adobe][runtime]"`,
  `build\debug\tests\integration\test_integration.exe "[integration][adobe][runtime]"`,
  `ctest --test-dir build\debug -R regression_adobe_cmake_scaffold --output-on-failure`,
  `scripts\verify_ci.ps1 -Mode Format`, and `git diff --check`.
- PiPL metadata registration is complete in the in-repo slice. The generated
  Adobe metadata now carries the stable match name, display name, category,
  support URL, version fields, Smart Render flag, and Float Color Aware flag,
  with coverage through public `EffectMain(PF_Cmd_GLOBAL_SETUP)` assertions and
  `regression_adobe_cmake_scaffold`.
- Adobe SDK audit follow-up aligned plugin-authored errors with
  `PF_OutFlag_DISPLAY_ERROR_MESSAGE` and moved SmartFX non-layer parameter reads
  to host `checkout_param`/`checkin_param` callbacks instead of relying on the
  render selector's `params[]` array. Unit coverage now drives SmartFX through
  public `EffectMain(PF_Cmd_SMART_RENDER)` calls with null `params[]`, verifies
  parameter checkin on render rejection, rejects missing parameter checkout
  callbacks before pixel checkout, and returns host callback failures without a
  duplicate plugin error dialog.
- Adobe SDK audit follow-up keeps `PF_OutFlag2_SUPPORTS_THREADED_RENDERING`
  unset until Multi-Frame Rendering has focused concurrency coverage. The
  remaining product-behavior attention item is `Blue-Green Channel Swap` parity:
  the Adobe UI exposes the option, but the OFX-equivalent channel
  canonicalization still lives in OFX code. That should move into a shared color
  or post-process module before the Adobe effect consumes it.
- TDD Blue-Green Channel Swap parity slice moved screen-color canonicalization
  out of the OFX plugin header and into `src/post_process/screen_color.hpp`.
  The Adobe request now records the selected screen-color mode, the Adobe bridge
  can canonicalize runtime frames into the green domain, and the render path uses
  that transformed frame for inference while preserving the original source frame
  for `Source+Matte` output and restoring foreground output back to the plate
  domain.
- Verification passed for the Blue-Green parity slice:
  `scripts\windows.ps1 -Task build -Preset debug -EnableAdobePlugin -AdobeSdkRoot C:\Dev\CorridorKey-Runtime\vendor\adobe-after-effects-sdk`,
  `scripts\windows.ps1 -Task build -Preset release -EnableAdobePlugin -AdobeSdkRoot C:\Dev\CorridorKey-Runtime\vendor\adobe-after-effects-sdk`,
  `build\debug\tests\unit\test_unit.exe "[unit][adobe][effect]"`,
  `build\debug\tests\unit\test_unit.exe "[unit][adobe][runtime]"`,
  `build\debug\tests\unit\test_unit.exe "[unit][ofx][regression]"`,
  `build\debug\tests\integration\test_integration.exe "[integration][adobe][runtime]"`,
  `ctest --test-dir build\debug -R regression_adobe_cmake_scaffold --output-on-failure`,
  `scripts\verify_ci.ps1 -Mode Format`, and `git diff --check`.
- Hot-path benchmark smoke used `scripts/run_corpus.sh` with the release build,
  `CORRIDORKEY_CORPUS_PROFILE=smoke`, `corridorkey_fp16_1024.onnx`, and
  `ofx_benchmark_harness.exe`. The documented
  `dist/optimization_checkpoints/phase_8_gpu_prepare` artifact is not present in
  this checkout, so the comparison used the local compatible baseline at
  `build/runtime_corpus_before/benchmark_ofx_primary.json`. Current
  `benchmark_ofx_primary.json` reported `avg_latency_ms` `114.314 -> 109.803`
  (`-3.9%`) and `ort_run` `550.540 -> 549.164` (`-0.2%`), within the 10%
  regression gate.
- Local After Effects host smoke remains blocked on this workstation because no
  After Effects installation is present. `AfterFX.exe` was not found under
  `C:\Program Files\Adobe` or `C:\Program Files (x86)\Adobe`; the only Adobe
  application directory found under `C:\Program Files\Adobe` is
  `Adobe Photoshop (Beta)`. The two host-smoke acceptance criteria remain open
  until an After Effects host is available.
- After Effects host smoke with an installed host exposed a version handshake
  failure: `PF_Cmd_GLOBAL_SETUP` reported code version `1.0`, while the PiPL
  resource decoded as version `00`, producing After Effects error `25::16`.
  Diagnose loop: `regression_adobe_cmake_scaffold` was extended to fail unless
  the generated PiPL uses the SDK `PF_VERSION(1,0,0,0,1)` encoding. Ranked
  hypotheses were: raw PiPL version integer instead of SDK bit packing
  (winner), stale/generated PiPL resource not embedded, stale installed `.aex`,
  or generated metadata diverging from `PF_Cmd_GLOBAL_SETUP`. Inspection of the
  generated `.r`/`.rr` files proved the PiPL had `AE_Effect_Version { 1 }`,
  while the Adobe SDK macro encodes the same semantic version as `524289`.
  `src/plugins/adobe/CMakeLists.txt` now computes the PiPL version with the SDK
  bit layout, and the regression verifies `AE_Effect_Version { 524289 }`.
- The After Effects warning about slower preview/export is a separate
  capability warning, not the version bug. We continue to leave
  `PF_OutFlag2_SUPPORTS_THREADED_RENDERING` unset because Multi-Frame Rendering
  needs focused runtime/session concurrency coverage before advertising
  threaded rendering support to the host.
- Alpha Hint parity now uses an Adobe-native `PF_Param_LAYER` named
  `Alpha Hint Layer`, defaulting to `<none>`. Direct render checks the layer
  through `checkout_param`; SmartFX checks it out during pre-render and checks
  out pixels during Smart Render. When a readable layer is connected, its alpha
  channel wins. Without a connected layer, meaningfully varied source alpha
  remains a compatibility hint, `Auto Rough Fallback` generates the shared
  rough matte, and `Require External Hint` returns a visible
  `Waiting for Alpha Hint Layer` error. Nearly opaque source alpha is ignored
  so After Effects precision noise does not turn into an all-foreground hint.
- The Adobe visible effect names now include the generated CorridorKey display
  version label while the stable Green `AE_Effect_Match_Name` remains
  `com.corridorkey.effect` and the Blue effect uses
  `com.corridorkey.effect.blue`. `PF_Cmd_ABOUT` returns each versioned display
  name so the Effect Controls header and About entry agree.
- FPS visibility was grounded as a separate runtime-panel task rather than a
  quick parameter edit. Adobe `PF_InData::time_step` and `time_scale` identify
  composition timing, not CorridorKey processing FPS; live processing FPS should
  be derived from measured render wall time and displayed through a supported
  custom ECW/event UI surface. See `doc/tasks/0021-implement-adobe-runtime-panel.md`.
- Verification passed for the Alpha Hint and visible-version slice:
  `scripts\windows.ps1 -Task build -Preset debug -EnableAdobePlugin -AdobeSdkRoot C:\Dev\CorridorKey-Runtime\vendor\adobe-after-effects-sdk`,
  `scripts\windows.ps1 -Task build -Preset release -EnableAdobePlugin -AdobeSdkRoot C:\Dev\CorridorKey-Runtime\vendor\adobe-after-effects-sdk`,
  `build\debug\tests\unit\test_unit.exe "[unit][adobe][runtime]"`,
  `build\debug\tests\unit\test_unit.exe "[unit][adobe][effect]"`, and
  `ctest --test-dir build\debug -R "regression_adobe_(cmake_scaffold|windows_wrapper_args|package_scaffold)" --output-on-failure`.
  The Release PiPL generated `Name {
  "CorridorKey v0.8.4-win.1-52-gf222bde-dirty-b20260523T215133760Z" }`,
  `AE_Effect_Version { 524289 }`, and stable
  `AE_Effect_Match_Name { "com.corridorkey.effect" }`.
- Adobe now mirrors the dedicated-node product contract used by Resolve and
  Nuke: the build emits `corridorkey_adobe_green.aex` and
  `corridorkey_adobe_blue.aex` instead of one effect with a `Node Identity`
  popup. The Green effect exposes `Green` and `Blue-Green Channel Swap`; the
  Blue effect is locked to the dedicated Blue model/screen path.
- Diagnose loop for the all-white matte: runtime logs showed successful
  inference through `corridorkey_fp16_2048.onnx` with output alpha essentially
  white, and the nearest deterministic seam was alpha-hint source resolution.
  Regression coverage now proves nearly opaque source alpha falls back to the
  rough matte, while a genuinely varied source alpha remains usable as an
  implicit hint. Verification passed with
  `build\debug\tests\unit\test_unit.exe "[unit][adobe][runtime][alpha-hint]"`,
  `build\debug\tests\unit\test_unit.exe "[unit][adobe][effect]"`, and
  `ctest --test-dir build\debug -R "regression_adobe_(pipl_metadata|cmake_scaffold|package_scaffold)" --output-on-failure`.
- The second all-white matte diagnose run found the missing seam in SmartFX:
  the render path checked out `Alpha Hint Layer` pixels unconditionally, so an
  unselected layer control could still be treated as an external all-opaque
  hint before source-alpha fallback logic ran. SmartFX pre-render and render now
  inspect the layer parameter first; `None` skips Alpha Hint checkout, while a
  selected layer still checks out and wins. Regression coverage locks both
  branches with `build\debug\tests\unit\test_unit.exe
  "[unit][adobe][effect][smartfx]"`.

### 2026-05-24

- Diagnose loop for After Effects error `25::23` / `516` reproduced the bug at
  the public `EffectMain(PF_Cmd_SMART_RENDER)` seam: reading `Alpha Hint Layer`
  selection through `checkout_param` can fail before render reaches the runtime.
  Grounding sources were the Adobe SmartFX guide
  (`https://ae-plugins.docsforadobe.dev/smartfx/smartfx/`), SDK samples
  `SDK_Noise.cpp`, `Shifter.cpp`, and `SDK_Invert_ProcAmp.cpp`, and the in-repo
  OFX Resolve/Nuke alpha-hint clip path. Adobe SmartFX guidance says layer
  inputs must be requested with `checkout_layer` during
  `PF_Cmd_SMART_PRE_RENDER`, while non-layer parameters use
  `PF_CHECKOUT_PARAM`; it also says a pre-rendered layer does not have to be
  used later in Smart Render. The Adobe path now follows that contract: SmartFX
  pre-render attempts the optional `Alpha Hint Layer` checkout, records only
  whether it succeeded in `pre_render_data`, and Smart Render only calls
  `checkout_layer_pixels` when that pre-render checkout exists. Alpha Hint pixel
  checkout failure falls back instead of surfacing a plugin dialog.
- Product behavior now matches the Resolve/Nuke dedicated-node contract more
  closely: Adobe removed the user-facing `Alpha Hint Policy` popup. The only
  alpha-hint control is `Alpha Hint Layer`; if it is absent, unreadable, or not
  a meaningful matte, CorridorKey falls back to varied source alpha or the shared
  rough matte. A fully opaque external Alpha Hint no longer overrides the
  fallback path.
- TDD regression coverage added:
  `build\debug\tests\unit\test_unit.exe "[unit][adobe][effect][smartfx]"`,
  `build\debug\tests\unit\test_unit.exe
  "After Effects params setup registers CorridorKey controls"`,
  `build\debug\tests\unit\test_unit.exe
  "After Effects render parameters build runtime request values"`, and
  `build\debug\tests\unit\test_unit.exe "[unit][adobe][runtime][alpha-hint]"`.
- OFX-to-Adobe option parity audit:
  - Runtime status, processing backend, processing device, effective quality,
    guide source, runtime session, status, last frame, requested quality, safe
    quality ceiling, loaded artifact, runtime path, backend work, update
    status, download, open log folder, help buttons, update check, and
    pre-release update toggle remain outside the standard render parameter
    set. Adobe standard parameters cover popups, sliders, checkboxes, layers,
    and buttons, but live telemetry and parameter value mutation are constrained
    to `PF_Cmd_USER_CHANGED_PARAM`, `PF_Cmd_EVENT`, and supported UI update
    paths. The Adobe runtime panel task owns these through custom ECW/event UI
    instead of render-time parameter writes.
  - `Quality`, `Screen Color`, `Alpha Hint Layer`, `Recover Original Details`,
    `Details Edge Shrink`, `Details Edge Feather`, `Despill Strength`, `Spill
    Method`, `Output Mode`, `Prepare Timeout (s)`, and `Render Timeout (s)` were
    already present in Adobe. Stable disk IDs were preserved while the visible
    order was adjusted to match the OFX product flow more closely.
  - `Matte Clip Black`, `Matte Clip White`, `Matte Shrink/Grow`, `Matte Edge
    Blur`, and `Matte Gamma` were brought to Adobe and applied after runtime
    inference with the shared `post_process/alpha_edge` implementation.
  - `Enable Tiling`, `Tile Overlap`, `Auto Despeckle`, `Min Region Size`,
    `Upscale Method`, `Quality Fallback`, and `Coarse Resolution Override` were
    brought to Adobe and mapped into `InferenceParams`. Adobe's host-plugin
    artifact selection now accepts an explicit coarse-resolution override so
    `Coarse to Fine` can prepare the same coarse artifact path the runtime will
    request.
  - `Input Color Space` was not brought in this slice. OFX reads host color
    metadata and can fall back to manual conversion; Adobe needs a proper
    `PF_ColorCallbacksSuite`/project-color-management integration before
    exposing a host-managed choice. A plain popup without host conversion would
    make the UI claim behavior the Adobe bridge does not yet provide.
  - `Refinement Mode` was not brought because the OFX control is disabled for
    the current packaged artifact family; the shared runtime contract rejects
    non-auto refinement overrides for those artifacts.
  - `Temporal Smoothing` was not brought because it requires per-instance or
    per-sequence frame history with explicit frame-order and concurrency
    semantics. The Adobe effect still does not advertise Multi-Frame Rendering,
    and a temporal cache needs its own state-lifetime and host-smoke coverage
    before appearing as a user-facing control.
- Verification passed for the parity slice:
  `scripts\windows.ps1 -Task build -Preset debug -EnableAdobePlugin -AdobeSdkRoot C:\Dev\CorridorKey-Runtime\vendor\adobe-after-effects-sdk`,
  `build\debug\tests\unit\test_unit.exe "[unit][adobe][effect]"`,
  `build\debug\tests\unit\test_unit.exe "[unit][adobe][runtime]"`,
  `build\debug\tests\unit\test_unit.exe "[unit][runtime][quality]"`, and
  `ctest --test-dir build\debug -R "regression_adobe_(pipl_metadata|cmake_scaffold|package_scaffold)" --output-on-failure`.
- Fresh review confirmed the documented `phase_8_gpu_prepare` benchmark
  artifact root is absent from this checkout and not tracked in `HEAD`, so a
  canonical `dist/optimization_checkpoints/phase_8_gpu_prepare` comparison
  cannot be completed locally. The current release build was still measured
  through `scripts/run_corpus.sh` with `CORRIDORKEY_CORPUS_PROFILE=smoke`,
  `CORRIDORKEY_DEVICE=tensorrt`, and `corridorkey_fp16_1024.onnx`, then compared
  with the existing local compatible baseline under `build/runtime_corpus_before`.
  Non-canonical fallback result: synthetic `avg_latency_ms` `101.291 -> 96.786`
  (`-4.4%`) and `ort_run` `748.991 -> 778.672` (`+4.0%`); OFX
  `avg_latency_ms` `114.314 -> 115.348` (`+0.9%`) and `ort_run`
  `550.540 -> 587.427` (`+6.7%`). Both fallback comparisons remain within the
  10% regression gate.

## Definition of Done

All Acceptance Criteria checked, plus:

- [ ] Local tests pass (or N/A documented in Notes)
- [ ] Code review completed (human or fresh-context reviewer per WORKFLOW section 10)
- [ ] No orphan `TODO`/`FIXME` introduced
- [ ] Status updated to `done` and Notes log closes the task
