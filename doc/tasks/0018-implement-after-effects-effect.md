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

- [ ] The effect entrypoint handles `PF_Cmd_ABOUT`, `PF_Cmd_GLOBAL_SETUP`,
      `PF_Cmd_GLOBAL_SETDOWN`, `PF_Cmd_PARAMS_SETUP`, sequence setup/setdown,
      `PF_Cmd_RENDER`, and the SmartFX selectors needed for supported
      After Effects bit depths.
- [x] All supported bit depths and pixel layouts are explicitly documented in
      code-level tests or host-smoke expectations; unsupported formats return a
      visible Adobe error instead of producing output.
- [x] The effect registers CorridorKey controls needed to build a complete
      prepare request, including model or node identity, quality, screen color,
      alpha hint policy, and post-process controls that have OFX equivalents.
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
- [ ] Register PiPL metadata, stable match name, category, support URL, and
      effect display name.
- [x] Register CorridorKey effect parameters and map them into bridge requests.
- [ ] Implement render and SmartFX paths for the supported After Effects pixel
      formats.
- [ ] Add unit tests for parameter mapping, selector dispatch, state lifetime,
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

## Definition of Done

All Acceptance Criteria checked, plus:

- [ ] Local tests pass (or N/A documented in Notes)
- [ ] Code review completed (human or fresh-context reviewer per WORKFLOW section 10)
- [ ] No orphan `TODO`/`FIXME` introduced
- [ ] Status updated to `done` and Notes log closes the task
