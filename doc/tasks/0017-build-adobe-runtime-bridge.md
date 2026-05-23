# Task `0017`: Build Adobe Runtime Bridge

**Status:** done
**Created:** 2026-05-22
**Owner:** Runtime maintainers
**Spec ref:**
**ADR ref:** doc/adr/0007-add-adobe-host-plugins.md
**Board ref:**
**Depends on:** doc/tasks/0016-add-adobe-sdk-build-scaffold.md

## Context

CorridorKey's host plugins must stay thin. The existing OFX surface protects
Resolve and Nuke by launching an App-layer runtime service, preparing sessions
through a versioned IPC protocol, sending frames through shared memory, and
keeping inference backends outside the host process.

Adobe plugins need the same crash-containment and runtime-sharing shape. The
Adobe entrypoints should convert host buffers and parameters into CorridorKey
App/Core requests, but they should not load model artifacts or backend libraries
directly. This task owns the reusable Adobe bridge between Adobe SDK worlds and
the existing runtime service shape.

The implementation may either share generic IPC/runtime-client code with OFX or
introduce Adobe-specific wrapper types over the same App/Core contracts. It must
not fork model selection, diagnostics, or inference policy.

## Acceptance Criteria

Verifiable conditions. Each as a checkbox so progress is point-editable.

- [x] The Adobe plugin module has no direct import or link dependency on ONNX
      Runtime, CUDA, LibTorch, TensorRT, or packaged model files.
- [x] The bridge exposes health, prepare-session, process-frame, and
      release-session operations using `Result<T>`-style error handling.
- [x] Frame conversion from Adobe host buffers into CorridorKey `Image` or
      `ImageBuffer` handles row stride, bounds, channel order, and alpha without
      using `std::vector` for image data.
- [x] Unsupported Adobe pixel formats fail with a host-visible error instead
      of silent color corruption.
- [x] Runtime session identity includes the Adobe host surface and effect
      identity so Adobe renders cannot accidentally share state with OFX nodes
      or a different Adobe effect identity.
- [x] Unit tests cover frame conversion, unsupported-format failures, and
      prepare-request construction without requiring Adobe, GPU, I/O, or model
      files.
- [x] Integration or smoke coverage proves the bridge can talk to the runtime
      service health endpoint when the service binary is staged.

## Plan

Concrete sequential steps. Each as a checkbox. Reference file paths where applicable.

- [x] Identify the narrow reusable seam between `src/plugins/ofx` runtime
      client code and Adobe plugin needs.
- [x] Add Adobe bridge source under the architecture-approved Adobe plugin
      directory.
- [x] Add frame-view conversion helpers for Adobe worlds and host pixel formats.
- [x] Add prepare-request mapping for CorridorKey quality, selected model
      artifact, and runtime prewarm metadata.
- [x] Preserve screen color, alpha hint, and post-process controls through the
      selected model artifact, Adobe alpha conversion, and `InferenceParams`
      passed through `process_frame`.
- [x] Add tests for conversion and request construction.
- [x] Add an import/link audit script or test that blocks backend libraries from
      the Adobe plugin module.
- [x] Run focused unit tests and the runtime-service health smoke.

## Notes

Append-only log. Date each entry. Never rewrite past entries.

### 2026-05-22

Grounding highlights for this bridge:

- `ARCHITECTURE.md:24-29` requires Library First and Interface Segregation.
- `ARCHITECTURE.md:240-250` and `docs/SPEC.md:120-132` define the existing
  out-of-process runtime service and shared-memory frame transport shape.
- `src/app/host_plugin_runtime_client.hpp` exposes the current health,
  prepare-session, process-frame, and release-session client surface.
- `src/app/host_plugin_runtime_protocol.cpp:226-245` names the existing runtime
  protocol commands, and `src/app/host_plugin_runtime_service.cpp:544-664` dispatches
  health, prepare, render, and release requests through the broker.
- `src/plugins/ofx/ofx_render.cpp:619-700` is the closest in-repo analog for a
  host render callback that snapshots parameters, fetches host buffers, and
  delegates to the runtime pipeline.

Deepening follow-up for the reusable runtime seam:

- Renamed the OFX-owned runtime sidecar/client/protocol/broker into
  App-layer `HostPluginRuntime*` components.
- Moved the reusable client, runtime-family policy, server entry point, broker,
  protocol, and session policy under `src/app/`, leaving OFX-specific names and
  logging only at the OFX adapter edge.
- Renamed the Windows sidecar binary to
  `corridorkey_host_plugin_runtime_server.exe`, the CLI subcommand to
  `host-plugin-runtime-server`, and runtime override environment variables to
  the `CORRIDORKEY_HOST_PLUGIN_RUNTIME_*` family.
- Verification passed for the focused runtime TDD map:
  `build\debug\tests\unit\test_unit.exe "[ofx][runtime]" "[runtime][unit]"`,
  `build\debug\tests\integration\test_integration.exe "[integration][ofx][runtime]"`,
  and build targets `ofx_benchmark_harness` / `ofx_rpc_benchmark_harness`.
- Final Windows verification passed through
  `scripts\windows.ps1 -Task build -Preset debug -EnableAdobePlugin -AdobeSdkRoot C:\Dev\CorridorKey-Runtime\vendor\adobe-after-effects-sdk`.
  The build produced `src\plugins\adobe\corridorkey_adobe.aex`, staged
  `corridorkey_host_plugin_runtime_server.exe` in the OFX bundle, removed the
  legacy `corridorkey_ofx_runtime_server.exe` from the build-tree bundle, and
  `dumpbin /dependents` showed the OFX plugin still imports only host/system
  DLLs rather than ONNX Runtime, CUDA, LibTorch, or TensorRT.
- Packaging verification passed through
  `scripts\windows.ps1 -Task package-ofx -Preset debug -Track rtx` with
  forwarded `-SkipNsisInstaller -BuildDir C:\Dev\CorridorKey-Runtime\build\debug`.
  The staged RTX bundle validation found the new host-plugin runtime server,
  verified its PE imports, and completed packaged doctor as healthy.
- Added `src/plugins/adobe/adobe_bridge.hpp`,
  `src/plugins/adobe/adobe_bridge.cpp`, and
  `src/plugins/adobe/adobe_runtime_bridge.cpp` as the Adobe host adapter over
  the shared App-layer `HostPluginRuntimeClient`. The bridge exposes health,
  prepare-session, process-frame, and release-session operations with
  `Result<T>` errors and keeps the Adobe target linked only to
  `corridorkey_common`.
- Adobe frame conversion is SDK-agnostic and covers After Effects ARGB32,
  ARGB64, ARGB128, and Premiere BGRA32. It copies into CorridorKey
  `ImageBuffer` RGB plus alpha-hint buffers, validates row bytes and optional
  data size, rejects unsupported formats, and does not use `std::vector` for
  image data.
- Prepare-session construction scopes runtime identity with percent-escaped
  components as `adobe:<host_surface>:<effect_identity>` and appends the caller
  instance id only to `client_instance_id`, preventing Adobe effects from
  sharing state with OFX nodes or different Adobe effect identities.
- Screen color remains represented by the selected model artifact, alpha hint
  is derived from the Adobe source alpha channel, and post-process controls stay
  in the `InferenceParams` passed through `process_frame`; the bridge preserves
  those existing App/Core contracts instead of adding Adobe-specific policy.
- Verification passed:
  `scripts\windows.ps1 -Task build -Preset debug -EnableAdobePlugin -AdobeSdkRoot C:\Dev\CorridorKey-Runtime\vendor\adobe-after-effects-sdk`,
  `build\debug\tests\unit\test_unit.exe "[adobe][runtime]"`, and
  `build\debug\tests\integration\test_integration.exe "[integration][adobe][runtime]"`,
  plus `ctest --test-dir build\debug -R regression_adobe_cmake_scaffold --output-on-failure`.
  `dumpbin /dependents build\debug\src\plugins\adobe\corridorkey_adobe.aex`
  reported only `WS2_32.dll` and `KERNEL32.dll`.
- Fresh-context review completed through separate Standards and Spec passes.
  The follow-up patch added delimiter-safe identity encoding, prepare option
  range validation, frame dimension limits before allocation, Winsock/socket
  RAII plus retry handling in the Adobe runtime smoke test, and a broader
  backend-family dependency audit in
  `tests/regression/test_adobe_cmake_scaffold.ps1`.
- Follow-up verification passed:
  `scripts\windows.ps1 -Task build -Preset debug -EnableAdobePlugin -AdobeSdkRoot C:\Dev\CorridorKey-Runtime\vendor\adobe-after-effects-sdk`,
  `build\debug\tests\unit\test_unit.exe "[adobe][runtime]"`,
  `build\debug\tests\integration\test_integration.exe "[integration][adobe][runtime]"`,
  and `ctest --test-dir build\debug -R regression_adobe_cmake_scaffold --output-on-failure`.
- Hot-path benchmark gate passed for the branch changes touching
  `src/plugins/ofx/`, `src/core/inference_session.cpp`,
  `src/core/gpu_prep.cpp`, and `src/post_process/despeckle.cpp`.
  `scripts/run_corpus.sh` plus `scripts/compare_benchmarks.py` compared the
  branch against the `phase_8_gpu_prepare` baseline and reported
  `avg_latency_ms` `-45.4%` and `ort_run` `-52.0%`, both below the 10%
  regression threshold.

## Definition of Done

All Acceptance Criteria checked, plus:

- [x] Local tests pass (or N/A documented in Notes)
- [x] Code review completed (human or fresh-context reviewer per WORKFLOW section 10)
- [x] No orphan `TODO`/`FIXME` introduced
- [x] Status updated to `done` and Notes log closes the task
