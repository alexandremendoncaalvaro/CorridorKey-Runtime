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

Two distinct identifier kinds exist and must not be confused. Both are
derived mechanically from Git state; neither uses a hand-maintained
counter file.

| Kind | Who sees it | How it is produced | When it changes |
|---|---|---|---|
| **Published tag** | GitHub Releases consumers, auto-updater | `vX.Y.Z-<platform>.N`, written by the publishing pipeline | Only when a release is published to GitHub |
| **Local build label** | OFX panel, CLI `--version`, runtime-server log filename, dist artifact filenames | Output of `git describe --tags --dirty --match "v*-<platform>.*"` (or `--match "v*"` for stable) | Every commit; also flips to `-dirty` on uncommitted changes |

This split follows the pattern used by the Linux kernel, Go, Rust
nightly, Kubernetes, and most projects that ship a binary carrying a
version string. The published tag is the rare, human-curated event; the
local label is the always-on, self-maintaining identifier that answers
"which build is loaded in my Resolve right now?" without ever inflating
the release list.

**Published tag format**. Every tag pushed to GitHub uses this shape:

- Windows prerelease: `vX.Y.Z-win.N`
- macOS prerelease: `vX.Y.Z-mac.N`
- Linux prerelease: `vX.Y.Z-linux.N`
- Stable (all platforms): `vX.Y.Z` with no suffix

The platform identifier (`win`, `mac`, `linux`) is required on every
prerelease tag. A suffix-only tag like `v0.7.5-22` is not valid and will
be rejected by the publishing pipeline. The auto-updater in
[version_check.cpp](../src/app/version_check.cpp) filters releases by
this identifier so a Windows user on `v0.7.5-win.22` never receives a
macOS prerelease and vice versa. Stable releases without a suffix apply
to all platforms universally.

**Per-platform counters**. Each platform maintains its own independent
counter `N`. Windows and macOS iterate at whatever cadence their track
demands; they do not share or coordinate the counter. `N` is the count
of **published** prereleases for that `X.Y.Z-<platform>` cycle — it is
never incremented by a local build, so it grows slowly and predictably
(typically 1 to 5 per cycle, matching how Kubernetes, Node.js, and
Python number their RCs).

**Local build label format**. When a build is produced without
`-DisplayVersionLabel`, the pipeline derives the label from
`git describe --tags --dirty --match "v*-<platform>.*"`, stripping the
leading `v`. Concrete examples for a Windows build:

| Git state | Derived label |
|---|---|
| Clean checkout at exactly the tag `v0.8.1-win.1` | `0.8.1-win.1` |
| 3 commits past `v0.8.1-win.1`, at commit `abc1234`, clean | `0.8.1-win.1-3-gabc1234` |
| Same as above, with uncommitted changes | `0.8.1-win.1-3-gabc1234-dirty` |
| Before the first prerelease of this `X.Y.Z` cycle | `0.0.0-win.0-<n>-g<sha>` (fallback — pipeline falls back to the repo's initial commit) |

The label reads directly as a statement about the repo state:
`0.8.1-win.1-3-gabc1234` means "three commits past the `v0.8.1-win.1`
prerelease, at commit `abc1234`". Two rebuilds of the same commit
produce the same label, which is what you want — the label identifies
the source, not the build attempt.

**Counter advancement rule (absolute)**. `N` advances only inside the
`-PublishGithub` code path of the canonical release pipeline. There is
no other mechanism — no manual edit to a version file, no
`-DisplayVersionLabel` override that bumps state, no local task that
writes a new tag. The next `N` is computed as
`max(existing-N-for-this-X.Y.Z-platform) + 1`, from the tags already on
the remote. First prerelease of a new `X.Y.Z` cycle starts at `.1`.

**Dirty-tree rule (absolute)**. The pipeline refuses to publish when
`git describe --dirty` reports `-dirty`, or when there are staged but
uncommitted changes. Publication requires a clean, committed,
pushed-or-pushable HEAD. This closes the loophole where a label baked
from an uncommitted working tree could reach users and be
unreproducible from Git.

**`-DisplayVersionLabel` override**. The flag remains for the narrow
case where the operator needs to force a specific label string — for
example, when cutting a bespoke build for a single tester. It is not
the normal flow; the normal flow is letting `git describe` derive the
label. Using `-DisplayVersionLabel` does not publish anything and does
not advance `N`.

**Measurement ledger**. Published prereleases must be recorded in
[OPTIMIZATION_MEASUREMENTS.md](OPTIMIZATION_MEASUREMENTS.md) with the
measured hot-path numbers at the time of cut. Unpublished local builds
do not go in the ledger — they are ephemeral by definition and their
labels contain the commit sha, so any measurement taken from one is
already traceable to the exact source.

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

### Release lifecycle

Because Windows, macOS, and Linux are cut on independent timelines and
each platform's users download from its own tag, the lifecycle rules
below are absolute. Breaking any of them can invalidate a download link
that a user on a different platform is actively relying on.

**Cross-platform non-interference (absolute).** Publishing, deleting,
renaming, or editing a release or tag on one platform must never touch
a release or tag on another platform. The publishing pipeline's only
write path is `gh release create` (plus `gh release edit` on the tag it
just created, for retitling). It must not call `gh release delete` or
`git push --delete <tag>` on any tag whose platform identifier differs
from the one being published. This is defense-in-depth against a
single-track operator accidentally wiping downloads for the other two
tracks.

**Same-platform supersession is manual and conservative.** When
`vX.Y.Z-<platform>.N+1` ships, the prior `vX.Y.Z-<platform>.N` is not
deleted by the pipeline. The operator may manually delete a superseded
prerelease only when (a) a strictly newer `vX.Y.Z-<platform>.M` with
`M > N` already exists on the same platform and `X.Y.Z`, and (b) the
older build is known broken or obsolete. Absent both conditions, keep
the release — the release list is also an audit log.

**Known-broken prereleases may be deleted.** A prerelease that shipped
a reproducible fatal defect (for example, the `v0.7.5-21` Windows build
that failed to load in Resolve due to missing NPP runtime DLLs) may be
deleted to prevent new downloads of a guaranteed-broken artifact. The
deletion must be explicit, logged in the commit or PR that replaces it,
and must not touch any other platform's tags.

**Stable does not erase its prereleases.** When stable `vX.Y.Z` ships,
the `vX.Y.Z-win.*`, `vX.Y.Z-mac.*`, and `vX.Y.Z-linux.*` prereleases
stay. Users on a prerelease are migrated to stable by the auto-updater
via SemVer precedence (stable outranks any prerelease of the same
`X.Y.Z`), so deleting the prereleases provides no functional benefit
and costs historical traceability.

**Mutation is forbidden on any published tag.** See the "Tag and
release immutability" subsection above. This applies regardless of
platform, age, or supersession state. A broken tag gets deleted (under
the rules above) and replaced with a new tag — never mutated in place.

## 2. Standardized Artifact Naming

Artifact names must expose both the version and the backend-specific hardware
path so users can choose the correct installer without ambiguity.

- All artifacts include the version number and target OS.
- Backend-bound Windows artifacts include the backend in the filename.
- Do not reuse a generic filename for a backend-specific build.

The OFX product name in artifact filenames is `CorridorKey_OFX` (not
`CorridorKey_Resolve`). The plugin is a host-agnostic OFX bundle and runs in
both DaVinci Resolve and Foundry Nuke; the filename must not imply a single
host. Windows installer artifacts use the suffix `_Install.exe` (not
`_Installer.exe`).

### Windows Installers

Generated by the canonical Windows release wrapper, which delegates to the
packaging scripts internally.

- TensorRT RTX: `CorridorKey_OFX_vX.Y.Z_Windows_RTX_Install.exe`
- DirectML: `CorridorKey_OFX_vX.Y.Z_Windows_DirectML_Install.exe`

### macOS Installers

- Apple Silicon: `CorridorKey_OFX_vX.Y.Z_macOS_AppleSilicon.dmg`

### Linux Installers

Linux is an experimental product track. Packaging emits two installer wrappers
around the same validated `CorridorKey.ofx.bundle` payload, one per
distribution family, so each user installs through the idiomatic package
manager of the host distribution.

- Debian package (Ubuntu 22.04 / 24.04 LTS): `CorridorKey_OFX_vX.Y.Z_Linux_RTX.deb`
- RPM package (Rocky Linux / RHEL 9): `CorridorKey_OFX_vX.Y.Z_Linux_RTX.rpm`

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
- **Models:** the four FP16 runtime artifacts under `models\`
  (corridorkey_fp16_512/1024/1536/2048.onnx) are the reuse-path source
  of truth. If all expected files are present the pipeline skips
  regeneration entirely; if one or more are missing the pipeline
  exports them from `models\CorridorKey.pth` via
  `uv run python export_onnx.py`.

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
  - package Windows installers from an already certified model set. The
    legacy NSIS installer (one large `.exe` with every model bundled) is
    always produced. The modern Inno Setup installer is also produced when
    `-Flavor online|offline` is passed; see "Modern installer (Inno Setup)"
    below for the flavor semantics.
- `release`
  - package the official Windows release tracks from the currently staged,
    validated inputs
- `regen-rtx-release`
  - regenerate the Windows RTX artifacts from the checkpoint, certify them on
    the active RTX host, write the certified artifact manifest, and then
    package the Windows RTX installer

### Modern installer (Inno Setup)

Authoring lives under `scripts/installer/`:

- `corridorkey.iss.template` - Inno Setup 6 source consumed by ISCC.
  Components/Types (mínimo / recomendado / completo / personalizado) and
  modern Windows 11-themed wizard
  (`WizardStyle=modern dynamic windows11`).
- `build_installer.ps1` - PowerShell driver that expands the template and
  invokes ISCC.exe. Reads `distribution_manifest.json` for pack URLs and
  SHA256 hashes.
- `build_distribution_manifest.py` - regenerates `distribution_manifest.json`
  from authoritative Hugging Face state.
- `stage_offline_payload.ps1` - downloads every "ready" pack file into
  `dist/_offline_payload/` for the OFFLINE flavor build.

Two flavors are produced from the SAME template via the `Flavor`
preprocessor define:

| Flavor    | Size  | Network at install time | When to use |
|-----------|-------|-------------------------|-------------|
| `online`  | ~85 MB | Required | Default. Stub installer downloads selected packs from Hugging Face with SHA256 verification. |
| `offline` | ~7 GB  | None     | Air-gapped or low-bandwidth installs. Every selected pack is pre-bundled inside the .exe. |

Component selection (Plugin base / Pack Verde / Pack Azul / Hint Tracker)
is identical across flavors. The difference is purely transport: the
green-only operator pays only for green-track bytes; the blue operator
pays for the libtorch + TensorRT runtime DLLs only when blue is
selected.

Producing either flavor:

```powershell
# Online (small stub, downloads packs)
.\scripts\windows.ps1 -Task package-ofx -Track rtx -Flavor online \
    -DisplayVersionLabel <label>

# Offline (self-contained)
.\scripts\windows.ps1 -Task package-ofx -Track rtx -Flavor offline \
    -DisplayVersionLabel <label>
```

The wrapper produces BOTH the legacy NSIS .exe AND the chosen Inno
Setup flavor in the same run, against the same staged OFX bundle.
Once the modern installer is validated end-to-end the legacy NSIS
path retires.

Pack source-of-truth lives in `scripts/installer/distribution_manifest.json`,
which records the Hugging Face URL, SHA256, and size of every shipped
artifact. The Inno Setup `[Code]` block uses Inno Setup 6.1+'s built-in
`CreateDownloadPage` API (no third-party plugin) which natively verifies
each download against the manifest's SHA256 and aborts on mismatch.

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

The display label plumbs a human-readable version string into every
user-visible surface on Windows. It flows through CMake into
`include/corridorkey/version.hpp`
(`CORRIDORKEY_DISPLAY_VERSION_STRING`), which the OFX panel, the
`corridorkey --version` CLI, and the runtime-server log filename
(`ofx_runtime_server_v<label>.log`) all read. The packaging scripts
also bake the label into the dist artifact names when present:
`CorridorKey_Resolve_v<label>_Windows_RTX_Installer.exe`,
`CorridorKey_Resolve_v<label>_Windows_RTX\\`.

The label is produced by one of three mechanisms, in priority order:

1. `-DisplayVersionLabel <string>` explicitly passed. Override for
   narrow cases (bespoke tester build, reproducing a historical label).
   No validation beyond shape.
2. Publication flow (`-Task release -PublishGithub`). The pipeline
   computes the next canonical tag `vX.Y.Z-win.N` and uses that, after
   enforcing the dirty-tree rule.
3. Default (every local `build`, `package-ofx`, or `release` without
   `-PublishGithub`). Derived from
   `git describe --tags --dirty --match "v*-win.*"` with the leading
   `v` stripped.

The full derivation rules, dirty-tree rejection, and counter advancement
semantics are documented in section 1 "Pre-release labels". On Windows,
published labels must be of the form `X.Y.Z-win.N`; local labels are
free to be the longer `git describe` form ending in `-<count>-g<sha>`
or `-<count>-g<sha>-dirty`.

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

- `Windows RTX` packages the public FP16 ladder through `2048px`. INT8 ONNX
  and CPU rendering have been retired; FP16 on RTX is the only quality and
  only backend the installer ships.
- `Auto` continues to respect the safe quality ceiling of the active GPU tier.
- Manual fixed quality may attempt a higher packaged rung directly and then
  follow the established runtime failure path if that quality cannot execute.

The Windows RTX installer must pass packaged `doctor` validation before it is
considered releasable.

### macOS Build Steps

macOS has one curated runtime root and one canonical release entrypoint:

- `vendor/onnxruntime/` — vendored Apple Silicon ONNX Runtime dylib
- `scripts/release_pipeline_macos.sh` — canonical macOS build, package, and
  publish entrypoint

The canonical macOS release command is:

```bash
scripts/release_pipeline_macos.sh --display-label X.Y.Z-mac.N
```

Passing `--display-label` bakes the label into `version.hpp`
(`CORRIDORKEY_DISPLAY_VERSION_STRING`), the CLI `--version` output, the
OFX panel, the runtime-server log filename, and the DMG filename
(`CorridorKey_Resolve_vX.Y.Z-mac.N_macOS_AppleSilicon.dmg`). The label
must match `X.Y.Z-mac.N` and its `X.Y.Z` core must equal
`CMakeLists.txt` `PROJECT_VERSION`; the pipeline refuses to proceed
otherwise.

Other flags supported by the wrapper:

- `--skip-tests` — debug convenience; any build intended for a user must
  pass the quality gate, same rule as Windows.
- `--clean-only` — sanitize `build/release-macos-portable`,
  `build/debug-macos-portable`, and `dist/` then exit.
- `--publish-github` — after the build, package, and validation gates
  pass, invoke `scripts/publish_github_release.sh` with the produced
  DMG. Requires `--display-label`. Looks for release notes at
  `build/release_notes/v<label>.md` by default, or at
  `--notes-file PATH` when provided.
- `--github-repo OWNER/REPO` — override the publish target.

### Linux Build Steps

Linux is the experimental track. It has one canonical wrapper:

- `scripts/linux.sh --task release --version X.Y.Z [--display-label X.Y.Z-linux.N]`

The `--display-label` and `--publish-github` flags follow the same
contract as macOS: label shape is `X.Y.Z-linux.N`, core must match
`--version`, and publishing reuses
`scripts/publish_github_release.sh` with a notes file at
`build/release_notes/v<label>.md`. Linux emits two installer assets per
release (`.deb` and `.rpm`); both go up under the same tag.

### Publishing guardrails (shared)

`scripts/publish_github_release.sh` is the single bash helper that both
the macOS and Linux pipelines hand off to for GitHub publication. It
enforces, in this order, the same rules that
`Publish-CorridorKeyGithubRelease` enforces on Windows:

1. `--display-label` must parse as `X.Y.Z-<platform>.N` and its core
   must match `--version`. Naked `X.Y.Z` tags are treated as stable
   and mapped to `--latest`; labels with a suffix are mapped to
   `--prerelease`.
2. Working tree must be clean: `git diff`, `git diff --cached`, and
   `git ls-files --others --exclude-standard` must all be empty. The
   helper refuses to publish from a dirty checkout because the label
   baked into the artifact would otherwise be unreproducible from Git.
3. Release notes file must exist, be non-empty, and contain all four
   sections: `## Overview`, `## Changelog`, `## Assets & Downloads`,
   `## Installation Instructions`. Any missing section aborts the
   publish with a pointer to section 5 of this document.
4. The target tag `v<label>` must not already exist on the remote.
   Tag immutability is absolute (section 1, "Tag and release
   immutability").
5. Every declared `--asset` must exist on disk.
6. Title is constructed from the platform: `... (Windows)`,
   `... (macOS) - Apple Silicon`, or `... (Linux)`. The prerelease
   state is carried by the `--prerelease` flag, not by the title
   string.

`--dry-run` prints the exact `gh release create` command the helper
would run without invoking it. Use it as a final sanity check before
letting the real publish fire.

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
template below. Every canonical pipeline
(`scripts\release_pipeline_windows.ps1` on Windows,
`scripts/release_pipeline_macos.sh` on macOS, `scripts/linux.sh` on
Linux) refuses to publish a release without a notes file containing
the required sections, so there is no path to shipping a placeholder
like "Auto-published by ...".

Before publishing, write the notes to `build/release_notes/v<tag>.md`
(for example `build/release_notes/v0.8.1-win.1.md` or
`build/release_notes/v0.8.1-mac.1.md`). When the publish flag is
passed through the platform wrapper, the pipeline picks that file up
automatically if it exists at the convention path; otherwise point to
it explicitly:

- Windows: `scripts\windows.ps1 -Task release -Version X.Y.Z -ForwardArguments '-PublishGithub','-NotesFile','<path>'`
- macOS: `scripts/release_pipeline_macos.sh --display-label X.Y.Z-mac.N --publish-github --notes-file <path>`
- Linux: `scripts/linux.sh --task release --version X.Y.Z --display-label X.Y.Z-linux.N --publish-github --notes-file <path>`

### CorridorKey OFX (OFX plugin + bundled CLI)

This release track ships the OFX plugin used by DaVinci Resolve and
Foundry Nuke, plus the `corridorkey` CLI bundled inside the same
installer. The CLI's directory is registered on the system `PATH` at
install time, so a single OFX install delivers both the OFX surface
and the CLI surface; the Tauri GUI is shipped separately (see the
"CorridorKey Runtime" track below).

The host-coverage qualifier `[Nuke & Resolve]` precedes the platform
qualifier on every OFX release title, regardless of platform combination.
This makes the host coverage explicit at a glance and matches the
host-agnostic artifact name (`CorridorKey_OFX_*`). The single space
between the closing bracket and the opening parenthesis is intentional:
without it, Markdown renderers (and the
`scripts/check_docs_consistency.py` linter) would interpret the title
as an inline link and treat the platform name as a target path, turning
every example into a broken local link.

- Windows only: `CorridorKey OFX vX.Y.Z [Nuke & Resolve] (Windows)`
- macOS only: `CorridorKey OFX vX.Y.Z [Nuke & Resolve] (macOS) - Apple Silicon`
- Linux only: `CorridorKey OFX vX.Y.Z [Nuke & Resolve] (Linux)`
- Windows and macOS: `CorridorKey OFX vX.Y.Z [Nuke & Resolve] (Windows & macOS)`
- Windows and Linux: `CorridorKey OFX vX.Y.Z [Nuke & Resolve] (Windows & Linux)`
- Windows, macOS, and Linux: `CorridorKey OFX vX.Y.Z [Nuke & Resolve] (Windows, macOS & Linux)`

### CorridorKey Runtime (Tauri GUI desktop app)

This release track ships the Tauri-based desktop GUI defined in
`src/gui`. The historical name "Runtime" is preserved on the release
title and on the asset filename
(`CorridorKey_Runtime_v<label>_<Platform>_<Track>_Installer.exe`)
because changing it would break the auto-updater for users already on
this track. The product surface is the GUI; the embedded runtime
payload it ships is a copy of the same artifacts the OFX installer
ships (see Section 1 "What This Is" in `docs/SPEC.md` for the full
three-surface breakdown).

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
- **NVIDIA RTX 30 Series or newer:** Download `CorridorKey_OFX_vX.Y.Z_Windows_RTX_Install.exe`.
- **Windows DirectML track (experimental):** Download `CorridorKey_OFX_vX.Y.Z_Windows_DirectML_Install.exe`.
- Do not describe the DirectML installer as official support for every AMD, Intel, or RTX 20 series GPU family. Refer readers to `help/SUPPORT_MATRIX.md` for the real support designation.

### macOS
- Include this section only when the release contains a macOS installer.
- **Apple Silicon (M-Series):** Download `CorridorKey_OFX_vX.Y.Z_macOS_AppleSilicon.dmg`.

### Linux
- Include this section only when the release contains a Linux artifact.
- **Ubuntu 22.04 / 24.04 LTS (experimental):** Download `CorridorKey_OFX_vX.Y.Z_Linux_RTX.deb`.
- **Rocky Linux / RHEL 9 (experimental):** Download `CorridorKey_OFX_vX.Y.Z_Linux_RTX.rpm`.
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
