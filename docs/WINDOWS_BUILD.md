# Windows Build Guide

This document defines the end-to-end flow for building the Windows RTX
distribution of CorridorKey Runtime from a clean clone. Windows is the most
involved target because the release requires a custom ONNX Runtime build with
the TensorRT RTX execution provider, which is not published as a pre-built
binary by Microsoft, NVIDIA, or any other distributor as of ORT 1.23.x
([reference](https://onnxruntime.ai/docs/execution-providers/TensorRTRTX-ExecutionProvider.html)).
The canonical pipeline owns the full chain: source fetch, ORT build, model
staging, CorridorKey compile, and installer packaging.

**See also:**
[CONTRIBUTING.md](../CONTRIBUTING.md) — development setup across platforms |
[RELEASE_GUIDELINES.md](RELEASE_GUIDELINES.md) — versioning and distribution policy |
[ARCHITECTURE.md](ARCHITECTURE.md) — source structure

## 1. Prerequisites

The canonical pipeline auto-downloads one dependency (the TensorRT-RTX SDK) and
clones one (the OpenFX SDK). Everything else must be installed by the operator.

| Tool | Pinned version | Notes |
|---|---|---|
| Visual Studio 2022 | Community / Professional / Enterprise | Must include the `Desktop development with C++` workload. The pipeline auto-detects the install via `vswhere.exe` and activates the MSVC dev shell on demand. |
| CMake | `3.28+` | The pinned minimum lives in `Get-CorridorKeyWindowsRtxBuildContract.minimum_cmake_version`. |
| Python | `3.12` (exact) | Required by the ORT build's own helpers and by the model exporter. The pinned version lives in `Get-CorridorKeyWindowsRtxBuildContract.required_python_version`. Installing from [python.org](https://www.python.org/downloads/) into the default per-user path is enough — the pipeline resolves it via `Resolve-CorridorKeyPython312Path`. |
| uv | latest | Python dependency manager used by the model exporter. Install with `irm https://astral.sh/uv/install.ps1 \| iex`. The installer drops `uv.exe` under `%USERPROFILE%\.local\bin\` — the pipeline looks there even when it is not on `PATH`. |
| CUDA Toolkit | `12.9` (mandatory) | Required for CUDA headers, `nvcc`, and the NPP DLLs the OFX bundle ships. The contract pins `required_cuda_version = 12.9` because TensorRT-RTX 1.2.0.54 only ships against CUDA 12.9 and 13.x ([NVIDIA prerequisites](https://docs.nvidia.com/deeplearning/tensorrt-rtx/latest/installing-tensorrt-rtx/prerequisites.html)) and CUDA Minor Version Compatibility is forward-only ([NVIDIA compatibility guide](https://docs.nvidia.com/deploy/cuda-compatibility/minor-version-compatibility.html)) — building against 12.8 produces `ERROR_DLL_INIT_FAILED` (1114) inside `onnxruntime_providers_nv_tensorrt_rtx.dll` at host load time. Install to the default path `C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v12.9\`. Coexists with an existing 12.8 install. |
| vcpkg | manifest mode | Clone `microsoft/vcpkg`, run `bootstrap-vcpkg.bat`, then export `VCPKG_ROOT=<path>`. Prefer a short path such as `C:\tools\vcpkg` to avoid Windows long-path issues in vcpkg build output. |
| NSIS | `3.x` | Required to build the installer. Default install location (`C:\Program Files (x86)\NSIS\`) is auto-discovered. |
| Git for Windows | latest | Needed by the OpenFX SDK shallow-clone step and the ORT source checkout. |

All of these are available on a stock Windows 11 developer box once the
operator installs Visual Studio 2022 with the C++ workload. Nothing else is
download-gated.

## 2. Canonical build flow

The **only** supported Windows entrypoint is `scripts\windows.ps1`. Every
lower-level script (`build.ps1`, `prepare_windows_rtx_release.ps1`,
`build_ort_windows_rtx.ps1`, `release_pipeline_windows.ps1`, etc.) is an
internal delegate — call them directly only when debugging the wrapper.

### 2.1 First-time setup

```powershell
# Required by CMakePresets.json and by the ORT source build.
$env:VCPKG_ROOT = "C:\tools\vcpkg"

# Prepare the curated RTX runtime and the CorridorKey binaries. Idempotent —
# on subsequent runs it reuses an existing vendor/onnxruntime-windows-rtx
# staging if it is valid, so this is the "once per pin" long step.
.\scripts\windows.ps1 -Task prepare-rtx
```

What `prepare-rtx` does:

1. Validates or auto-stages the TensorRT-RTX SDK into `vendor\TensorRT-RTX-<version>\`.
2. Resolves the ONNX Runtime source tree at `vendor\onnxruntime-src` (must already be present as a git checkout at `v1.23.0`; clone manually if missing).
3. Reuses the prepared model set from `models\` if all seven expected artifacts are present (otherwise regenerates via `uv run` on the model exporter).
4. Validates the models load cleanly on the onnxruntime CPU EP (`validate_model_pack.py`).
5. Builds ONNX Runtime from source with `--use_nv_tensorrt_rtx` and stages the result into `vendor\onnxruntime-windows-rtx\`.
6. Auto-clones the pinned OpenFX SDK tag into `vendor\openfx\`.
7. Activates the MSVC environment and builds the CorridorKey C++ tree (library, CLI, OFX plugin, tests).
8. (Optional) Packages the portable runtime bundle — skipped when the Tauri GUI binary is absent, since that path belongs to the `package-runtime` track.

Expected completion time on a fresh clone: **45 min – 2 h**, dominated by the ORT source build. Subsequent `prepare-rtx` runs that reuse the staged ORT finish in under a minute.

### 2.2 Public release

```powershell
$env:VCPKG_ROOT = "C:\tools\vcpkg"

# Public release. Produces CorridorKey_Resolve_v0.7.5_Windows_RTX_Installer.exe
# plus the matching zip and bundle_validation.json.
.\scripts\windows.ps1 -Task release -Version 0.7.5
```

### 2.3 Pre-release

```powershell
# Pre-release candidate. Bakes the label into the runtime, the panel, and the
# log filename without bumping the root CMakeLists.txt version.
.\scripts\windows.ps1 -Task release -Version 0.7.5 -DisplayVersionLabel 0.7.5-10
```

See [RELEASE_GUIDELINES.md](RELEASE_GUIDELINES.md) section "Pre-release labels" for the numbering policy.

## 3. Troubleshooting

The canonical pipeline hits the issues below under real conditions. Each one
now resolves itself without operator intervention, but the root causes are
documented here so a new developer can trust the fix or recognize recurrences
in a different pin.

### 3.1 `vswhere.exe` is not recognized

**Symptom.** The ORT source build aborts with
`'vswhere.exe' não é reconhecido como um comando interno` (or the
English equivalent) right after `VsDevCmd.bat -arch=x64` prints its
banner.

**Root cause.** `vswhere.exe` ships under
`%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\`, which is
not on `PATH` by default. Something vcpkg invokes during its bootstrap
phase expects `vswhere` to be directly callable.

**Fix in-tree.** `build_ort_windows_rtx.ps1` prepends the Installer
directory to `PATH` at the PowerShell process level before calling
`cmd.exe /c`. The child `cmd.exe`, `VsDevCmd.bat`, and `build.py`
inherit the extended `PATH` without having to re-`set` it inside the
cmd chain (which has its own parse surprises when the path contains
`(x86)`).

### 3.2 `eigen3` download fails with HTTP 403 from gitlab.com

**Symptom.** The ORT source build aborts during `vcpkg install eigen3`
with:

```
error: https://gitlab.com/libeigen/eigen/-/archive/<sha>/...tar.gz: failed: status code 403
cf-mitigated: challenge
```

**Root cause.** GitLab puts the raw archive URL behind a Cloudflare
bot challenge for many non-browser clients. The stock vcpkg `eigen3`
port still pulls from gitlab (unlike upstream ONNX Runtime's own
`FetchContent` path, which [migrated to `github.com/eigen-mirror/eigen`](https://github.com/microsoft/onnxruntime/issues/24861)
for this exact reason).

**Fix in-tree.** `build_ort_windows_rtx.ps1` sets
`X_VCPKG_ASSET_SOURCES=x-script,<scripts\vcpkg_asset_fetch.ps1>`
before spawning the ORT build. The script rewrites
`gitlab.com/libeigen/eigen/-/archive/<sha>/...tar.gz` to
`codeload.github.com/eigen-mirror/eigen/tar.gz/<sha>` (byte-identical
content, so the pinned SHA512 still matches) and passes every other
URL through unchanged. Documented at
[Microsoft vcpkg asset caching](https://learn.microsoft.com/en-us/vcpkg/users/assetcaching).

**When this might recur.** A future ORT pin that bumps the Eigen
commit. The fix is SHA-generic (it reads the commit from the URL),
so the mirror still works without any code change as long as the
upstream and the `eigen-mirror/eigen` GitHub mirror stay in sync.

### 3.3 "OpenFX SDK not found at vendor/openfx"

**Symptom.** The CorridorKey cmake configure step in `prepare-rtx`
fails with:

```
OpenFX SDK not found at C:/Dev/CorridorKey-Runtime/vendor/openfx.
Run: git clone https://github.com/AcademySoftwareFoundation/openfx vendor/openfx
```

**Root cause.** `vendor/openfx/` is gitignored, so a fresh clone has
no OpenFX SDK on disk.

**Fix in-tree.** `prepare_windows_rtx_release.ps1` calls
`Ensure-CorridorKeyOpenFxSdk` immediately before the CorridorKey
configure step, which shallow-clones the pinned OpenFX tag from
`Get-CorridorKeyWindowsRtxBuildContract.openfx_git_ref`.

### 3.4 `cstdint` / `chrono` not found during CorridorKey build

**Symptom.** Every `.cpp` fails its dependency scan with
`fatal error C1083: Cannot open include file: 'cstdint'` or similar
STL headers, even though `cl.exe` is on `PATH`.

**Root cause.** cmake is spawning `cl.exe` without the MSVC `INCLUDE`
and `LIB` variables set — the compiler runs but has no STL to
include.

**Fix in-tree.** Both `build.ps1` and
`prepare_windows_rtx_release.ps1` call
`Initialize-CorridorKeyMsvcEnvironment` before the cmake configure
step. The helper lives in `windows_runtime_helpers.ps1` and runs
`Launch-VsDevShell.ps1 -Arch amd64` once per session, then no-ops.

### 3.5 `uv was not found`

**Symptom.** The model-preparation step (fresh clone or
`-ForceModelPreparation`) aborts with
`uv was not found. Install uv or pass -Uv`.

**Root cause.** `Resolve-CommandPath` in
`prepare_windows_rtx_release.ps1` previously only checked `PATH` via
`Get-Command`. `uv` installs to `%USERPROFILE%\.local\bin\` by default,
which is not added to user `PATH` by the installer.

**Fix in-tree.** `Resolve-CommandPath` now falls back to the
documented well-known install paths for `uv`, `git`, and `makensis`
via the shared `Resolve-CorridorKey*Path` helpers in
`windows_runtime_helpers.ps1`. No operator action needed.

### 3.6 Installer fails with sharing violation on reinstall

**Symptom.** Re-running the installer (or installing a different
version over an existing install) fails when replacing
`CorridorKey.ofx.bundle`, complaining about locked files.

**Root cause.** `corridorkey_ofx_runtime_server.exe` (and sometimes
`corridorkey.exe`) keep running in the background even after
DaVinci Resolve closes, holding the bundle DLLs mapped.

**Fix in-tree.** The NSIS install section now taskkills
`corridorkey_ofx_runtime_server.exe` and `corridorkey.exe` in
addition to `Resolve.exe` before replacing bundle files. The
uninstall section does the same. Log output from `taskkill` exit
code 128 (process not running) is expected and ignored.

### 3.7 Per-version logs missing after install

**Symptom.** The
`%LOCALAPPDATA%\CorridorKey\Logs\ofx_runtime_server_v<X.Y.Z>.log`
files from a previous install are gone after running the installer.

**Root cause.** Earlier installer revisions cleared the entire logs
directory on install. The release pipeline did the same on every
build. Both wipes were redundant because log filenames already
embed the version — they never collide across installs.

**Fix in-tree.** Both the installer and
`release_pipeline_windows.ps1` no longer touch the logs directory.
Logs accumulate across installs and can be compared version-over-
version for the optimization ledger.

## 4. Running the local benchmark

`tests/integration/ofx_benchmark_harness.exe` drives the same
`OfxSessionBroker` surface the OFX plugin uses to talk to the runtime,
so it exercises the prepare_session / render_frame / release_session
lifecycle without needing DaVinci Resolve on the machine. This is the
authoritative local reproduction tool for optimization measurements
and regression tracking — when comparing two versions, run both
through the harness with the same flags and diff the JSON output.

### 4.1 Synthetic mode (default)

Black zero-filled shared-transport buffers, useful for smoke tests
and short warm-up measurements:

```powershell
.\build\release\tests\integration\ofx_benchmark_harness.exe `
    --model models\corridorkey_fp16_2048.onnx `
    --resolution 2048 --frame-width 3840 --frame-height 2160 `
    --iterations 20 --device rtx --io-binding on
```

### 4.2 Video mode — real 4K input

Drives the session with decoded pairs of frames from two MP4 clips
(`--input-video` supplies the RGB content, `--hint-video` supplies a
grayscale alpha matte). Frame dimensions auto-populate from the RGB
clip; both flags are required together. Each reader loops
independently on EOF so `--iterations` can exceed the source length.

```powershell
.\build\release\tests\integration\ofx_benchmark_harness.exe `
    --model models\corridorkey_fp16_2048.onnx `
    --resolution 2048 --iterations 60 --device rtx --io-binding on `
    --input-video assets\video_samples\Jordan4k.mp4 `
    --hint-video  assets\video_samples\Jordan4k_alphahint.mp4
```

Use video mode whenever the question is "does this version hold its
throughput across a long session?" — 60 iterations with real inputs
is the minimum bar for exposing in-session drift and is the pattern
the optimization ledger relies on when a new `phase_N_*` checkpoint
claims a change is safe.

### 4.3 Reading the output

The harness emits a single JSON document on stdout. Redirect to a
file and key fields:

- `avg_latency_ms`, `fps` — aggregate across all iterations.
- `stage_timings[]` — aggregated per-stage breakdown (the ledger's
  historical format; `compare_benchmarks.py` expects this).
- `per_frame_timings[]` — per-iteration view: `iteration`,
  `roundtrip_ms`, and a `stages` object with the broker's
  stage-timing breakdown for that single frame. Use this to spot
  progressive degradation within a single session (cold-frame spike
  aside, steady-state numbers should be flat — if `ort_run` climbs
  frame-over-frame in the harness, investigate before shipping).

## 5. What NOT to do

- Do not call the lower-level scripts (`build.ps1`,
  `prepare_windows_rtx_release.ps1`, etc.) directly as your normal flow —
  they are internal delegates. Use `scripts\windows.ps1 -Task <task>`.
- Do not hand-download dependencies to bypass a failing pipeline step. If the
  canonical flow reports a missing dependency it is almost always a gap in
  the scripts that we should fix in-tree rather than work around.
- Do not commit `vendor/onnxruntime-windows-rtx/` or `vendor/openfx/` — both
  are gitignored for good reason.
- Do not bump the ORT pin (`ort_source_ref` in
  `Get-CorridorKeyWindowsRtxBuildContract`) without re-testing the full
  `prepare-rtx` flow on a clean clone. A pin bump typically drags along
  vcpkg dependency bumps, any of which can hit a new download issue.
