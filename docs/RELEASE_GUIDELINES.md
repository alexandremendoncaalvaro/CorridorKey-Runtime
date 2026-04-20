# Release Guidelines

This document defines the release procedure for CorridorKey. The goal is a
repeatable build and packaging flow that produces correctly versioned artifacts
from the curated runtime roots without manual file edits.

Release packaging and release messaging serve different purposes. Packaging
defines what artifacts are built. Release messaging defines what support is
claimed. Claims must match packaged and validated product tracks, not every
backend hook present in the codebase.

## 1. Versioning Policy

CorridorKey follows Semantic Versioning (`MAJOR.MINOR.PATCH`).

- **MAJOR**: breaking changes such as incompatible OFX parameter changes,
  removed hardware support, or major structural rewrites.
- **MINOR**: backward-compatible features such as new models, new quality
  options, or performance improvements.
- **PATCH**: backward-compatible fixes such as crash fixes, UI fixes, or
  corrected defaults.

Every release must bump the root `CMakeLists.txt` `VERSION`. That root CMake
version is the single Windows release version source. Runtime GUI metadata is
synchronized from it by the Windows packaging scripts.

### Pre-release labels

Iterative pre-release candidates against a fixed public version use a
platform-qualified SemVer 2.0.0 prerelease identifier, passed via
`-DisplayVersionLabel`:

```powershell
.\scripts\windows.ps1 -Task release -Version 0.7.5 -DisplayVersionLabel 0.7.5-win.22
```

The label is baked into the OFX panel, the CLI `--version` report, the
runtime-server log filename (`ofx_runtime_server_v<label>.log`), and the
dist artifact filenames so the operator never has to guess which build is
installed. Public releases (no pre-release cycle in progress) are cut
without `-DisplayVersionLabel`; `CMakeLists.txt` `VERSION` is authoritative
and artifact names match it exactly.

**Tag format**. The Git tag, the display label, and the artifact filenames
all use the same string:

- Windows prerelease: `vX.Y.Z-win.N`
- macOS prerelease: `vX.Y.Z-mac.N`
- Linux prerelease: `vX.Y.Z-linux.N`
- Stable (all platforms): `vX.Y.Z` with no suffix

The platform identifier (`win`, `mac`, `linux`) is required on every
prerelease tag. A suffix-only tag like `v0.7.5-22` is not valid and will be
rejected by the publishing pipeline. The auto-updater in
[version_check.cpp](../src/app/version_check.cpp) filters releases by this
identifier so a Windows user on `v0.7.5-win.22` never receives a macOS
prerelease and vice versa. Stable releases without a suffix apply to all
platforms universally.

**Per-platform counters**. Each platform maintains its own independent
counter (`N` above). Windows and macOS iterate at whatever cadence their
track demands; they do not share or coordinate the counter.

**Numbering rules for a new cycle** (applied per platform):

- The counter does not restart when a pre-release trajectory is reverted
  or abandoned. If the last shipped pre-release of `X.Y.Z-<platform>`
  was `.4`, the next cycle's first candidate starts at `.10` or higher,
  not `.1`. Jumping over the prior range makes it unambiguous in log
  files, screenshots, and bug reports that a build is on the new cycle,
  and avoids operator confusion between a `.1` that was the original
  start and a `.1` that was a reboot.
- Use round jumps between cycles (`.10`, `.20`, `.30`) so a counter like
  `-win.12` immediately reads as "second candidate of the Windows cycle
  that started at `.10`". This matches how the optimization measurement
  ledger labels its `phase_9_*` track.
- Always record the pre-release label in
  [OPTIMIZATION_MEASUREMENTS.md](OPTIMIZATION_MEASUREMENTS.md) with the
  measured numbers at the time of cut, even if the build is later
  abandoned — the ledger is the cross-session memory of the track.

### Tag and release immutability

A published tag is immutable. Once a release is pushed to GitHub, its
assets must never be replaced, re-uploaded, or re-pointed to a different
commit. A build that needs to be redone gets a new tag with the next
counter value. This rule is absolute: the SemVer comparator in
[version_check.cpp](../src/app/version_check.cpp) trusts the tag name as
ground truth, so mutating a published tag's assets breaks every installed
client that cached a stale URL from it.

This also forbids hosting multiple platforms' assets under a single
shared tag (for example, Windows `-win.22` alongside macOS `-mac.10`
under one `v0.7.5` release). Each platform-qualified tag owns exactly
its platform's assets.

### GitHub release publishing flags

The canonical Windows release pipeline calls `gh release create` with
these flags, derived from the tag shape:

- Tag with `-<platform>.N` suffix → `--prerelease`
- Tag with no suffix → `--latest`

Stable release `vX.Y.Z` is published only after every active platform
track has shipped its final prerelease for that `X.Y.Z` cycle.

## 2. Standardized Artifact Naming

Artifact names must expose both the version and the backend-specific hardware
path so users can choose the correct installer without ambiguity.

- All artifacts include the version number and target OS.
- Backend-bound Windows artifacts include the backend in the filename.
- Do not reuse a generic filename for a backend-specific build.

### Windows Installers

Generated by the canonical Windows release wrapper, which delegates to the
packaging scripts internally.

- TensorRT RTX: `CorridorKey_Resolve_vX.Y.Z_Windows_RTX_Installer.exe`
- DirectML: `CorridorKey_Resolve_vX.Y.Z_Windows_DirectML_Installer.exe`

### macOS Installers

- Apple Silicon: `CorridorKey_Resolve_vX.Y.Z_macOS_Silicon_Installer.pkg`

### Linux Installers

Linux is an experimental product track. Packaging emits two installer wrappers
around the same validated `CorridorKey.ofx.bundle` payload, one per
distribution family, so each user installs through the idiomatic package
manager of the host distribution.

- Debian package (Ubuntu 22.04 / 24.04 LTS): `CorridorKey_Resolve_vX.Y.Z_Linux_RTX.deb`
- RPM package (Rocky Linux / RHEL 9): `CorridorKey_Resolve_vX.Y.Z_Linux_RTX.rpm`

Both artifacts share the same embedded `CorridorKey.ofx.bundle`,
`bundle_validation.json`, and model inventory. They differ only in how they
install the bundle under `/usr/OFX/Plugins/` and how they manage the
`/usr/local/bin/corridorkey` symlink.

### One installer per platform

Release assets are installers only. Portable archives (`.zip` for Windows,
`.tar.gz` for Linux) are not produced or published. Every supported
platform has exactly one installer artifact per track, matching the
filenames above. A user on an unsupported distro or locked-down Windows
host is not a target; do not add fallback archives to accommodate them.

## 3. Build and Packaging Process

Releases must be built through the canonical repo wrapper so version
synchronization, packaged runtime selection, artifact naming, and validation
stay consistent.

### Windows Prerequisites

The canonical Windows pipeline auto-stages every dependency that can be
downloaded from a pinned URL. The table below lists what the operator
still has to install manually; everything else is fetched on demand by
`scripts\windows.ps1 -Task prepare-rtx` from a clean clone.

| Tool | Expected location / detection | Notes |
|---|---|---|
| Git for Windows | on `PATH` | auto-discovered by the helper scripts as a fallback when not on `PATH` |
| Visual Studio 2022 | Community, Pro, or Enterprise with the "Desktop development with C++" workload and the Windows 10/11 SDK | `vcvars64.bat` must be locatable under `C:\Program Files\Microsoft Visual Studio\2022\*`; the pipeline injects the MSVC dev shell on demand |
| CUDA Toolkit 12.8 | default location `C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v12.8` | or pass `-CudaHome <path>` through `-ForwardArguments` |
| vcpkg | `C:\tools\vcpkg` with `VCPKG_ROOT` pointing at it | pinned baseline is in `vcpkg-configuration.json`; every shell that calls the pipeline must export `VCPKG_ROOT` |
| Python 3.12 | default per-user install from python.org | auto-discovered by `Resolve-CorridorKeyPython312Path`; the pin lives in `Get-CorridorKeyWindowsRtxBuildContract.required_python_version` |
| uv | `%USERPROFILE%\.local\bin\uv.exe` (default installer path) | auto-discovered via `Resolve-CorridorKeyUvPath`; install once with `irm https://astral.sh/uv/install.ps1 \| iex` |
| NSIS 3.x | default install at `C:\Program Files (x86)\NSIS\` | auto-discovered |

What the pipeline handles for the operator:

- **TensorRT-RTX SDK:** auto-downloaded from the pinned URL in
  `Get-CorridorKeyWindowsRtxBuildContract.tensorrt_rtx_download_url`
  and extracted into `vendor\TensorRT-RTX-<version>\` on first
  `prepare-rtx`. No manual staging needed.
- **OpenFX SDK:** auto-cloned at the pinned tag from
  `AcademySoftwareFoundation/openfx` into `vendor\openfx\`.
- **ONNX Runtime source:** bootstraps `vendor\onnxruntime-src\` at
  the pinned ref when absent.
- **vcpkg eigen3 archive (blocked by Cloudflare on gitlab):**
  `scripts\vcpkg_asset_fetch.ps1` is wired in as a vcpkg
  `X_VCPKG_ASSET_SOURCES=x-script,...` that transparently redirects
  the `libeigen/eigen/<commit>` fetch to the byte-identical
  `eigen-mirror/eigen` GitHub mirror.
- **Models:** the seven FP16 + INT8 runtime artifacts under
  `models\` are the reuse-path source of truth. If all expected
  files are present the pipeline skips regeneration entirely; if
  one or more are missing the pipeline exports them from
  `models\CorridorKey.pth` via `uv run python export_onnx.py`.

If any manual row above is not satisfied, stop and fix it. Do not
invent workarounds from outside the canonical pipeline; they will
diverge from what the pipeline produces and from what users install.
See [docs/WINDOWS_BUILD.md](WINDOWS_BUILD.md) section 3 for the
troubleshooting index of every failure mode the auto-stage paths have
been validated against.

### Windows Build Steps

Windows has two curated runtime roots and one canonical release entrypoint:

- `vendor\onnxruntime-windows-rtx` - curated TensorRT RTX runtime
- `vendor\onnxruntime-windows-dml` - curated DirectML runtime
- `scripts\windows.ps1` - canonical Windows build and release entrypoint

Do not use global ONNX Runtime installations or
`vendor\onnxruntime-universal` as a fallback path. The release flow must
resolve one of the curated runtime roots explicitly.

The canonical Windows release command is:

- `scripts\windows.ps1 -Task release -Version X.Y.Z`

That canonical command generates the official Windows `RTX` installer by
default. Experimental Windows tracks must be requested explicitly:

- `scripts\windows.ps1 -Task release -Version X.Y.Z -Track dml`
- `scripts\windows.ps1 -Task release -Version X.Y.Z -Track all`

The wrapper also supports `build`, `prepare-rtx`, `package-ofx`,
`package-runtime`, and `sync-version` for local workflow needs, but they are
all part of the same entrypoint. Lower-level scripts are internal delegates
for debugging the wrapper and should not be treated as alternate release
procedures.

The Windows wrapper tasks are intentionally different:

- `build`
  - compile the binaries only
- `certify-rtx-artifacts`
  - certify an already existing Windows RTX model set and write the certified
    artifact manifest without regenerating the `.onnx` files from the
    checkpoint
- `package-ofx`
  - package Windows installers from an already certified model set
- `release`
  - package the official Windows release tracks from the currently staged,
    validated inputs
- `regen-rtx-release`
  - regenerate the Windows RTX artifacts from the checkpoint, certify them on
    the active RTX host, write the certified artifact manifest, and then
    package the Windows RTX installer

The supported repo-local runtime locations are only
`vendor\onnxruntime-windows-rtx` and `vendor\onnxruntime-windows-dml`.

1. Prepare the curated RTX runtime when it is not already staged:
   ```powershell
   .\scripts\windows.ps1 -Task prepare-rtx
   ```
2. Run the canonical Windows release flow:
   ```powershell
   .\scripts\windows.ps1 -Task release -Version X.Y.Z
   ```
3. Only request experimental Windows tracks intentionally:
   ```powershell
   .\scripts\windows.ps1 -Task release -Version X.Y.Z -Track dml
   .\scripts\windows.ps1 -Task release -Version X.Y.Z -Track all
   ```

Scripts validate the runtime root. If `-OrtRoot` does not map to the expected
curated runtime track, packaging aborts.

For Windows RTX, `package-ofx` is not a model regeneration command. It is a
strict packaging command that expects a certified artifact set. The RTX
packaging flow now requires:

- the packaged `corridorkey_fp16_*.onnx` and `*_ctx.onnx` files
- a matching `artifact_manifest.json` written from the certification report

If that manifest is absent or does not match the packaged RTX artifacts,
packaging fails intentionally. This prevents the project from generating a new
installer from stale or manually copied RTX models.

### Windows Anti-Patterns

Every one of the workarounds below has produced a regression in
production. They are listed here so contributors (human or AI) stop
reinventing them.

- **Do not call `scripts\build.ps1`, `scripts\prepare_windows_rtx_release.ps1`,
  or `scripts\release_pipeline_windows.ps1` directly.** They are internal
  delegates. Always go through `scripts\windows.ps1 -Task ...`. Direct
  calls skip version-metadata sync, track resolution, and validation
  that the wrapper applies.
- **Do not create git worktrees that shadow `vendor\`.** A worktree
  inherits a tracked `vendor\` with `.gitkeep` stubs, and running `git
  worktree remove --force` on a worktree containing a Windows junction
  into the main `vendor\` has, in practice, followed the junction and
  erased the real binaries. If a second working copy is needed, use
  `git worktree add -B <branch> <path>` without touching `vendor\` and
  stage the curated runtimes separately for that worktree.
- **Do not skip the quality gate on the release pipeline.** Running
  `scripts\windows.ps1 -Task release` with `-SkipTests` forwarded through
  `-ForwardArguments` is a debug convenience only. Any build intended
  for a user — even an internal pre-release — must pass the quality
  gate.
- **Do not touch the render hot path without measuring.** Any change
  under `src/plugins/ofx/`, `src/core/inference_session.cpp`,
  `src/core/engine.cpp`, `src/core/gpu_prep.cpp`, `src/core/gpu_resize.cpp`,
  or `src/post_process/` must be measured against the
  `phase_8_gpu_prepare` baseline recorded in
  `docs/OPTIMIZATION_MEASUREMENTS.md`. Use `scripts/run_corpus.sh` then
  `scripts/compare_benchmarks.py`; reject the change if
  `avg_latency_ms` or `ort_run` regresses by more than 10%.

### Windows Release Label Plumbing

`-DisplayVersionLabel` is the one mechanism that plumbs a human-readable
version string into every user-visible surface on Windows. It flows
through CMake into `include/corridorkey/version.hpp`
(`CORRIDORKEY_DISPLAY_VERSION_STRING`), which the OFX panel, the
`corridorkey --version` CLI, and the runtime-server log filename
(`ofx_runtime_server_v<label>.log`) all read. The packaging scripts
also bake the label into the dist artifact names when present:
`CorridorKey_Resolve_v<label>_Windows_RTX_Installer.exe`,
`CorridorKey_Resolve_v<label>_Windows_RTX.zip`,
`CorridorKey_Resolve_v<label>_Windows_RTX\\`. On Windows the label must
be of the form `X.Y.Z-win.N` (platform-qualified). Public releases drop
the flag; `CMakeLists.txt` `VERSION` is authoritative and artifact names
match it exactly. The numbering rule (counters do not restart across
reverted cycles; jump to `.10`, `.20`, ...) is documented in
section 1 "Pre-release labels".

Use the following commands according to the state you have:

1. You only need to build binaries:
   ```powershell
   .\scripts\windows.ps1 -Task build -Preset release
   ```
2. You already have a certified RTX artifact set and only want the installers:
   ```powershell
   .\scripts\windows.ps1 -Task package-ofx -Version X.Y.Z -Track rtx
   ```
3. You already have a local Windows RTX model set and need to certify it
   before packaging:
   ```powershell
   .\scripts\windows.ps1 -Task certify-rtx-artifacts -Version X.Y.Z
   ```
4. You need to regenerate and certify the RTX ladder from the checkpoint:
   ```powershell
   .\scripts\windows.ps1 -Task regen-rtx-release -Version X.Y.Z
   ```

### Windows Model Availability Policy

Windows build and release artifacts may be packaged with a partial model set
when some packaged artifacts are intentionally absent or temporarily
unavailable.

- Missing packaged models do not block bundle or installer generation by
  themselves.
- Packaging must emit explicit inventory and validation reports for every
  generated Windows distribution artifact.
- OFX bundles must include `model_inventory.json`.
- Windows OFX release folders must include `bundle_validation.json`.
- Missing packaged models must remain explicit in generated reports and must
  surface as normal runtime or plugin errors when the missing quality is
  requested.
- Invalid packaged models that are present still fail validation. Partial model
  coverage is allowed; silently shipping broken artifacts is not.

### Windows RTX Track Policy

Windows RTX now ships as a single installer that replaces the same OFX bundle
path during installation.

- `Windows RTX` packages the public FP16 ladder through `2048px` plus the
  portable INT8 CPU artifacts.
- `Auto` continues to respect the safe quality ceiling of the active GPU tier.
- Manual fixed quality may attempt a higher packaged rung directly and then
  follow the established runtime failure path if that quality cannot execute.

The Windows RTX installer must pass packaged `doctor` validation before it is
considered releasable.

## 4. Support Claims in Release Assets

Release names, GitHub release text, installer labels, and download guidance
must match the support matrix. A packaged track may exist without being an
officially supported hardware family.

- The Windows `RTX` artifact is the supported Windows track for NVIDIA RTX 30
  series and newer.
- The canonical public Windows release produces the Windows RTX installer
  unless another track is requested explicitly.
- The Windows `DirectML` artifact is an experimental Windows track. It must
  not be published by default or described as official support for all AMD,
  Intel, or RTX 20 series hardware.
- Apple Silicon is the official macOS track.
- The `Linux RTX` artifacts (`.tar.gz`, `.deb`, `.rpm`) are experimental.
  Release copy must state the Experimental designation and must not imply
  parity with the Windows RTX track. All three Linux artifacts share the
  same validated bundle and are part of the same experimental track; they
  differ only as distribution wrappers.
- Do not turn a backend name into a broad hardware promise. When in doubt,
  point users to `help/SUPPORT_MATRIX.md`.

## 5. GitHub Release Publishing

GitHub release titles must stay consistent so users can identify the platform
and artifact type quickly. Release notes are mandatory and follow the
template below. The canonical pipeline
(`scripts\release_pipeline_windows.ps1`) refuses to publish a release
without a notes file containing the required sections, so there is no
path to shipping a placeholder like "Auto-published by ...".

Before publishing, write the notes to `build/release_notes/v<tag>.md`
(for example `build/release_notes/v0.7.6-win.1.md`). When
`-PublishGithub` is passed through `scripts\windows.ps1 -Task release`,
the pipeline picks that file up automatically if it exists at the
convention path; otherwise pass `-ForwardArguments '-PublishGithub','-NotesFile','<path>'`
explicitly.

### CorridorKey Resolve OFX

- Windows only: `CorridorKey Resolve OFX vX.Y.Z (Windows)`
- macOS only: `CorridorKey Resolve OFX vX.Y.Z (macOS) - Apple Silicon`
- Linux only: `CorridorKey Resolve OFX vX.Y.Z (Linux)`
- Windows and macOS: `CorridorKey Resolve OFX vX.Y.Z (Windows & macOS)`
- Windows and Linux: `CorridorKey Resolve OFX vX.Y.Z (Windows & Linux)`
- Windows, macOS, and Linux: `CorridorKey Resolve OFX vX.Y.Z (Windows, macOS & Linux)`

### CorridorKey Runtime

- Windows only: `CorridorKey Runtime vX.Y.Z (Windows)`
- macOS only: `CorridorKey Runtime vX.Y.Z (macOS)`
- Linux only: `CorridorKey Runtime vX.Y.Z (Linux)`
- Windows and macOS: `CorridorKey Runtime vX.Y.Z (Windows & macOS)`
- Windows, macOS, and Linux: `CorridorKey Runtime vX.Y.Z (Windows, macOS & Linux)`

Use the following release description template:

```markdown
## Overview
[A concise 1-2 sentence description of what this release introduces.]

## Changelog
### Added
- [Feature A]
- [Feature B]

### Changed
- [Modification A]

### Fixed
- [Bugfix A]

## Assets & Downloads

### Windows
- **NVIDIA RTX 30 Series or newer:** Download `CorridorKey_Resolve_vX.Y.Z_Windows_RTX_Installer.exe`.
- **Windows DirectML track (experimental):** Download `CorridorKey_Resolve_vX.Y.Z_Windows_DirectML_Installer.exe`.
- Do not describe the DirectML installer as official support for every AMD, Intel, or RTX 20 series GPU family. Refer readers to `help/SUPPORT_MATRIX.md` for the real support designation.

### macOS
- Include this section only when the release contains a macOS installer.
- **Apple Silicon (M-Series):** Download `CorridorKey_Resolve_vX.Y.Z_macOS_Silicon_Installer.pkg`.

### Linux
- Include this section only when the release contains a Linux artifact.
- **Ubuntu 22.04 / 24.04 LTS (experimental):** Download `CorridorKey_Resolve_vX.Y.Z_Linux_RTX.deb`.
- **Rocky Linux / RHEL 9 (experimental):** Download `CorridorKey_Resolve_vX.Y.Z_Linux_RTX.rpm`.
- Do not describe the Linux artifacts as official support. Refer readers to `help/SUPPORT_MATRIX.md` for the real support designation. A proprietary NVIDIA driver 555 or newer is required; the CUDA Toolkit is not required.

## Installation Instructions

1. Close DaVinci Resolve if it is running.
2. Run the downloaded installer.
3. The installer automatically overwrites the previous version.
4. Launch DaVinci Resolve and load the plugin from the OpenFX Library.

## Uninstallation
To remove the plugin, go to **Windows Settings > Apps > Installed apps**, search
for "CorridorKey Resolve OFX", and click Uninstall.

## Known Issues
- [List any currently tracked critical issues the user might face.]
- [If there are no known issues, omit this section entirely.]
```
