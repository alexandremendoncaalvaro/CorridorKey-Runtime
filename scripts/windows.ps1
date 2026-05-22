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
    # Modern installer flavor (Inno Setup 6). When empty, package-ofx
    # produces the legacy NSIS installer. When set to "online" or
    # "offline", package-ofx skips NSIS and emits only the selected
    # Inno Setup installer from the same staged OFX bundle.
    [ValidateSet("", "online", "offline")]
    [string]$Flavor = "",
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

$publishGithubRequested = $additionalArguments | Where-Object { $_ -eq "-PublishGithub" -or $_ -eq "/PublishGithub" }
if ($Task -eq "release" -and $publishGithubRequested -and $Flavor -ne "online") {
    throw "Windows GitHub publication is online-only. Re-run with -Flavor online; offline packages are local/private artifacts and must not be uploaded to GitHub."
}
$stableGithubReleaseRequested = $Task -eq "release" -and $publishGithubRequested -and
    [string]::IsNullOrWhiteSpace($DisplayVersionLabel)

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

# Mechanism #3: derive the local-build label from `git describe` plus a
# per-build reference when the operator did not pass an explicit override.
# Without the build reference, rebuilding the same dirty commit overwrites
# an installer name that still looks identical in Resolve's OFX panel.
# Stable GitHub releases are the only clean-label path: when publishing
# with no prerelease label, the release pipeline keeps X.Y.Z as the tag,
# installer name, and panel version.
if ([string]::IsNullOrWhiteSpace($DisplayVersionLabel)) {
    if ($stableGithubReleaseRequested) {
        Write-Host "[windows] Stable GitHub release requested; using clean version label $resolvedVersion." -ForegroundColor Yellow
    } elseif ($Task -eq "package-ofx") {
        $buildDir = Join-Path $repoRoot ("build\" + $Preset)
        $builtLabel = Get-CorridorKeyBuiltCliDisplayLabel -BuildDir $buildDir
        if ([string]::IsNullOrWhiteSpace($builtLabel)) {
            throw "Task 'package-ofx' could not read a built CLI label from $buildDir. Run scripts\windows.ps1 -Task build -Preset $Preset first."
        }
        if ($builtLabel -eq $resolvedVersion) {
            throw "Task 'package-ofx' found a bare built CLI label '$builtLabel'. Rebuild through scripts\windows.ps1 -Task build -Preset $Preset so the panel and installer name carry a build reference."
        }
        $DisplayVersionLabel = $builtLabel
        Write-Host "[windows] Reusing display version label from built CLI: $DisplayVersionLabel" -ForegroundColor Yellow
    } else {
        $derivedLabel = Get-CorridorKeyDerivedDisplayLabel -RepoRoot $repoRoot
        if (-not [string]::IsNullOrWhiteSpace($derivedLabel)) {
            $DisplayVersionLabel = $derivedLabel
            Write-Host "[windows] Derived display version label from git/build: $DisplayVersionLabel" -ForegroundColor Yellow
        }
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
            if (-not [string]::IsNullOrWhiteSpace($Flavor) -and $variant.Suffix -eq "RTX") {
                $arguments += @("-SkipNsisInstaller")
            }
            $arguments += $additionalArguments
            Invoke-CorridorKeyScript -ScriptName "package_ofx_installer_windows.ps1" -Arguments $arguments
            Assert-CorridorKeyVariantDoctorHealthy -Version $resolvedVersion -ReleaseSuffix $variant.Suffix -DisplayVersionLabel $DisplayVersionLabel

            # Modern installer (Inno Setup 6, scripts/installer/). Only
            # the RTX variant is wired right now; DirectML keeps the
            # legacy NSIS path. The Inno builder reuses the staged OFX
            # bundle the OFX packager just produced (same Plugin
            # Payload, same DLLs, same model layout); the only
            # difference is the surrounding installer shell.
            if (-not [string]::IsNullOrWhiteSpace($Flavor) -and $variant.Suffix -eq "RTX") {
                $stagedTag = if ([string]::IsNullOrWhiteSpace($DisplayVersionLabel)) { $resolvedVersion } else { $DisplayVersionLabel }
                $stagedBundle = Join-Path $repoRoot ("dist\CorridorKey_OFX_v${stagedTag}_Windows_$($variant.Suffix)\CorridorKey.ofx.bundle")
                if (-not (Test-Path $stagedBundle)) {
                    throw "Inno Setup builder requires the staged OFX bundle, not found at $stagedBundle. The OFX packager either failed silently or was skipped."
                }

                $innoArgs = @(
                    "-Flavor", $Flavor,
                    "-Version", $resolvedVersion,
                    "-DisplayVersionLabel", $stagedTag,
                    "-PluginPayloadDir", $stagedBundle
                )
                if ($Flavor -eq "offline") {
                    # Offline flavor needs every pack file laid out on
                    # disk before ISCC compiles. stage_offline_payload
                    # requires every manifest entry to be ready, then
                    # downloads each file from Hugging Face into
                    # dist/_offline_payload/ with SHA256 verify.
                    # The helper is idempotent (skips files whose
                    # local sha256 already matches the manifest) so a
                    # repeated package-ofx run only re-downloads what
                    # actually changed.
                    Invoke-CorridorKeyScript -ScriptName "installer\stage_offline_payload.ps1" -Arguments @()
                    $offlineRoot = Join-Path $repoRoot "dist\_offline_payload"
                    $innoArgs += @("-ModelPayloadDir", $offlineRoot)
                }
                Invoke-CorridorKeyScript -ScriptName "installer\build_installer.ps1" -Arguments $innoArgs
            }
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
        if (-not [string]::IsNullOrWhiteSpace($Flavor)) {
            $arguments += @("-Flavor", $Flavor)
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
