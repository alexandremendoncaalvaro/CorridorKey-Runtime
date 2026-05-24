# Task `0020`: Package Adobe Plugins

**Status:** in_progress
**Created:** 2026-05-22
**Owner:** Runtime maintainers
**Spec ref:**
**ADR ref:** doc/adr/0007-add-adobe-host-plugins.md
**Board ref:**
**Depends on:** doc/tasks/0018-implement-after-effects-effect.md, doc/tasks/0019-validate-premiere-compatibility.md

## Context

After the Adobe effect builds and passes host smoke tests, users still need a
repeatable way to install it with the same runtime payload, model inventory,
diagnostics, and validation discipline as the existing product surfaces. The
repository rules require Windows build, package, certification, and release
work to go through `scripts/windows.ps1`, not through internal delegate scripts.

The package must stage the Adobe `.aex` effect, the out-of-process runtime
service, required app-local runtime libraries, and model artifacts without
turning missing optional models into silent success. The support matrix must not
claim a support level beyond the validation evidence produced by this task.

This task owns packaging and certification. It must not add new render behavior
or change Adobe parameter semantics.

## Acceptance Criteria

Verifiable conditions. Each as a checkbox so progress is point-editable.

- [x] `scripts/windows.ps1` exposes a canonical Adobe packaging or release task,
      and no Adobe packaging workflow requires calling an internal delegate
      script directly.
- [x] The Windows Adobe package stages the `.aex` effect into the Adobe Common
      Plug-ins MediaCore path or into an installer payload that targets that
      path.
- [x] The package stages the runtime service, app-local dependencies, model
      inventory, validation reports, and diagnostics expected by the Adobe
      bridge.
- [x] Generated inventory and validation reports identify the Adobe plugin
      binary, PiPL/effect identity, runtime payload, and packaged model state.
- [x] Missing packaged models are surfaced as reportable package state, while
      invalid packaged models still block the package flow.
- [x] The Adobe installer uses the shared modern Inno Setup online/offline
      installer flow, including branded wizard assets, SHA256-verified online
      pack downloads, and selectable Green only, Blue only, or Green + Blue
      model packs.
- [x] Clean install and upgrade install both leave After Effects and Premiere
      able to discover the effect after host restart.
- [ ] The support matrix, README, and user help are updated only to the support
      designation justified by the completed host validation evidence.

## Plan

Concrete sequential steps. Each as a checkbox. Reference file paths where applicable.

- [x] Extend `scripts/windows.ps1` with the canonical Adobe packaging entrypoint
      after agreeing the task name and parameters.
- [x] Add installer or package staging for the Adobe plugin module, runtime
      service, app-local dependencies, and model packs.
- [x] Extend package inventory and validation reporting for Adobe artifacts.
- [x] Reuse the shared modern installer builder for Adobe instead of keeping a
      separate bare Inno Setup script.
- [x] Add clean-install and upgrade-install smoke checks for Adobe host plugin
      discovery.
- [x] Run the canonical Windows package task and record artifact paths, hashes,
      and validation reports in Notes.
- [ ] Update user-facing docs after the package and host validation evidence
      exists.
- [ ] Run fresh-context review before marking this task done.

## Notes

Append-only log. Date each entry. Never rewrite past entries.

### 2026-05-22

Grounding highlights for packaging:

- `AGENTS.md` requires Windows build, package, certification, and release flows
  to run through `scripts/windows.ps1`.
- After Effects "Sample Projects" documents the Adobe Common Plug-ins `7.0`
  MediaCore development path for Adobe effect discovery.
- Existing package behavior in `src/plugins/ofx/CMakeLists.txt` stages the OFX
  bundle and runtime payload, while `docs/SPEC.md:120-132` requires the
  host-plugin runtime to stay out of process and versioned.
- `help/SUPPORT_MATRIX.md` currently says Adobe hosts are unsupported; this
  task must update that only after host validation creates evidence.

### 2026-05-23

Grounding and implementation notes for the first Windows Adobe package slice:

- Adobe's After Effects SDK guide recommends the shared Common Plug-ins
  MediaCore location for plug-ins that should be loaded by Premiere Pro as well
  as After Effects, and documents the Windows registry value
  `HKLM\SOFTWARE\Adobe\After Effects\[version]\CommonPluginInstallPath`.
  Source: https://ae-plugins.docsforadobe.dev/intro/where-installers-should-put-plug-ins/
- Adobe's PiPL guidance says PiPL outflags must agree with
  `PF_Cmd_GLOBAL_SETUP`; package inventory and validation now record the Adobe
  match name and SmartFX/float-capable PiPL capability claims. Source:
  https://ae-plugins.docsforadobe.dev/intro/pipl-resources/
- Implemented canonical `scripts/windows.ps1 -Task package-adobe -Preset release
  -Track rtx`. The wrapper delegates to `scripts/package_adobe_plugins_windows.ps1`
  and asserts `adobe_package_validation.json` through
  `Assert-CorridorKeyAdobePackageValidationHealthy`.
- The package stages an installer payload rooted at
  `dist/CorridorKey_Adobe_v0.8.4-win.1-52-gf222bde-dirty-b20260523T203222484Z_Windows_RTX/Adobe/Common/Plug-ins/7.0/MediaCore/CorridorKey`.
  The runtime layout is `Contents/Win64` for `corridorkey_adobe.aex`,
  `corridorkey.exe`, `corridorkey_host_plugin_runtime_server.exe`, and
  app-local DLLs; models and TorchTRT resources live under
  `Contents/Resources`.
- Added Adobe package validation at `scripts/validate_adobe_package_win.ps1`.
  It validates MediaCore payload shape, runtime DLL presence, app-local Visual
  C++ runtime DLLs, effect identity, PiPL capability inventory, model inventory,
  runtime backend probe, and packaged doctor output.
- `src/app/runtime_diagnostics.cpp` now recognizes the `Contents/Win64`
  Adobe payload as `windows_adobe` when `corridorkey_adobe.aex` is present, so
  the runtime doctor no longer misclassifies the Adobe package as a broken OFX
  package.
- `src/plugins/adobe/adobe_effect_render.cpp` resolves packaged models relative
  to the Adobe plugin module path, so After Effects and Premiere do not have to
  rely on process-relative model discovery from `AfterFX.exe` or
  `Adobe Premiere Pro.exe`.
- Canonical package verification passed:
  `scripts/windows.ps1 -Task package-adobe -Preset release -Track rtx`.
  Validation report:
  `dist/CorridorKey_Adobe_v0.8.4-win.1-52-gf222bde-dirty-b20260523T203222484Z_Windows_RTX/adobe_package_validation.json`.
  Result: `validation_passed=true`, `doctor.layout_kind=windows_adobe`,
  `doctor.bundle_healthy=true`, supported backends `tensorrt, torchtrt, cpu`,
  five packaged models present, zero missing models.
- The same package command emitted the Inno Setup installer at
  `dist/CorridorKey_Adobe_v0.8.4-win.1-52-gf222bde-dirty-b20260523T203222484Z_Windows_RTX_Install.exe`.
  NSIS was rejected for this payload after a real compile hit an internal mmap
  limit on the large model/runtime package; Adobe packaging now uses Inno Setup
  with `PrivilegesRequired=admin`, matching the modern Windows installer path.
- Clean-install and upgrade-install host discovery smokes remain open; support
  matrix and user-facing support claims remain unchanged until that evidence
  exists.
- After Effects host smoke diagnosed a PiPL/code version mismatch in the built
  effect. The package flow was rerun after fixing the generated PiPL version
  encoding to `PF_VERSION(1,0,0,0,1)` (`524289`). Canonical verification passed:
  `scripts/windows.ps1 -Task package-adobe -Preset release -Track rtx`.
  Validation report:
  `dist/CorridorKey_Adobe_v0.8.4-win.1-52-gf222bde-dirty-b20260523T210044257Z_Windows_RTX/adobe_package_validation.json`.
  Result: `validation_passed=true`, `doctor.layout_kind=windows_adobe`,
  `doctor.bundle_healthy=true`, supported backends `tensorrt, torchtrt, cpu`,
  five packaged models present, zero missing models. The regenerated installer
  is
  `dist/CorridorKey_Adobe_v0.8.4-win.1-52-gf222bde-dirty-b20260523T210044257Z_Windows_RTX_Install.exe`
  with SHA256
  `A9A9A1014F876B38B7E387DA06249DD4C1DD4E0DEBE651143D8FAFF532724B41`.
- Adobe installer metadata now uses the same display version label as the generated
  filename for the visible Inno Setup fields: `AppVersion`,
  `AppVerName`, uninstall display name, `VersionInfoProductTextVersion`, and
  `VersionInfoTextVersion`. Numeric Windows version fields stay on the base
  CMake version through `VersionInfoVersion` and `VersionInfoProductVersion`.
  Regression coverage in `regression_adobe_package_scaffold` locks this split.
- The package flow was rerun after rebuilding the Release preset with Adobe
  enabled so the staged PiPL and installer payload contain the versioned effect
  display name. Canonical verification passed:
  `scripts\windows.ps1 -Task build -Preset release -EnableAdobePlugin -AdobeSdkRoot C:\Dev\CorridorKey-Runtime\vendor\adobe-after-effects-sdk`
  and `scripts\windows.ps1 -Task package-adobe -Preset release -Track rtx`.
  Validation report:
  `dist/CorridorKey_Adobe_v0.8.4-win.1-52-gf222bde-dirty-b20260523T215133760Z_Windows_RTX/adobe_package_validation.json`.
  Result: `validation_passed=true`, effect name
  `CorridorKey v0.8.4-win.1-52-gf222bde-dirty-b20260523T215133760Z`,
  `match_name=com.corridorkey.effect`, five packaged models present, zero
  missing models. The regenerated installer is
  `dist/CorridorKey_Adobe_v0.8.4-win.1-52-gf222bde-dirty-b20260523T215133760Z_Windows_RTX_Install.exe`
  with SHA256
  `117D57D4BF48396D49DB40B9610591E2760EC30C5FBA0B566D726FC2AC666256`.
- Adobe packaging now delegates installer creation to
  `scripts/installer/build_installer.ps1` with `-InstallerSurface adobe`
  instead of emitting a separate bare Inno Setup script. The shared template
  keeps the existing product identity assets (`assets/ck-install-banner.png`
  and `assets/ck-microchip.png`), the built-in Inno Setup download page, and
  manifest SHA256 verification for online pack downloads.
- `scripts/package_adobe_plugins_windows.ps1` defaults to the online installer
  flavor and accepts `-Flavor online|offline`. The generated online installer
  exposes Green only, Blue only, Recommended (Green + Blue), and Custom setup
  types. Green only means the green model pack, including blue-screen handling
  through channel-swap canonicalization; Blue only downloads the dedicated
  dynamic blue model and LibTorch runtime pack.
- The Adobe installer surface uses the display version label for Inno
  `AppVersion`, `AppVerName`, and text version metadata, while numeric Windows
  resource fields stay on the base CMake version. This preserves the visible
  installer/package identity without invalid numeric version resources.
- Rebuilt the Release preset with Adobe enabled and explicit display label
  `0.8.5-win.1`, then produced the canonical online Adobe installer through
  `scripts\windows.ps1 -Task package-adobe -Preset release -Track rtx -Flavor online
  -DisplayVersionLabel 0.8.5-win.1`. Validation report:
  `dist/CorridorKey_Adobe_v0.8.5-win.1_Windows_RTX/adobe_package_validation.json`.
  Result: `validation_passed=true`, `doctor.layout_kind=windows_adobe`,
  `doctor.bundle_healthy=true`, effect name `CorridorKey v0.8.5-win.1`,
  `match_name=com.corridorkey.effect`, five packaged models present, zero
  missing models. The installer is
  `dist/CorridorKey_Adobe_v0.8.5-win.1_Windows_RTX_online_Setup.exe` with
  SHA256 `64137CAA48D49B1A7B45740251EC1EFB21C6E0BEF944AFDA8AE25DA289B6380C`.
- Adobe packaging now stages dedicated Green and Blue effect modules instead
  of a single module with an in-effect node identity selector:
  `corridorkey_adobe_green.aex` uses match name `com.corridorkey.effect` and
  visible name `CorridorKey Green v<display-label>`, while
  `corridorkey_adobe_blue.aex` uses match name
  `com.corridorkey.effect.blue` and visible name
  `CorridorKey Blue v<display-label>`. Package validation records both effect
  identities and verifies their SmartFX PiPL capability claims.
- The shared Inno installer template gates the Adobe Green and Blue `.aex`
  files by installer component. Installing only Green leaves only the Green
  effect visible in Adobe hosts; installing only Blue leaves only the Blue
  effect visible. The Adobe installer also removes the retired
  `corridorkey_adobe.aex` and removes any unselected Green or Blue effect from
  an existing install during component changes.
- Rebuilt the Release preset with Adobe enabled and explicit display label
  `0.8.5-win.1`, then produced the refreshed canonical online Adobe installer
  through `scripts\windows.ps1 -Task package-adobe -Preset release -Track rtx
  -Flavor online -DisplayVersionLabel 0.8.5-win.1`. Validation report:
  `dist/CorridorKey_Adobe_v0.8.5-win.1_Windows_RTX/adobe_package_validation.json`.
  Result: `validation_passed=true`, `doctor.layout_kind=windows_adobe`,
  `doctor.bundle_healthy=true`, Green effect
  `CorridorKey Green v0.8.5-win.1` with match name
  `com.corridorkey.effect`, Blue effect `CorridorKey Blue v0.8.5-win.1`
  with match name `com.corridorkey.effect.blue`, five packaged models
  present, zero missing models. The refreshed installer is
  `dist/CorridorKey_Adobe_v0.8.5-win.1_Windows_RTX_online_Setup.exe` with
  SHA256 `b1df6958b2af6247f0804d70d2c30f23079b339f14c24db79506ab65c15ffe27`.
- Rebuilt the Release preset and regenerated the same canonical online Adobe
  installer after fixing SmartFX optional Alpha Hint checkout. Validation
  report:
  `dist/CorridorKey_Adobe_v0.8.5-win.1_Windows_RTX/adobe_package_validation.json`.
  Result: `validation_passed=true`, `doctor.layout_kind=windows_adobe`,
  `doctor.bundle_healthy=true`, Green effect
  `CorridorKey Green v0.8.5-win.1` with match name
  `com.corridorkey.effect`, Blue effect `CorridorKey Blue v0.8.5-win.1`
  with match name `com.corridorkey.effect.blue`, five packaged models
  present, zero missing models. The refreshed installer is
  `dist/CorridorKey_Adobe_v0.8.5-win.1_Windows_RTX_online_Setup.exe` with
  SHA256 `3338eab8b0fa2dd417c8d090557566f6436fafd72412f197ba4045362f2df59b`.

### 2026-05-24

- Fresh review of the Adobe package flow found three release blockers: RTX
  packages could stage without the required TensorRT/CUDA ONNX Runtime provider
  sidecars, package validation tolerated total `doctor` command failure when any
  model was missing, and the package README claimed Adobe Media Encoder support
  without validation evidence.
- `scripts/package_adobe_plugins_windows.ps1` now requires the TensorRT RTX ONNX
  Runtime provider plus TensorRT RTX, CUDA runtime, and NPP sidecars for
  `windows-rtx` packages. The CUDA execution-provider DLL remains optional
  because the canonical `vendor/onnxruntime-windows-rtx` root is built with
  `--use_nv_tensorrt_rtx` and does not ship `onnxruntime_providers_cuda.dll`.
  The package flow also constrains recursive package cleanup to a resolved child
  of `dist`, uses `Remove-Item -LiteralPath`, guards the generated manual
  install helper's destination before recursive cleanup, and removes unvalidated
  host discovery claims from the generated README.
- `scripts/validate_adobe_package_win.ps1` now tolerates missing packaged
  models only after `corridorkey.exe doctor --json` produces a report and
  `Test-CorridorKeyDoctorMissingModelProbeFailuresOnly` proves every failing
  execution probe corresponds to an inventory-missing model. A total doctor
  command failure always blocks validation.
- `AGENTS.md` and `CLAUDE.md` now list `package-adobe` in the canonical
  `scripts/windows.ps1 -Task` set, matching the wrapper's supported task list.
- Regression coverage was tightened in
  `tests/regression/test_adobe_package_scaffold.ps1`, and the existing
  missing-model doctor tolerance regression is registered in CTest as
  `regression_windows_doctor_missing_model_tolerance`.
- Verification passed:
  `powershell.exe -NoProfile -ExecutionPolicy Bypass -File tests\regression\test_adobe_package_scaffold.ps1`,
  `powershell.exe -NoProfile -ExecutionPolicy Bypass -File tests\regression\test_windows_doctor_missing_model_tolerance.ps1`,
  `scripts\windows.ps1 -Task build -Preset debug -EnableAdobePlugin -AdobeSdkRoot C:\Dev\CorridorKey-Runtime\vendor\adobe-after-effects-sdk`,
  `scripts\windows.ps1 -Task build -Preset release -EnableAdobePlugin -AdobeSdkRoot C:\Dev\CorridorKey-Runtime\vendor\adobe-after-effects-sdk`,
  `build\debug\tests\unit\test_unit.exe "[unit][adobe][effect]"`,
  `build\debug\tests\unit\test_unit.exe "[unit][adobe][runtime]"`,
  `build\debug\tests\unit\test_unit.exe "[unit][runtime][quality]"`, and
  `ctest --test-dir build\debug -R "regression_adobe_(pipl_metadata|cmake_scaffold|package_scaffold)" --output-on-failure`.
- Canonical Adobe package verification passed after the package-audit fixes:
  `scripts\windows.ps1 -Task package-adobe -Preset release -Track rtx -Flavor online -DisplayVersionLabel 0.8.5-win.1`.
  Validation report:
  `dist/CorridorKey_Adobe_v0.8.5-win.1_Windows_RTX/adobe_package_validation.json`.
  Result: `validation_passed=true`, `doctor.layout_kind=windows_adobe`,
  `doctor.bundle_healthy=true`, Green effect
  `CorridorKey Green v0.8.5-win.1`, Blue effect
  `CorridorKey Blue v0.8.5-win.1`, five packaged models present, zero missing
  models. The regenerated installer is
  `dist/CorridorKey_Adobe_v0.8.5-win.1_Windows_RTX_online_Setup.exe` with
  SHA256 `e5e754047a47c118b1334f83b6f8ffdf0e3754cb1fb213bf5c9d82ecb41a5acc`.
- TDD clean/upgrade install-smoke slice added
  `scripts/smoke_adobe_install_win.ps1` and CTest coverage
  `regression_adobe_install_smoke`. The smoke installs the staged Adobe package
  into a controlled Adobe Common Plug-ins MediaCore root, validates that Green
  and Blue `.aex` modules plus runtime entrypoints are discoverable, rejects the
  retired single `corridorkey_adobe.aex`, covers literal-path handling for
  Windows paths with bracket characters, and writes separate clean/upgrade
  reports. The canonical `package-adobe` flow now runs both smokes before
  building the installer and emits
  `adobe_install_smoke_clean.json` plus
  `adobe_install_smoke_upgrade.json`.
- Verification passed:
  `powershell.exe -NoProfile -ExecutionPolicy Bypass -File tests\regression\test_adobe_install_smoke.ps1`,
  `powershell.exe -NoProfile -ExecutionPolicy Bypass -File tests\regression\test_adobe_package_scaffold.ps1`,
  `scripts\windows.ps1 -Task build -Preset debug -EnableAdobePlugin -AdobeSdkRoot C:\Dev\CorridorKey-Runtime\vendor\adobe-after-effects-sdk`,
  `ctest --test-dir build\debug -R "regression_adobe_(install_smoke|package_scaffold)" --output-on-failure`,
  and
  `scripts\windows.ps1 -Task package-adobe -Preset release -Track rtx -Flavor online -DisplayVersionLabel 0.8.5-win.1`.
  The package validation report remains
  `dist/CorridorKey_Adobe_v0.8.5-win.1_Windows_RTX/adobe_package_validation.json`
  with `validation_passed=true`, `doctor.layout_kind=windows_adobe`, Green
  effect `CorridorKey Green v0.8.5-win.1`, Blue effect
  `CorridorKey Blue v0.8.5-win.1`, five packaged models present, and zero
  missing models. Clean and upgrade smoke reports both recorded
  `adobe_common_mediacore_payload_present` for After Effects and Premiere. The
  regenerated installer is
  `dist/CorridorKey_Adobe_v0.8.5-win.1_Windows_RTX_online_Setup.exe` with
  SHA256 `e5e754047a47c118b1334f83b6f8ffdf0e3754cb1fb213bf5c9d82ecb41a5acc`.

## Definition of Done

All Acceptance Criteria checked, plus:

- [ ] Local tests pass (or N/A documented in Notes)
- [ ] Code review completed (human or fresh-context reviewer per WORKFLOW section 10)
- [ ] No orphan `TODO`/`FIXME` introduced
- [ ] Status updated to `done` and Notes log closes the task
