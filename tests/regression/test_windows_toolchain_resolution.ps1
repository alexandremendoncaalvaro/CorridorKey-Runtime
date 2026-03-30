Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
. (Join-Path $repoRoot "scripts\windows_runtime_helpers.ps1")

function New-FakeCmakeCommand {
    param(
        [string]$Path,
        [string]$Version
    )

    $content = @(
        "@echo off",
        "echo cmake version $Version"
    )

    Set-Content -Path $Path -Value $content -Encoding Ascii
}

$tempRoot = Join-Path $env:TEMP ("corridorkey-toolchain-test-" + [guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $tempRoot -Force | Out-Null

try {
    $oldCmakePath = Join-Path $tempRoot "cmake-old.cmd"
    $newCmakePath = Join-Path $tempRoot "cmake-new.cmd"

    New-FakeCmakeCommand -Path $oldCmakePath -Version "3.22.2"
    New-FakeCmakeCommand -Path $newCmakePath -Version "4.3.1"

    $resolvedVersion = Get-CorridorKeyCmakeVersion -CmakePath $newCmakePath
    if ($resolvedVersion -ne "4.3.1") {
        throw "Expected Get-CorridorKeyCmakeVersion to return 4.3.1, but got $resolvedVersion."
    }

    $resolved = Select-CorridorKeyBestVersionedPath `
        -Candidates @(
            [pscustomobject]@{ path = $oldCmakePath; version = "3.22.2" },
            [pscustomobject]@{ path = $newCmakePath; version = "4.3.1" }
        ) `
        -MinimumVersion "3.28.0"

    $expectedNewPath = [System.IO.Path]::GetFullPath($newCmakePath)
    if (-not $resolved.meets_minimum) {
        throw "Expected the resolver to satisfy the minimum version with the newer candidate."
    }
    if ($resolved.path -ne $expectedNewPath) {
        throw "Expected the resolver to prefer $expectedNewPath, but got $($resolved.path)."
    }
    if ($resolved.version -ne "4.3.1") {
        throw "Expected version 4.3.1, but got $($resolved.version)."
    }

    $fallback = Select-CorridorKeyBestVersionedPath `
        -Candidates @(
            [pscustomobject]@{ path = $oldCmakePath; version = "3.22.2" }
        ) `
        -MinimumVersion "3.28.0"

    $expectedOldPath = [System.IO.Path]::GetFullPath($oldCmakePath)
    if ($fallback.meets_minimum) {
        throw "Expected a below-minimum candidate to remain below the minimum."
    }
    if ($fallback.path -ne $expectedOldPath) {
        throw "Expected the fallback candidate to be $expectedOldPath, but got $($fallback.path)."
    }
    if ($fallback.version -ne "3.22.2") {
        throw "Expected version 3.22.2, but got $($fallback.version)."
    }

    Write-Host "[PASS] Windows toolchain resolver regression checks passed." -ForegroundColor Green
} finally {
    if (Test-Path $tempRoot) {
        Remove-Item -Path $tempRoot -Recurse -Force
    }
}
