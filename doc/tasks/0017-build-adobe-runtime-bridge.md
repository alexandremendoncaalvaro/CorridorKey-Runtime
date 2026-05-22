# Task `0017`: Build Adobe Runtime Bridge

**Status:** in_progress
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

- [ ] The Adobe plugin module has no direct import or link dependency on ONNX
      Runtime, CUDA, LibTorch, TensorRT, or packaged model files.
- [ ] The bridge exposes health, prepare-session, process-frame, and
      release-session operations using `Result<T>`-style error handling.
- [ ] Frame conversion from Adobe host buffers into CorridorKey `Image` or
      `ImageBuffer` handles row stride, bounds, channel order, and alpha without
      using `std::vector` for image data.
- [ ] Unsupported Adobe pixel formats fail with a host-visible error instead
      of silent color corruption.
- [ ] Runtime session identity includes the Adobe host surface and effect
      identity so Adobe renders cannot accidentally share state with OFX nodes
      or a different Adobe effect identity.
- [ ] Unit tests cover frame conversion, unsupported-format failures, and
      prepare-request construction without requiring Adobe, GPU, I/O, or model
      files.
- [ ] Integration or smoke coverage proves the bridge can talk to the runtime
      service health endpoint when the service binary is staged.

## Plan

Concrete sequential steps. Each as a checkbox. Reference file paths where applicable.

- [x] Identify the narrow reusable seam between `src/plugins/ofx` runtime
      client code and Adobe plugin needs.
- [ ] Add Adobe bridge source under the architecture-approved Adobe plugin
      directory.
- [ ] Add frame-view conversion helpers for Adobe worlds and host pixel formats.
- [ ] Add prepare-request mapping for CorridorKey quality, model, screen color,
      alpha hint, and post-process controls.
- [ ] Add tests for conversion and request construction.
- [ ] Add an import/link audit script or test that blocks backend libraries from
      the Adobe plugin module.
- [ ] Run focused unit tests and the runtime-service health smoke.

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

## Definition of Done

All Acceptance Criteria checked, plus:

- [ ] Local tests pass (or N/A documented in Notes)
- [ ] Code review completed (human or fresh-context reviewer per WORKFLOW section 10)
- [ ] No orphan `TODO`/`FIXME` introduced
- [ ] Status updated to `done` and Notes log closes the task
