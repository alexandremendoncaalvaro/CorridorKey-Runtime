param(
    [ValidateSet("build", "prepare-rtx", "prepare-models", "prepare-torchtrt", "certify-rtx-artifacts", "certify-torchtrt-artifacts", "package-ofx", "package-runtime", "release", "sync-version", "regen-rtx-release")]
    [string]$Task = "build",
    [ValidateSet("debug", "release", "release-lto")]
    [string]$Preset = "release",
    [string]$Version = "",
    [string]$Checkpoint = "",
    [string]$CorridorKeyRepo = "",
    [ValidateSet("rtx", "dml", "all")]
    [string]$Track = "all",
    [string]$DisplayVersionLabel = "",
    [string[]]$ForwardArguments = @()
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot "windows_runtime_helpers.ps1")

function Assert-CorridorKeyWindowsReleaseLabelFormat {
    param(
        [string]$Version,
        [string]$DisplayVersionLabel
    )
    if ([string]::IsNullOrWhiteSpace($DisplayVersionLabel)) {
        return
    }
    $pattern = '^(?<core>\d+\.\d+\.\d+)-win\.(?<counter>\d+)$'
    $match = [regex]::Match($DisplayVersionLabel, $pattern)
    if (-not $match.Success) {
        throw "DisplayVersionLabel '$DisplayVersionLabel' is not a valid Windows prerelease label. Expected form: X.Y.Z-win.N (see docs/RELEASE_GUIDELINES.md section 1)."
    }
    $labelCore = $match.Groups['core'].Value
    if (-not [string]::IsNullOrWhiteSpace($Version) -and $labelCore -ne $Version) {
        throw "DisplayVersionLabel core '$labelCore' does not match -Version '$Version'. The label must be '$Version-win.<counter>'."
    }
}

function Assert-CorridorKeyVariantDoctorHealthy {
    param(
        [string]$Version,
        [string]$ReleaseSuffix,
        [string]$DisplayVersionLabel = ""
    )

    $artifactVersionTag = if ([string]::IsNullOrWhiteSpace($DisplayVersionLabel)) { $Version } else { $DisplayVersionLabel }
    $bundleValidationPath = Join-Path $repoRoot ("dist\CorridorKey_OFX_v${artifactVersionTag}_Windows_${ReleaseSuffix}\bundle_validation.json")
    Assert-CorridorKeyBundleValidationHealthy `
        -ValidationReportPath $bundleValidationPath `
        -Label "Variant $ReleaseSuffix" | Out-Null
}

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

# Validate any user-provided override BEFORE we attempt to derive a
# label from git. The strict X.Y.Z-win.N format only applies when the
# operator explicitly opts in to the published-prerelease label shape;
# the derived form (mechanism #3 in docs/RELEASE_GUIDELINES.md
# "Windows Release Label Plumbing") is the longer git-describe shape
# `0.8.2-win.2-82-g4a75ef2[-dirty]` and is intentionally allowed to
# bypass that strict format.
Assert-CorridorKeyWindowsReleaseLabelFormat `
    -Version $resolvedVersion `
    -DisplayVersionLabel $DisplayVersionLabel

# Mechanism #3: derive the local-build label from `git describe` when
# the operator did not pass an explicit override. Without this the
# packaged binary's `CORRIDORKEY_DISPLAY_VERSION_STRING` falls back to
# the bare CMakeLists `PROJECT_VERSION`, which collides across every
# local build of the same X.Y.Z cycle and defeats the operator's
# ability to confirm a fresh build loaded in the editor (the OFX
# panel just shows "0.8.3" no matter how many rebuilds happened).
# Empty derivation result keeps the historical fallback (CMake
# version), so old branches without matching tags do not regress.
if ([string]::IsNullOrWhiteSpace($DisplayVersionLabel)) {
    $derivedLabel = Get-CorridorKeyDerivedDisplayLabel -RepoRoot $repoRoot
    if (-not [string]::IsNullOrWhiteSpace($derivedLabel)) {
        $DisplayVersionLabel = $derivedLabel
        Write-Host "[windows] Derived display version label from git: $DisplayVersionLabel" -ForegroundColor Yellow
    }
}

$prepareArguments = @("-Version", $resolvedVersion, "-BuildPreset", $Preset)
if (-not [string]::IsNullOrWhiteSpace($Checkpoint)) {
    $prepareArguments += @("-Checkpoint", $Checkpoint)
}
if (-not [string]::IsNullOrWhiteSpace($CorridorKeyRepo)) {
    $prepareArguments += @("-CorridorKeyRepo", $CorridorKeyRepo)
}
if (-not [string]::IsNullOrWhiteSpace($DisplayVersionLabel)) {
    $prepareArguments += @("-DisplayVersionLabel", $DisplayVersionLabel)
}

Write-Host "[windows] Task: $Task" -ForegroundColor Cyan
Write-Host "[windows] Version: $resolvedVersion" -ForegroundColor Cyan
if (-not [string]::IsNullOrWhiteSpace($DisplayVersionLabel)) {
    Write-Host "[windows] Display version label: $DisplayVersionLabel" -ForegroundColor Cyan
}
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

        $arguments = @("-Preset", $Preset)
        if (-not [string]::IsNullOrWhiteSpace($DisplayVersionLabel)) {
            $arguments += @("-DisplayVersionLabel", $DisplayVersionLabel)
        }
        Invoke-CorridorKeyScript -ScriptName "build.ps1" -Arguments $arguments
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
    "certify-rtx-artifacts" {
        $arguments = @(
            "-Version", $resolvedVersion,
            "-BuildPreset", $Preset
        )
        if (-not [string]::IsNullOrWhiteSpace($DisplayVersionLabel)) {
            $arguments += @("-DisplayVersionLabel", $DisplayVersionLabel)
        }
        $arguments += $additionalArguments
        Invoke-CorridorKeyScript -ScriptName "certify_windows_rtx_artifacts.ps1" -Arguments $arguments
        break
    }
    "prepare-torchtrt" {
        # Stages the curated TorchTRT runtime payload under
        # vendor/torchtrt-windows/. Sister to prepare-rtx but bound to the
        # blue-pack runtime (see Sprint 1 plan, Strategy C).
        $arguments = @() + $additionalArguments
        Invoke-CorridorKeyScript -ScriptName "prepare_windows_torchtrt_release.ps1" -Arguments $arguments
        break
    }
    "certify-torchtrt-artifacts" {
        $arguments = @() + $additionalArguments
        Invoke-CorridorKeyScript -ScriptName "certify_windows_torchtrt_artifacts.ps1" -Arguments $arguments
        break
    }
    "package-ofx" {
        foreach ($variant in Get-CorridorKeyWindowsOfxReleaseVariants -Track $resolvedTrack) {
            $arguments = @(
                "-Version", $resolvedVersion,
                "-ReleaseSuffix", $variant.Suffix,
                "-ModelProfile", $variant.ModelProfile
            )
            if (-not [string]::IsNullOrWhiteSpace($DisplayVersionLabel)) {
                $arguments += @("-DisplayVersionLabel", $DisplayVersionLabel)
            }
            $arguments += $additionalArguments
            Invoke-CorridorKeyScript -ScriptName "package_ofx_installer_windows.ps1" -Arguments $arguments
            Assert-CorridorKeyVariantDoctorHealthy -Version $resolvedVersion -ReleaseSuffix $variant.Suffix -DisplayVersionLabel $DisplayVersionLabel
        }
        break
    }
    "package-runtime" {
        $runtimeSuffixes = switch ($resolvedTrack) {
            "rtx" { @("RTX") }
            "dml" { @("DirectML") }
            default { @("DirectML", "RTX") }
        }
        foreach ($suffix in $runtimeSuffixes) {
            $arguments = @("-Version", $resolvedVersion, "-ReleaseSuffix", $suffix) + $additionalArguments
            Invoke-CorridorKeyScript -ScriptName "package_runtime_installer_windows.ps1" -Arguments $arguments
        }
        break
    }
    "release" {
        $arguments = @(
            "-Version", $resolvedVersion,
            "-Track", $resolvedTrack
        )
        if (-not [string]::IsNullOrWhiteSpace($DisplayVersionLabel)) {
            $arguments += @("-DisplayVersionLabel", $DisplayVersionLabel)
        }
        $arguments += $additionalArguments
        Invoke-CorridorKeyScript -ScriptName "release_pipeline_windows.ps1" -Arguments $arguments
        break
    }
    "regen-rtx-release" {
        $arguments = @(
            "-Version", $resolvedVersion,
            "-BuildPreset", $Preset
        )
        if (-not [string]::IsNullOrWhiteSpace($DisplayVersionLabel)) {
            $arguments += @("-DisplayVersionLabel", $DisplayVersionLabel)
        }
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
