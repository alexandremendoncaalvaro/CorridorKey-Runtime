param(
    [ValidateSet("build", "prepare-rtx", "prepare-models", "package-ofx", "package-runtime", "release", "sync-version", "regen-rtx-release")]
    [string]$Task = "build",
    [ValidateSet("debug", "release", "release-lto")]
    [string]$Preset = "release",
    [string]$Version = "",
    [string]$Checkpoint = "",
    [string]$CorridorKeyRepo = "",
    [ValidateSet("rtx", "dml", "all")]
    [string]$Track = "all",
    [string[]]$ForwardArguments = @()
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot "windows_runtime_helpers.ps1")

function Invoke-CorridorKeyScript {
    param(
        [string]$ScriptName,
        [string[]]$Arguments = @()
    )

    $scriptPath = Join-Path $PSScriptRoot $ScriptName
    if (-not (Test-Path $scriptPath)) {
        throw "Script not found: $scriptPath"
    }

    $command = @(
        "-NoProfile",
        "-ExecutionPolicy", "Bypass",
        "-File", $scriptPath
    ) + $Arguments

    & powershell.exe @command
    if ($LASTEXITCODE -ne 0) {
        throw "Script failed: $scriptPath"
    }
}

function Get-CorridorKeyReleaseSuffixes {
    param([string]$Track)

    switch ($Track) {
        "rtx" { return @("RTX") }
        "dml" { return @("DirectML") }
        default { return @("DirectML", "RTX") }
    }
}

$resolvedTrack = $Track
if ($Task -eq "release" -and -not $PSBoundParameters.ContainsKey("Track")) {
    $resolvedTrack = "rtx"
}

$syncGuiMetadata = $Task -in @("package-runtime", "release", "sync-version")
$additionalArguments = @($ForwardArguments)
$resolvedVersion = Initialize-CorridorKeyVersion `
    -RepoRoot $repoRoot `
    -Version $Version `
    -SyncGuiMetadata:$syncGuiMetadata

$prepareArguments = @("-Version", $resolvedVersion, "-BuildPreset", $Preset)
if (-not [string]::IsNullOrWhiteSpace($Checkpoint)) {
    $prepareArguments += @("-Checkpoint", $Checkpoint)
}
if (-not [string]::IsNullOrWhiteSpace($CorridorKeyRepo)) {
    $prepareArguments += @("-CorridorKeyRepo", $CorridorKeyRepo)
}

Write-Host "[windows] Task: $Task" -ForegroundColor Cyan
Write-Host "[windows] Version: $resolvedVersion" -ForegroundColor Cyan
if ($Task -in @("package-ofx", "package-runtime", "release")) {
    Write-Host "[windows] Track: $resolvedTrack" -ForegroundColor Cyan
}

switch ($Task) {
    "sync-version" {
        Write-Host "[windows] Version metadata synchronized." -ForegroundColor Green
        break
    }
    "build" {
        if ($additionalArguments.Count -gt 0) {
            throw "Task 'build' does not accept additional arguments. Use -Preset only."
        }

        Invoke-CorridorKeyScript -ScriptName "build.ps1" -Arguments @("-Preset", $Preset)
        break
    }
    "prepare-rtx" {
        $arguments = @($prepareArguments) + $additionalArguments
        Invoke-CorridorKeyScript -ScriptName "prepare_windows_rtx_release.ps1" -Arguments $arguments
        break
    }
    "prepare-models" {
        $arguments = @($prepareArguments) + @(
            "-SkipOrtBuild",
            "-SkipRuntimeBuild",
            "-SkipPackage",
            "-ForceModelPreparation"
        ) + $additionalArguments
        Invoke-CorridorKeyScript -ScriptName "prepare_windows_rtx_release.ps1" -Arguments $arguments
        break
    }
    "package-ofx" {
        foreach ($suffix in Get-CorridorKeyReleaseSuffixes -Track $resolvedTrack) {
            $arguments = @("-Version", $resolvedVersion, "-ReleaseSuffix", $suffix) + $additionalArguments
            Invoke-CorridorKeyScript -ScriptName "package_ofx_installer_windows.ps1" -Arguments $arguments
        }
        break
    }
    "package-runtime" {
        foreach ($suffix in Get-CorridorKeyReleaseSuffixes -Track $resolvedTrack) {
            $arguments = @("-Version", $resolvedVersion, "-ReleaseSuffix", $suffix) + $additionalArguments
            Invoke-CorridorKeyScript -ScriptName "package_runtime_installer_windows.ps1" -Arguments $arguments
        }
        break
    }
    "release" {
        $arguments = @("-Version", $resolvedVersion, "-Track", $resolvedTrack) + $additionalArguments
        Invoke-CorridorKeyScript -ScriptName "release_pipeline_windows.ps1" -Arguments $arguments
        break
    }
    "regen-rtx-release" {
        $arguments = @("-Version", $resolvedVersion, "-BuildPreset", $Preset)
        if (-not [string]::IsNullOrWhiteSpace($Checkpoint)) {
            $arguments += @("-Checkpoint", $Checkpoint)
        }
        if (-not [string]::IsNullOrWhiteSpace($CorridorKeyRepo)) {
            $arguments += @("-CorridorKeyRepo", $CorridorKeyRepo)
        }
        $arguments += $additionalArguments
        Invoke-CorridorKeyScript -ScriptName "regen_windows_rtx_release.ps1" -Arguments $arguments
        break
    }
}
