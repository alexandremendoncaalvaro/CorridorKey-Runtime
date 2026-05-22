#!/usr/bin/env pwsh
# verify_ci.ps1 - run local quality gates before pushing.
#
# Format checking uses the repository .clang-format file and the same
# clang-format 18.x toolchain version documented for contributors. Build and
# unit-test checks use the Windows debug preset.
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

function Add-ClangFormatCandidate {
    param(
        [System.Collections.Generic.List[string]]$Candidates,
        [string]$Path
    )

    if ([string]::IsNullOrWhiteSpace($Path)) {
        return
    }

    $expandedPath = [Environment]::ExpandEnvironmentVariables($Path)
    try {
        $fullPath = [System.IO.Path]::GetFullPath($expandedPath)
    } catch {
        $fullPath = $expandedPath
    }

    if (-not $Candidates.Contains($fullPath)) {
        [void]$Candidates.Add($fullPath)
    }
}

function Add-PythonUserBaseCandidate {
    param(
        [System.Collections.Generic.List[string]]$Candidates,
        [string]$Executable,
        [string[]]$ArgumentList
    )

    $command = Get-Command $Executable -ErrorAction SilentlyContinue
    if (-not $command) {
        return
    }

    try {
        $userBase = & $command.Source @ArgumentList 2>$null | Select-Object -First 1
    } catch {
        return
    }

    if ([string]::IsNullOrWhiteSpace($userBase)) {
        return
    }

    Add-ClangFormatCandidate $Candidates (Join-Path $userBase "Scripts\clang-format.exe")
}

function Get-ClangFormatVersionText {
    param([string]$Path)

    try {
        return (& $Path --version 2>$null | Select-Object -First 1)
    } catch {
        return $null
    }
}

function Test-ClangFormatSupported {
    param([string]$VersionText)

    if ($VersionText -match "version\s+([0-9]+)") {
        return ([int]$Matches[1]) -ge 18
    }
    return $false
}

function Find-ClangFormat {
    $candidates = [System.Collections.Generic.List[string]]::new()

    Add-ClangFormatCandidate $candidates $env:CORRIDORKEY_CLANG_FORMAT

    foreach ($name in @("clang-format", "clang-format-18")) {
        $commands = Get-Command $name -All -ErrorAction SilentlyContinue
        foreach ($command in $commands) {
            Add-ClangFormatCandidate $candidates $command.Source
        }
    }

    Add-PythonUserBaseCandidate $candidates "py" @("-3", "-m", "site", "--user-base")
    Add-PythonUserBaseCandidate $candidates "python" @("-m", "site", "--user-base")
    Add-PythonUserBaseCandidate $candidates "python3" @("-m", "site", "--user-base")

    if (-not [string]::IsNullOrWhiteSpace($env:LOCALAPPDATA)) {
        foreach ($path in Get-ChildItem -Path (Join-Path $env:LOCALAPPDATA "Programs\Python\Python*\Scripts\clang-format.exe") -ErrorAction SilentlyContinue) {
            Add-ClangFormatCandidate $candidates $path.FullName
        }
        foreach ($path in Get-ChildItem -Path (Join-Path $env:LOCALAPPDATA "Packages\PythonSoftwareFoundation.Python.*\LocalCache\local-packages\Python*\Scripts\clang-format.exe") -ErrorAction SilentlyContinue) {
            Add-ClangFormatCandidate $candidates $path.FullName
        }
    }

    if (-not [string]::IsNullOrWhiteSpace($env:APPDATA)) {
        foreach ($path in Get-ChildItem -Path (Join-Path $env:APPDATA "Python\Python*\Scripts\clang-format.exe") -ErrorAction SilentlyContinue) {
            Add-ClangFormatCandidate $candidates $path.FullName
        }
    }

    Add-ClangFormatCandidate $candidates "C:\Program Files\LLVM\bin\clang-format.exe"
    Add-ClangFormatCandidate $candidates "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\Llvm\x64\bin\clang-format.exe"
    Add-ClangFormatCandidate $candidates "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\Llvm\bin\clang-format.exe"

    $programFilesX86 = ${env:ProgramFiles(x86)}
    if (-not [string]::IsNullOrWhiteSpace($programFilesX86)) {
        $vsWhere = Join-Path $programFilesX86 "Microsoft Visual Studio\Installer\vswhere.exe"
        if (Test-Path $vsWhere) {
            $vsInstalls = & $vsWhere -products * -property installationPath 2>$null
            foreach ($vsInstall in $vsInstalls) {
                Add-ClangFormatCandidate $candidates (Join-Path $vsInstall "VC\Tools\Llvm\x64\bin\clang-format.exe")
                Add-ClangFormatCandidate $candidates (Join-Path $vsInstall "VC\Tools\Llvm\bin\clang-format.exe")
            }
        }
    }

    $script:RejectedClangFormatCandidates = @()
    foreach ($candidate in $candidates) {
        if (-not (Test-Path $candidate)) {
            continue
        }

        $versionText = Get-ClangFormatVersionText $candidate
        if (Test-ClangFormatSupported $versionText) {
            $script:ClangFormatVersionText = $versionText
            return $candidate
        }

        if (-not [string]::IsNullOrWhiteSpace($versionText)) {
            $script:RejectedClangFormatCandidates += "$candidate ($versionText)"
        }
    }

    return $null
}

Write-Host "==> [1/3] clang-format --dry-run --Werror"
$clangFormat = Find-ClangFormat
if (-not $clangFormat) {
    if ($script:RejectedClangFormatCandidates.Count -gt 0) {
        Write-Error "clang-format 18+ not found. Older candidates: $($script:RejectedClangFormatCandidates -join '; ')"
    } else {
        Write-Error "clang-format 18+ not found. Install via 'pip install clang-format==18.1.8' or LLVM, or set CORRIDORKEY_CLANG_FORMAT."
    }
    exit 1
}
Write-Host "    using: $clangFormat"
Write-Host "    version: $($script:ClangFormatVersionText)"

$targets = Get-ChildItem -Path src, include, tests -Recurse -File `
    -Include *.cpp, *.hpp | Select-Object -ExpandProperty FullName
$style = "file:$RepoRoot\.clang-format"
& $clangFormat "--style=$style" --dry-run --Werror @targets
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
Write-Host "    OK"

if ($Mode -eq "Format") {
    Write-Host "==> Format-only run; skipping build and tests."
    exit 0
}

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

$Preset = "debug"
$BuildDir = "build/$Preset"

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

Write-Host "==> [3/3] ctest --label-regex unit"
ctest --test-dir $BuildDir --output-on-failure --label-regex unit
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
Write-Host "    OK"

Write-Host "==> All local quality checks passed."
