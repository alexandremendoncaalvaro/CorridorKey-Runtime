#!/usr/bin/env pwsh
# verify_ci.ps1 — run the same checks CI runs, locally, before pushing.
#
# Mirrors .github/workflows/ci.yml: format check, configure with the CI debug
# preset, build, run unit tests. Intended to give developers a single
# "will CI pass?" command so format/build/test regressions do not ship to the
# runner.
#
# Usage:
#   scripts/verify_ci.ps1                  # all checks
#   scripts/verify_ci.ps1 -Mode Format     # format check only
#   scripts/verify_ci.ps1 -Mode SkipTests  # format + build, skip ctest
[CmdletBinding()]
param(
    [ValidateSet("All", "Format", "SkipTests")]
    [string]$Mode = "All"
)

$ErrorActionPreference = "Stop"
$RepoRoot = (git rev-parse --show-toplevel).Trim()
Set-Location $RepoRoot

# VCPKG_ROOT is required by CMakePresets.json for the vcpkg toolchain file.
# Fail early with a clear message instead of a confusing CMake error later.
if ([string]::IsNullOrWhiteSpace($env:VCPKG_ROOT)) {
    Write-Error "VCPKG_ROOT is not set. Set it to your vcpkg checkout (e.g. C:\tools\vcpkg) before running this script."
    exit 1
}
if (-not (Test-Path $env:VCPKG_ROOT)) {
    Write-Error "VCPKG_ROOT does not exist: $env:VCPKG_ROOT"
    exit 1
}

# 1. Format check — matches CI's format job exactly.
Write-Host "==> [1/3] clang-format --dry-run --Werror"

# Discover clang-format: PATH first, then common install locations. Ubuntu CI
# uses apt's clang-format (v18); pip's clang-format==18.1.8 wheel is the easiest
# way to match that on Windows.
function Find-ClangFormat {
    $candidates = @()
    $onPath = Get-Command clang-format -ErrorAction SilentlyContinue
    if ($onPath) { $candidates += $onPath.Source }
    $pipScripts = Join-Path $env:APPDATA '..\Local\Packages\PythonSoftwareFoundation.Python.3.13_qbz5n2kfra8p0\LocalCache\local-packages\Python313\Scripts\clang-format.exe'
    $candidates += [System.IO.Path]::GetFullPath($pipScripts)
    $candidates += 'C:\Program Files\LLVM\bin\clang-format.exe'
    $candidates += 'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\Llvm\x64\bin\clang-format.exe'
    foreach ($c in $candidates) {
        if ($c -and (Test-Path $c)) { return $c }
    }
    return $null
}

$clangFormat = Find-ClangFormat
if (-not $clangFormat) {
    Write-Error "clang-format not found. Install via 'pip install clang-format==18.1.8' or LLVM."
    exit 1
}
Write-Host "    using: $clangFormat"

$targets = Get-ChildItem -Path src, include, tests -Recurse -File `
    -Include *.cpp, *.hpp | Select-Object -ExpandProperty FullName
& $clangFormat --dry-run --Werror @targets
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
Write-Host "    OK"

if ($Mode -eq "Format") {
    Write-Host "==> Format-only run; skipping build and tests."
    exit 0
}

# 2. Configure + build with the Windows CI preset.
$Preset = "debug"
$BuildDir = "build/$Preset"

# CMake's compiler detection needs MSVC on PATH. If cl.exe is not already
# present (i.e. the user is running from a plain PowerShell rather than a
# Developer PowerShell), bootstrap the VS dev shell. Mirrors scripts/build.ps1.
$clFound = Get-Command cl.exe -ErrorAction SilentlyContinue
if (-not $clFound) {
    $vsWhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vsWhere)) {
        Write-Error "cl.exe not on PATH and vswhere.exe not found. Run from a Developer PowerShell or install Visual Studio."
        exit 1
    }
    $vsInstallDir = & $vsWhere -latest -property installationPath 2>$null
    $launchScript = Join-Path $vsInstallDir "Common7\Tools\Launch-VsDevShell.ps1"
    if (-not (Test-Path $launchScript)) {
        Write-Error "Launch-VsDevShell.ps1 not found at: $launchScript"
        exit 1
    }
    Write-Host "    bootstrapping MSVC dev shell from: $vsInstallDir"
    & $launchScript -Arch amd64 -SkipAutomaticLocation | Out-Null
}

Write-Host "==> [2/3] cmake --preset $Preset && cmake --build"
cmake --preset $Preset
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
cmake --build --preset $Preset
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
Write-Host "    OK"

if ($Mode -eq "SkipTests") {
    Write-Host "==> Skipping tests (-Mode SkipTests)."
    exit 0
}

# 3. Run the same ctest selector CI uses.
Write-Host "==> [3/3] ctest --label-regex unit"
ctest --test-dir $BuildDir --output-on-failure --label-regex unit
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
Write-Host "    OK"

Write-Host "==> All CI-equivalent checks passed."
