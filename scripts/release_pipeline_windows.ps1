param(
    [string]$Version = "",
    [ValidateSet("rtx", "dml", "all")]
    [string]$Track = "rtx",
    [ValidateSet("", "online", "offline")]
    [string]$Flavor = "",
    [string]$DisplayVersionLabel = "",
    [switch]$SkipTests,
    [switch]$CleanOnly,
    [switch]$PublishGithub,
    [switch]$PackageAdobe,
    [string]$AdobeSdkRoot = "",
    [string]$GithubRepo = "alexandremendoncaalvaro/CorridorKey-Runtime",
    [string]$NotesFile = ""
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
Set-Location $repoRoot
. (Join-Path $PSScriptRoot "windows_runtime_helpers.ps1")

function Write-Step([string]$msg) {
    Write-Host "`n=== [STEP] $msg ===" -ForegroundColor Cyan
}

function Write-Success([string]$msg) {
    Write-Host "[SUCCESS] $msg" -ForegroundColor Green
}

function Assert-CorridorKeyWindowsReleaseLabel {
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
    if ($labelCore -ne $Version) {
        throw "DisplayVersionLabel core '$labelCore' does not match -Version '$Version'. The label must be '$Version-win.<counter>'."
    }
}

function Publish-CorridorKeyGithubRelease {
    param(
        [string]$Version,
        [string]$DisplayVersionLabel,
        [string]$Track,
        [string]$Flavor,
        [switch]$IncludeAdobeInstaller,
        [string]$GithubRepo,
        [string]$RepoRoot,
        [string]$NotesFile
    )
    $tagLabel = if ([string]::IsNullOrWhiteSpace($DisplayVersionLabel)) { $Version } else { $DisplayVersionLabel }
    $tagName = "v$tagLabel"
    $isPrerelease = -not [string]::IsNullOrWhiteSpace($DisplayVersionLabel)
    $releaseTarget = (& git -C $RepoRoot rev-parse HEAD 2>$null | Out-String).Trim()
    if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($releaseTarget)) {
        throw "Cannot publish GitHub release: unable to resolve the current HEAD commit."
    }

    $gh = Get-Command gh -ErrorAction SilentlyContinue
    if (-not $gh) {
        throw "Cannot publish GitHub release: 'gh' CLI is not on PATH. Install GitHub CLI or publish manually."
    }

    # Release notes are required; docs/RELEASE_GUIDELINES.md section 5
    # defines the Overview/Changelog/Assets/Installation template that
    # every release must carry. Refuse to publish with a placeholder.
    if ([string]::IsNullOrWhiteSpace($NotesFile)) {
        throw "Cannot publish GitHub release: -NotesFile is required. Write build/release_notes/v$tagLabel.md per docs/RELEASE_GUIDELINES.md section 5 and pass -ForwardArguments '-PublishGithub','-NotesFile','build\release_notes\v$tagLabel.md' through scripts\windows.ps1."
    }
    if (-not (Test-Path $NotesFile)) {
        throw "Cannot publish GitHub release: release notes file not found at '$NotesFile'."
    }
    $notesText = Get-Content -Raw -LiteralPath $NotesFile
    if ([string]::IsNullOrWhiteSpace($notesText)) {
        throw "Cannot publish GitHub release: release notes file '$NotesFile' is empty."
    }
    foreach ($marker in @('## Overview', '## Changelog', '## Assets & Downloads', '## Installation Instructions')) {
        if ($notesText -notmatch [regex]::Escape($marker)) {
            throw "Cannot publish GitHub release: notes file '$NotesFile' is missing required section '$marker'. See docs/RELEASE_GUIDELINES.md section 5."
        }
    }

    # gh release view exits non-zero when the release does not exist and
    # writes "release not found" to stderr. Under $ErrorActionPreference=Stop
    # that non-zero exit is escalated to a terminating error before we can
    # inspect $LASTEXITCODE, so isolate the probe with a local preference.
    $priorPreference = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $existing = & gh release view $tagName --repo $GithubRepo --json tagName 2>$null
        $viewExit = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $priorPreference
    }
    if ($viewExit -eq 0 -and -not [string]::IsNullOrWhiteSpace($existing)) {
        throw "GitHub release '$tagName' already exists in $GithubRepo. Per docs/RELEASE_GUIDELINES.md, a published tag is immutable. Bump the counter and retry."
    }

    $assetGlobs = @()
    foreach ($variant in Get-CorridorKeyWindowsOfxReleaseVariants -Track $Track) {
        if (-not [string]::IsNullOrWhiteSpace($Flavor) -and $variant.Suffix -eq "RTX") {
            $assetGlobs += (Join-Path $RepoRoot ("dist\CorridorKey_v${tagLabel}_Windows_$($Flavor.ToLowerInvariant())_Setup.exe"))
        } else {
            $assetGlobs += (Join-Path $RepoRoot ("dist\CorridorKey_OFX_v${tagLabel}_Windows_$($variant.Suffix)_Install.exe"))
        }
    }
    if ($IncludeAdobeInstaller) {
        if ($Flavor -ne "online") {
            throw "Adobe GitHub release assets are online-only. Re-run with -Flavor online."
        }
        $assetGlobs += (Join-Path $RepoRoot ("dist\CorridorKey_Adobe_v${tagLabel}_Windows_RTX_$($Flavor.ToLowerInvariant())_Setup.exe"))
    }
    foreach ($asset in $assetGlobs) {
        if (-not (Test-Path $asset)) {
            throw "Expected release asset missing: $asset"
        }
    }

    # Title format is defined in docs/RELEASE_GUIDELINES.md section 5:
    # "CorridorKey OFX vX.Y.Z [Nuke & Resolve] (Windows)". The host-coverage
    # qualifier "[Nuke & Resolve]" is required on every OFX release title;
    # the prerelease state is carried by the --prerelease flag, not by the
    # title string. The space between "]" and "(" is intentional so the
    # title is not parsed as a Markdown [text](url) link by docs renderers
    # or by scripts/check_docs_consistency.py.
    $title = if ($IncludeAdobeInstaller) {
        "CorridorKey v$tagLabel [Nuke, Resolve & After Effects] (Windows)"
    } else {
        "CorridorKey OFX v$tagLabel [Nuke & Resolve] (Windows)"
    }

    $ghArgs = @(
        "release", "create", $tagName,
        "--repo", $GithubRepo,
        "--title", $title,
        "--notes-file", $NotesFile,
        "--target", $releaseTarget
    )
    if ($isPrerelease) {
        $ghArgs += "--prerelease"
    } else {
        $ghArgs += @("--latest")
    }
    $ghArgs += $assetGlobs

    Write-Host "Publishing GitHub release: $tagName ($(if ($isPrerelease) { 'prerelease' } else { 'stable latest' }))"
    & gh @ghArgs
    if ($LASTEXITCODE -ne 0) {
        throw "gh release create failed for $tagName."
    }
    Write-Success "Published GitHub release $tagName in $GithubRepo."
}

try {
    $Version = Initialize-CorridorKeyVersion -RepoRoot $repoRoot -Version $Version -SyncGuiMetadata
    Assert-CorridorKeyWindowsReleaseLabel -Version $Version -DisplayVersionLabel $DisplayVersionLabel
    if ($PublishGithub -and $Flavor -ne "online") {
        throw "Windows GitHub publication is online-only. Re-run with -Flavor online; offline packages are local/private artifacts and must not be uploaded to GitHub."
    }
    if ([string]::IsNullOrWhiteSpace($DisplayVersionLabel) -and -not $PublishGithub) {
        $derivedLabel = Get-CorridorKeyDerivedDisplayLabel -RepoRoot $repoRoot -Version $Version
        if (-not [string]::IsNullOrWhiteSpace($derivedLabel)) {
            $DisplayVersionLabel = $derivedLabel
            Write-Host "Using local build display label: $DisplayVersionLabel" -ForegroundColor Yellow
        }
    }

    $needsRtxTrack = $Track -in @("rtx", "all")
    $needsDirectMlTrack = $Track -in @("dml", "all")
    $buildTrack = if ($Track -eq "dml") { "dml" } else { "rtx" }
    $buildOrtRoot = $null
    $rtxOrtRoot = $null
    $directMlOrtRoot = $null
    $resolvedAdobeSdkRoot = ""

    if ($PackageAdobe) {
        if ($Flavor -ne "online") {
            throw "Adobe release packaging through this pipeline is online-only. Re-run with -Flavor online."
        }
        if (-not $needsRtxTrack) {
            throw "Adobe release packaging requires the RTX track because the Adobe Blue payload depends on the RTX/TorchTRT package layout."
        }
        $candidateAdobeSdkRoot = $AdobeSdkRoot
        if ([string]::IsNullOrWhiteSpace($candidateAdobeSdkRoot)) {
            $candidateAdobeSdkRoot = Join-Path $repoRoot "vendor\adobe-after-effects-sdk"
        }
        if (-not (Test-Path -LiteralPath $candidateAdobeSdkRoot)) {
            throw "Adobe release packaging requested, but Adobe SDK root was not found at $candidateAdobeSdkRoot. Pass -AdobeSdkRoot or stage vendor\adobe-after-effects-sdk."
        }
        $resolvedAdobeSdkRoot = (Resolve-Path -LiteralPath $candidateAdobeSdkRoot).Path
        Write-Host "Adobe release packaging enabled with SDK: $resolvedAdobeSdkRoot" -ForegroundColor Yellow
    }

    Write-Step "Sanitizing Environment"
    $dirsToClean = @("build/release", "dist", "temp")
    foreach ($dir in $dirsToClean) {
        if (Test-Path $dir) {
            Write-Host "Cleaning $dir..."
            Remove-Item $dir -Recurse -Force
        }
    }

    # Do NOT clear %LOCALAPPDATA%\CorridorKey\Logs here. The runtime server
    # writes per-version log files (`host_plugin_runtime_server_v<X.Y.Z>.log`) that
    # the optimization measurement track consumes for cross-version
    # comparison. Wiping them on every release build destroys the history a
    # developer just captured from testing the previous installer.

    if ($CleanOnly) { exit 0 }

    if ($needsRtxTrack) {
        $rtxOrtRoot = Get-CorridorKeyWindowsOrtRootPath -RepoRoot $repoRoot -Track "rtx"
        if (-not (Test-Path $rtxOrtRoot)) {
            throw "Curated RTX runtime not found at $rtxOrtRoot. Stage it with scripts\windows.ps1 -Task prepare-rtx first."
        }
    }
    if ($needsDirectMlTrack) {
        $directMlOrtRoot = Get-CorridorKeyWindowsOrtRootPath -RepoRoot $repoRoot -Track "dml"
    }

    if ($buildTrack -eq "rtx") {
        $buildOrtRoot = $rtxOrtRoot
    } elseif ($needsDirectMlTrack) {
        if (-not (Test-Path $directMlOrtRoot)) {
            throw "Curated DirectML runtime not found at $directMlOrtRoot. Run scripts\\sync_onnxruntime_directml.ps1 first or package the RTX track."
        }
        $buildOrtRoot = $directMlOrtRoot
    }

    if ($needsDirectMlTrack) {
        if ($null -eq $rtxOrtRoot -or -not (Test-Path $rtxOrtRoot)) {
            throw "DirectML release packaging requires the curated RTX runtime to infer the aligned ONNX Runtime package version."
        }
        $rtxOrtVersion = Get-CorridorKeyWindowsOrtBinaryVersion -RepoRoot $repoRoot -Track "rtx"

        Write-Step "Synchronizing DirectML Runtimes"
        & powershell.exe -NoProfile -File "scripts/sync_onnxruntime_directml.ps1" -OrtVersion $rtxOrtVersion
        if ($LASTEXITCODE -ne 0) { throw "DirectML runtime synchronization failed." }
        Write-Success "DirectML runtimes synchronized."
        if (-not (Test-Path $directMlOrtRoot)) {
            throw "DirectML runtime not found at $directMlOrtRoot after synchronization."
        }
    }

    Write-Step "Building Project (Release Mode)"
    $vcvars = Get-ChildItem -Path "C:\Program Files\Microsoft Visual Studio" -Filter vcvars64.bat -Recurse | Select-Object -First 1 -ExpandProperty FullName
    if (-not $vcvars) { throw "vcvars64.bat not found. MSVC environment required." }

    $displayVersionArg = "-DCORRIDORKEY_DISPLAY_VERSION_LABEL=$DisplayVersionLabel"
    if (-not [string]::IsNullOrWhiteSpace($DisplayVersionLabel)) {
        Write-Host "Using display version label: $DisplayVersionLabel" -ForegroundColor Yellow
    }

    $adobeConfigureArgText = " -DCORRIDORKEY_ENABLE_ADOBE_PLUGIN=OFF"
    if ($PackageAdobe) {
        $adobeConfigureArgText = " -DCORRIDORKEY_ENABLE_ADOBE_PLUGIN=ON -DCORRIDORKEY_ADOBE_SDK_ROOT=`"$resolvedAdobeSdkRoot`""
    }

    & cmd /c "call `"$vcvars`" && set `"CORRIDORKEY_WINDOWS_ORT_ROOT=$buildOrtRoot`" && cmake --preset release -DCORRIDORKEY_WINDOWS_ORT_ROOT=`"$buildOrtRoot`" $displayVersionArg$adobeConfigureArgText && cmake --build --preset release -j 8"
    if ($LASTEXITCODE -ne 0) { throw "Build failed." }
    Write-Success "Build completed successfully."

    if (-not $SkipTests) {
        Write-Step "Quality Gate: Running Automated Tests"
        & cmd /c "call `"$vcvars`" && set `"CORRIDORKEY_WINDOWS_ORT_ROOT=$buildOrtRoot`" && cmake --build --preset release --target test_unit test_regression && cd build/release && ctest --output-on-failure"
        if ($LASTEXITCODE -ne 0) { throw "Tests failed." }
        Write-Success "All tests passed."
    }

    Write-Step "Quality Gate: Packaging and Backend Validation"

    $variants = @()
    foreach ($variant in Get-CorridorKeyWindowsOfxReleaseVariants -Track $Track) {
        $variantOrtRoot = if ($variant.Track -eq "dml") { $directMlOrtRoot } else { $rtxOrtRoot }
        $variants += @{
            Label = $variant.Label
            Suffix = $variant.Suffix
            Root = $variantOrtRoot
            ModelProfile = $variant.ModelProfile
        }
    }

    $artifactVersionTag = if ([string]::IsNullOrWhiteSpace($DisplayVersionLabel)) { $Version } else { $DisplayVersionLabel }

    foreach ($v in $variants) {
        Write-Host "--- Packaging Variant: $($v.Label) ---" -ForegroundColor Yellow
        $packageArgs = @(
            "-Version", $Version,
            "-ReleaseSuffix", $v.Suffix,
            "-ModelProfile", $v.ModelProfile,
            "-OrtRoot", $v.Root
        )
        if (-not [string]::IsNullOrWhiteSpace($DisplayVersionLabel)) {
            $packageArgs += @("-DisplayVersionLabel", $DisplayVersionLabel)
        }
        $isModernInnoInstaller = (-not [string]::IsNullOrWhiteSpace($Flavor)) -and ($v.Suffix -eq "RTX")
        if ($isModernInnoInstaller) {
            $packageArgs += @("-SkipNsisInstaller")
        }
        & powershell.exe -NoProfile -File "scripts/package_ofx_installer_windows.ps1" @packageArgs

        if ($LASTEXITCODE -ne 0) { throw "Packaging failed for variant: $($v.Suffix)" }

        if ($isModernInnoInstaller) {
            $stagedBundle = Join-Path $repoRoot ("dist\CorridorKey_OFX_v${artifactVersionTag}_Windows_$($v.Suffix)\CorridorKey.ofx.bundle")
            if (-not (Test-Path $stagedBundle)) {
                throw "Inno Setup builder requires the staged OFX bundle, not found at $stagedBundle."
            }

            $innoArgs = @(
                "-Flavor", $Flavor,
                "-Version", $Version,
                "-DisplayVersionLabel", $artifactVersionTag,
                "-PluginPayloadDir", $stagedBundle
            )
            if ($Flavor -eq "offline") {
                & powershell.exe -NoProfile -File "scripts\installer\stage_offline_payload.ps1"
                if ($LASTEXITCODE -ne 0) {
                    throw "Offline payload staging failed."
                }
                $offlineRoot = Join-Path $repoRoot "dist\_offline_payload"
                $innoArgs += @("-ModelPayloadDir", $offlineRoot)
            }

            & powershell.exe -NoProfile -File "scripts\installer\build_installer.ps1" @innoArgs
            if ($LASTEXITCODE -ne 0) {
                throw "Inno Setup installer build failed for flavor '$Flavor'."
            }
        }

        $expectedInstaller = if ($isModernInnoInstaller) {
            Join-Path $repoRoot "dist/CorridorKey_v${artifactVersionTag}_Windows_$($Flavor.ToLowerInvariant())_Setup.exe"
        } else {
            Join-Path $repoRoot "dist/CorridorKey_OFX_v${artifactVersionTag}_Windows_$($v.Suffix)_Install.exe"
        }
        if (-not (Test-Path $expectedInstaller)) {
            throw "CRITICAL: Pipeline claimed success but installer was NOT found at: $expectedInstaller"
        }
        $expectedValidationReport = Join-Path $repoRoot "dist/CorridorKey_OFX_v${artifactVersionTag}_Windows_$($v.Suffix)\bundle_validation.json"
        if (-not (Test-Path $expectedValidationReport)) {
            throw "CRITICAL: Bundle validation did not produce a validation report at: $expectedValidationReport"
        }
        $validation = Assert-CorridorKeyBundleValidationHealthy `
            -ValidationReportPath $expectedValidationReport `
            -Label "Variant $($v.Suffix)"
        Write-Host "[VERIFIED] Artifact created: $expectedInstaller" -ForegroundColor Green
        Write-Host "[VERIFIED] Bundle validation report created: $expectedValidationReport" -ForegroundColor Green
        if ($validation.models.missing_count -gt 0) {
            Write-Host "[INFO] $($v.Suffix) artifact uses partial model coverage: $($validation.models.missing_models -join ', ')" -ForegroundColor Cyan
        }
        if (-not $validation.doctor.succeeded) {
            Write-Host "[INFO] $($v.Suffix) doctor did not produce a report. Reason: $($validation.doctor.failure_reason)" -ForegroundColor Cyan
        }
    }

    if ($PackageAdobe) {
        $adobeVariant = @($variants | Where-Object { $_.Suffix -eq "RTX" } | Select-Object -First 1)
        if ($adobeVariant.Count -ne 1) {
            throw "Adobe release packaging could not find an RTX variant to stage."
        }

        Write-Host "--- Packaging Variant: Adobe $($adobeVariant[0].Label) ---" -ForegroundColor Yellow
        $adobePackageArgs = @(
            "-Version", $Version,
            "-ReleaseSuffix", $adobeVariant[0].Suffix,
            "-ModelProfile", $adobeVariant[0].ModelProfile,
            "-OrtRoot", $adobeVariant[0].Root,
            "-Flavor", $Flavor
        )
        if (-not [string]::IsNullOrWhiteSpace($DisplayVersionLabel)) {
            $adobePackageArgs += @("-DisplayVersionLabel", $DisplayVersionLabel)
        }
        & powershell.exe -NoProfile -File "scripts/package_adobe_plugins_windows.ps1" @adobePackageArgs
        if ($LASTEXITCODE -ne 0) {
            throw "Adobe packaging failed for variant: $($adobeVariant[0].Suffix)"
        }

        $expectedAdobeInstaller = Join-Path $repoRoot "dist/CorridorKey_Adobe_v${artifactVersionTag}_Windows_$($adobeVariant[0].Suffix)_$($Flavor.ToLowerInvariant())_Setup.exe"
        if (-not (Test-Path $expectedAdobeInstaller)) {
            throw "CRITICAL: Adobe pipeline claimed success but installer was NOT found at: $expectedAdobeInstaller"
        }
        $expectedAdobeValidationReport = Join-Path $repoRoot "dist/CorridorKey_Adobe_v${artifactVersionTag}_Windows_$($adobeVariant[0].Suffix)\adobe_package_validation.json"
        Assert-CorridorKeyAdobePackageValidationHealthy `
            -ValidationReportPath $expectedAdobeValidationReport `
            -Label "Adobe $($adobeVariant[0].Suffix) package" | Out-Null
        Write-Host "[VERIFIED] Adobe artifact created: $expectedAdobeInstaller" -ForegroundColor Green
        Write-Host "[VERIFIED] Adobe validation report created: $expectedAdobeValidationReport" -ForegroundColor Green
    }

    Write-Success "Selected installers generated, physically verified, and validated."

    Write-Step "Release v$Version is READY"
    Get-ChildItem "dist/*.exe" | Select-Object Name, @{Name="Size(MB)"; Expression={"{0:N2}" -f ($_.Length / 1MB)}} | Format-Table -AutoSize

    if ($PublishGithub) {
        Write-Step "Publishing GitHub Release"
        $resolvedNotesFile = $NotesFile
        if ([string]::IsNullOrWhiteSpace($resolvedNotesFile)) {
            $notesTag = if ([string]::IsNullOrWhiteSpace($DisplayVersionLabel)) { $Version } else { $DisplayVersionLabel }
            $defaultNotesFile = Join-Path $repoRoot ("build/release_notes/v$notesTag.md")
            if (Test-Path $defaultNotesFile) {
                $resolvedNotesFile = $defaultNotesFile
            }
        }
        Publish-CorridorKeyGithubRelease `
            -Version $Version `
            -DisplayVersionLabel $DisplayVersionLabel `
            -Track $Track `
            -Flavor $Flavor `
            -IncludeAdobeInstaller:$PackageAdobe `
            -GithubRepo $GithubRepo `
            -RepoRoot $repoRoot `
            -NotesFile $resolvedNotesFile
    } else {
        Write-Host "`n[INFO] -PublishGithub not set; skipping GitHub release publish." -ForegroundColor Yellow
        Write-Host "       To publish: rerun with -ForwardArguments '-PublishGithub' through scripts\\windows.ps1," -ForegroundColor Yellow
        Write-Host "       or call this script directly with -PublishGithub." -ForegroundColor Yellow
    }

} catch {
    Write-Host "`n[FATAL ERROR] Pipeline failed at step: $($_.InvocationInfo.ScriptName)" -ForegroundColor Red
    Write-Host "Error Details: $($_.Exception.Message)" -ForegroundColor Red
    exit 1
}
