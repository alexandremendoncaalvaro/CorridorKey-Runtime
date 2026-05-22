# Task `0016`: Add Adobe SDK Build Scaffold

**Status:** done
**Created:** 2026-05-22
**Owner:** Runtime maintainers
**Spec ref:**
**ADR ref:** doc/adr/0007-add-adobe-host-plugins.md
**Board ref:**
**Depends on:** doc/tasks/0015-align-adobe-plugin-scope.md

## Context

The Adobe plugin work needs a buildable host-plugin scaffold before runtime
behavior can be added. The After Effects SDK relies on PiPL resources for effect
metadata, and the official guide says the PiPL outflags must agree with
`PF_Cmd_GLOBAL_SETUP`. The guide also recommends starting from the Skeleton
sample to avoid reconstructing the Windows PiPL resource build steps.

CorridorKey uses CMake presets, vcpkg manifest mode, and target-scoped build
settings. Adobe SDK availability must not make the normal build fail on
machines that do not have the SDK installed. The scaffold must therefore be an
optional target enabled only when the Adobe SDK root is configured.

This task owns SDK discovery, CMake target shape, PiPL generation, and a minimal
loadable entrypoint. It must not implement CorridorKey inference, runtime IPC,
host parameter parity, or installer distribution.

## Acceptance Criteria

Verifiable conditions. Each as a checkbox so progress is point-editable.

- [x] The default CMake configure and `scripts/windows.ps1 -Task build` remain
      clean when no Adobe SDK root is configured.
- [x] A documented CMake cache variable or preset option enables the Adobe
      plugin target when the After Effects SDK root is present.
- [x] The Adobe scaffold builds a Windows `.aex` module target through
      target-scoped CMake settings, with no global include or link settings.
- [x] The scaffold compiles one PiPL resource for one effect code fragment and
      records a stable match name before host projects can depend on it.
- [x] The PiPL `AE_Effect_Global_OutFlags` and
      `AE_Effect_Global_OutFlags_2` values match the flags returned during
      global setup.
- [x] A smoke test or build verification proves the exported Adobe entrypoint
      exists in the built module.
- [x] The scaffold does not link ONNX Runtime, CUDA, LibTorch, TensorRT, or
      model artifacts into the Adobe plugin module.

## Plan

Concrete sequential steps. Each as a checkbox. Reference file paths where applicable.

- [x] Add the Adobe plugin directory and `src/plugins/adobe/CMakeLists.txt`
      only after `ARCHITECTURE.md` has been updated by task 0015.
- [x] Add CMake SDK-root detection and keep the target disabled when the SDK is
      absent.
- [x] Add the minimal Adobe effect source, header, and PiPL resource file.
- [x] Add a Windows PiPL generation step using the SDK resource tools.
- [x] Add a focused smoke test or script check for the exported entrypoint and
      module suffix.
- [x] Run `scripts/windows.ps1 -Task build` without the Adobe SDK enabled.
- [x] Run the Adobe-enabled build on a machine with the SDK installed and record
      the artifact path in Notes.

## Notes

Append-only log. Date each entry. Never rewrite past entries.

### 2026-05-22

Grounding highlights for this scaffold:

- After Effects "PiPL Resources" says PiPL behavior must agree with
  `PF_Cmd_GLOBAL_SETUP`, Windows samples process a `.r` file through the SDK
  PiPL tool, and the match name is a stable identifier separate from the display
  name.
- After Effects "How To Start Creating Plug-ins" recommends starting from the
  Skeleton template for effects because the sample already contains the
  troublesome PiPL build steps.
- `Wunkolo/Vulkanator:CMakeLists.txt:92-136` shows a CMake `MODULE` target with
  custom commands for PiPL resource generation.
- `bryful/F-s-PluginsProjects/_Skeleton/Win/_Skeleton.vcxproj:71-96` builds an
  `.aex` and `:396-427` wires PiPL custom build and resource compilation.

### 2026-05-22

TDD cycle started with SDK-resolution behavior before adding plugin sources.
`tests/regression/test_adobe_sdk_resolution.cmake` first failed because the
resolver module did not exist, then passed after adding
`cmake/CorridorKeyAdobeSdk.cmake`.

Follow-up red checks caught incomplete fake SDK roots and missing exported
resource-directory state. The resolver now reports `disabled`, `missing`, or
`available`, requires the core After Effects SDK headers plus `PiPLTool`, and
exports include, resource, and tool paths for the plugin target.

`CORRIDORKEY_ENABLE_ADOBE_PLUGIN` and `CORRIDORKEY_ADOBE_SDK_ROOT` are CMake
cache entries. With a complete fake SDK, configure emits a
`corridorkey_adobe` module target with `.aex` suffix. The target is under
`src/plugins/adobe` and is added only when SDK discovery succeeds.

Verification:

- `cmake --preset debug`
- `powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts\windows.ps1 -Task build -Preset debug`
- `ctest --test-dir build\debug -R regression_adobe_sdk_resolution --output-on-failure`
- `powershell.exe -NoProfile -ExecutionPolicy Bypass -File tests\regression\test_adobe_cmake_scaffold.ps1`

The scaffold still lacks PiPL generation, stable match-name resource data, and
a built-module export smoke test, so the remaining Acceptance Criteria stay
unchecked.

### 2026-05-22

Second TDD cycle covered the PiPL and binary scaffold. The red check was
`cmake -P tests\regression\test_adobe_pipl_metadata.cmake`, which failed until
the generated metadata header and PiPL template existed.

The Adobe target now configures one effect metadata set in
`src/plugins/adobe/CMakeLists.txt`, generates `adobe_effect_metadata.hpp` and
`corridorkey_adobe.r`, runs the Windows PiPL pipeline through
`cmake/CorridorKeyRunAdobePipl.cmake`, and compiles the generated `.rc` into
the `.aex` target. `EffectMain` now uses the Adobe SDK entrypoint signature and
returns the same generated `out_flags` and `out_flags2` values recorded by the
PiPL template.

`tests/regression/test_adobe_cmake_scaffold.ps1` now builds
`corridorkey_adobe` with a fake SDK/PiPLTool, verifies
`corridorkey_adobe.aex`, checks that `EffectMain` is exported, and rejects
direct imports of ONNX Runtime, CUDA, TensorRT, or Torch libraries.

Verification:

- `cmake -P tests\regression\test_adobe_pipl_metadata.cmake`
- `cmake -P tests\regression\test_adobe_sdk_resolution.cmake`
- `powershell.exe -NoProfile -ExecutionPolicy Bypass -File tests\regression\test_adobe_cmake_scaffold.ps1`
- `cmake --preset debug`
- `ctest --test-dir build\debug -R "regression_adobe_(sdk_resolution|pipl_metadata)" --output-on-failure`
- `powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts\windows.ps1 -Task build -Preset debug`
- `git diff --check`

No repository-local After Effects SDK root is present under `vendor/`, so the
Adobe-enabled build against a real SDK remains unchecked.

### 2026-05-22

The After Effects SDK download
`C:\Users\alexa\Downloads\AfterEffectsSDK_25.6_61_win.zip` was extracted into
the ignored local payload root `vendor\adobe-after-effects-sdk`. The official
SDK layout stores headers and resources under `Examples\Headers` and
`Examples\Resources`, so `cmake/CorridorKeyAdobeSdk.cmake` now accepts either
`Headers`/`Resources` directly under the configured root or the nested
`Examples` layout. The resolver no longer requires `entry.h`, which is absent
from the SDK 25.6 archive and unused by this scaffold.

The Windows wrapper now accepts `-EnableAdobePlugin` and `-AdobeSdkRoot`, and
forwards them through `scripts\windows.ps1` into `scripts\build.ps1` so the
canonical Windows entrypoint can build the Adobe target.

Real SDK build artifact:

- `build\debug\src\plugins\adobe\corridorkey_adobe.aex`

Verification:

- `powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts\windows.ps1 -Task build -Preset debug -EnableAdobePlugin -AdobeSdkRoot C:\Dev\CorridorKey-Runtime\vendor\adobe-after-effects-sdk`
- `dumpbin.exe /nologo /exports build\debug\src\plugins\adobe\corridorkey_adobe.aex`
- `dumpbin.exe /nologo /dependents build\debug\src\plugins\adobe\corridorkey_adobe.aex`
- `ctest --test-dir build\debug -R "regression_adobe" --output-on-failure`
- `git check-ignore -v vendor\adobe-after-effects-sdk\Examples\Headers\AE_Effect.h`

The `.aex` exports `EffectMain` and does not directly import ONNX Runtime,
CUDA, TensorRT, or Torch libraries.

### 2026-05-22

Fresh-context review completed and follow-up concerns were corrected through
TDD. The SDK resolver now resolves relative SDK roots from the repository root,
explicit Adobe enablement fails fast when the requested SDK is incomplete, and
the PowerShell wrapper rejects Adobe arguments for non-build tasks instead of
silently ignoring them.

`EffectMain` now follows the Adobe Skeleton setup split: `PF_Cmd_GLOBAL_SETUP`
publishes `my_version`, `out_flags`, and `out_flags2`; `PF_Cmd_PARAMS_SETUP`
publishes `num_params`. The metadata template now generates version constants
used by `PF_VERSION`.

`tests/regression/test_adobe_cmake_scaffold.ps1` uses build-local fake SDK
state, checks incomplete SDK rejection, and is registered in CTest as
`regression_adobe_cmake_scaffold`. `ARCHITECTURE.md` now lists only the Adobe
scaffold files that exist in this task.

Verification:

- `cmake -P tests\regression\test_adobe_sdk_resolution.cmake`
- `cmake -P tests\regression\test_adobe_pipl_metadata.cmake`
- `powershell.exe -NoProfile -ExecutionPolicy Bypass -File tests\regression\test_adobe_windows_wrapper_args.ps1`
- `powershell.exe -NoProfile -ExecutionPolicy Bypass -File tests\regression\test_adobe_cmake_scaffold.ps1`
- `powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts\windows.ps1 -Task build -Preset debug -EnableAdobePlugin -AdobeSdkRoot C:\Dev\CorridorKey-Runtime\vendor\adobe-after-effects-sdk`
- `ctest --test-dir build\debug -R regression_adobe --output-on-failure`
- `dumpbin.exe /nologo /exports build\debug\src\plugins\adobe\corridorkey_adobe.aex`
- `dumpbin.exe /nologo /dependents build\debug\src\plugins\adobe\corridorkey_adobe.aex`

## Definition of Done

All Acceptance Criteria checked, plus:

- [x] Local tests pass (or N/A documented in Notes)
- [x] Code review completed (human or fresh-context reviewer per WORKFLOW section 10)
- [x] No orphan `TODO`/`FIXME` introduced
- [x] Status updated to `done` and Notes log closes the task
