# Self-Hosted Windows Runner

## Why

The hosted `windows-2022` runner takes ~90 minutes to run our CI from a cold
vcpkg cache, dominated by the FFmpeg compile. The GitHub Actions binary cache
(`x-gha`) brings warm runs to ~5 minutes, but cache evictions and dependency
bumps still cost a full cold compile. A self-hosted Windows runner with a
persistent on-disk vcpkg binary cache eliminates the cold-compile path
entirely: the binaries live on the runner's disk, and a vcpkg dependency
graph change rebuilds only the affected ports.

This is opt-in. The repository does not require a self-hosted runner — the
hosted Windows runner with `x-gha` caching is the default. This document
describes the setup for the moment we choose to switch.

## Scope

- Single Windows machine, single runner process.
- Used only by `CorridorKey-Runtime`. Do not register this runner against
  multiple repos — the cache contents are repo-scoped trust.
- Public repository safety: the workflows that target self-hosted runners
  must gate on `github.event.pull_request.head.repo.full_name ==
  github.repository` so a fork PR cannot execute attacker-controlled code on
  the runner.

## Hardware and OS

- Windows 11 Pro or Windows Server 2022.
- 8+ cores, 32+ GB RAM, 200+ GB free disk on a fast SSD.
- No GPU required for build/test. A GPU is required only if the runner is
  going to execute integration tests that load a real ONNX model on
  TensorRT/DirectML — out of scope here.

## One-time installs

Install the toolchain that matches the hosted `windows-2022` image. Versions
below are the floor; later patch releases are fine.

1. Visual Studio Build Tools 2022 with the `Desktop development with C++`
   workload (MSVC v143, Windows 11 SDK, CMake tools).
2. Git for Windows (latest).
3. `pwsh` (PowerShell 7+) — already shipped on Windows 11.
4. Python 3.11+ on PATH (used by `scripts/lint_release_notes.py`).
5. CUDA Toolkit 12.8 if the runner will execute GPU integration tests.
   Standard install path; no env tweaks required.

Add nothing else to PATH. In particular, **do not install MinGW or MSYS2 on
the runner**. The hosted image has MinGW on PATH, which is exactly the trap
we papered over with `ilammy/msvc-dev-cmd@v1` in `ci.yml`. A self-hosted
runner without MinGW makes that step unnecessary.

## vcpkg setup

```powershell
# Repository checkout
git clone https://github.com/microsoft/vcpkg C:\tools\vcpkg
C:\tools\vcpkg\bootstrap-vcpkg.bat -disableMetrics

# Persistent binary cache — survives runner restarts and workflow runs.
$cache = "C:\vcpkg-binary-cache"
New-Item -ItemType Directory -Path $cache -Force | Out-Null
[Environment]::SetEnvironmentVariable("VCPKG_DEFAULT_BINARY_CACHE", $cache, "Machine")
[Environment]::SetEnvironmentVariable("VCPKG_ROOT", "C:\tools\vcpkg", "Machine")
```

The repo's CLAUDE.md and `feedback_vcpkg_root` memory both record that
`VCPKG_ROOT` must be `C:\tools\vcpkg` on this machine. The persistent
binary cache is the new piece — it is what removes cold compiles. Workflows
do not need to set `VCPKG_BINARY_SOURCES` because the local default cache
is read-write by definition.

## Runner registration

In the GitHub UI: `Settings -> Actions -> Runners -> New self-hosted
runner`. Copy the registration command and run it from an admin PowerShell
in `C:\actions-runner`. When prompted for labels, add this exact label:

```
corridorkey-build
```

Workflows target this label explicitly (`runs-on: [self-hosted, Windows,
X64, corridorkey-build]`) so an unrelated self-hosted runner registered to
the org cannot pick up our jobs.

Install the runner as a service so it survives reboots:

```powershell
.\config.cmd --url https://github.com/<org>/CorridorKey-Runtime `
             --token <token> `
             --labels corridorkey-build `
             --runasservice
```

## Switching workflows to use it

The current `ci.yml` and `release.yml` use `runs-on: windows-2022`. To
switch a job, change that line to:

```yaml
runs-on: [self-hosted, Windows, X64, corridorkey-build]
if: ${{ github.event_name != 'pull_request' || github.event.pull_request.head.repo.full_name == github.repository }}
```

The `if` is the public-repo safety gate. Without it, anyone can fork the
repo, push a `.github/workflows/...` change to their fork, open a PR, and
have their workflow code execute on our runner. The gate restricts
self-hosted execution to refs we already trust (push events on the main
repo, and PRs whose head is a branch on the main repo).

After switching, the first run will populate the on-disk vcpkg cache (one
slow run, ~30 minutes for FFmpeg). Subsequent runs read from cache and
should complete the configure step in under 60 seconds.

## Maintenance

- **Disk usage**: the vcpkg binary cache grows over time. Set up a monthly
  scheduled task to prune entries older than 30 days:
  ```powershell
  Get-ChildItem $env:VCPKG_DEFAULT_BINARY_CACHE -Recurse |
    Where-Object { $_.LastAccessTime -lt (Get-Date).AddDays(-30) } |
    Remove-Item -Force
  ```
- **OS / SDK updates**: patch the runner the same week as the corresponding
  hosted-image bump in GitHub's runner-images repository. Drift between the
  self-hosted runner and the hosted Windows image creates "passes locally,
  fails in CI" surprises.
- **Runner upgrades**: GitHub auto-updates the runner agent on its own
  cadence. Do not pin a version.
- **Health check**: `actions-runner/run.cmd --check` verifies connectivity
  and credentials. Run it after any network change.

## Decommissioning

When this runner is no longer needed:

1. Disable the workflow gates that target it (revert the `runs-on:` lines
   to `windows-2022`).
2. In the GitHub UI: `Settings -> Actions -> Runners`, remove the runner.
3. On the machine: `actions-runner\config.cmd remove --token <removal-token>`.
4. Delete `C:\actions-runner` and `C:\vcpkg-binary-cache`.

The vcpkg checkout at `C:\tools\vcpkg` may remain — it is also used by
local developer builds.
