param(
    [string]$Version = "",
    [ValidateSet("rtx", "dml", "all")]
    [string]$Track = "rtx",
    [string]$DisplayVersionLabel = "",
    [switch]$SkipTests,
    [switch]$CleanOnly,
    [switch]$PublishGithub,
    [string]$GithubRepo = "alexandremendoncaalvaro/CorridorKey-Runtime"
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
        [string]$GithubRepo,
        [string]$RepoRoot
    )
    $tagLabel = if ([string]::IsNullOrWhiteSpace($DisplayVersionLabel)) { $Version } else { $DisplayVersionLabel }
    $tagName = "v$tagLabel"
    $isPrerelease = -not [string]::IsNullOrWhiteSpace($DisplayVersionLabel)

    $gh = Get-Command gh -ErrorAction SilentlyContinue
    if (-not $gh) {
        throw "Cannot publish GitHub release: 'gh' CLI is not on PATH. Install GitHub CLI or publish manually."
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
        $assetGlobs += (Join-Path $RepoRoot ("dist\CorridorKey_Resolve_v${tagLabel}_Windows_$($variant.Suffix)_Installer.exe"))
    }
    foreach ($asset in $assetGlobs) {
        if (-not (Test-Path $asset)) {
            throw "Expected release asset missing: $asset"
        }
    }

    $titlePlatform = if ($isPrerelease) { "Windows prerelease" } else { "Windows" }
    $title = "CorridorKey Resolve OFX v$tagLabel ($titlePlatform)"

    $ghArgs = @(
        "release", "create", $tagName,
        "--repo", $GithubRepo,
        "--title", $title,
        "--notes", "Auto-published by scripts\release_pipeline_windows.ps1."
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

    $needsRtxTrack = $Track -in @("rtx", "all")
    $needsDirectMlTrack = $Track -in @("dml", "all")
    $buildTrack = if ($Track -eq "dml") { "dml" } else { "rtx" }
    $buildOrtRoot = $null
    $rtxOrtRoot = $null
    $directMlOrtRoot = $null

    Write-Step "Sanitizing Environment"
    $dirsToClean = @("build/release", "dist", "temp")
    foreach ($dir in $dirsToClean) {
        if (Test-Path $dir) {
            Write-Host "Cleaning $dir..."
            Remove-Item $dir -Recurse -Force
        }
    }

    # Do NOT clear %LOCALAPPDATA%\CorridorKey\Logs here. The runtime server
    # writes per-version log files (`ofx_runtime_server_v<X.Y.Z>.log`) that
    # the optimization measurement track consumes for cross-version
    # comparison. Wiping them on every release build destroys the history a
    # developer just captured from testing the previous installer.

    if ($CleanOnly) { exit 0 }

    if ($needsRtxTrack) {
        $rtxOrtRoot = Get-CorridorKeyWindowsOrtRootPath -RepoRoot $repoRoot -Track "rtx"
        if (-not (Test-Path $rtxOrtRoot)) {
            throw "Curated RTX runtime not found at $rtxOrtRoot. Build it with scripts\prepare_windows_rtx_release.ps1 or scripts\build_ort_windows_rtx.ps1 first."
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

    & cmd /c "call `"$vcvars`" && set `"CORRIDORKEY_WINDOWS_ORT_ROOT=$buildOrtRoot`" && cmake --preset release -DCORRIDORKEY_WINDOWS_ORT_ROOT=`"$buildOrtRoot`" $displayVersionArg && cmake --build --preset release -j 8"
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
        & powershell.exe -NoProfile -File "scripts/package_ofx_installer_windows.ps1" @packageArgs

        if ($LASTEXITCODE -ne 0) { throw "Packaging failed for variant: $($v.Suffix)" }

        $expectedInstaller = Join-Path $repoRoot "dist/CorridorKey_Resolve_v${artifactVersionTag}_Windows_$($v.Suffix)_Installer.exe"
        if (-not (Test-Path $expectedInstaller)) {
            throw "CRITICAL: Pipeline claimed success but installer was NOT found at: $expectedInstaller"
        }
        $expectedValidationReport = Join-Path $repoRoot "dist/CorridorKey_Resolve_v${artifactVersionTag}_Windows_$($v.Suffix)\bundle_validation.json"
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

    Write-Success "Selected installers generated, physically verified, and validated."

    Write-Step "Release v$Version is READY"
    Get-ChildItem "dist/*.exe" | Select-Object Name, @{Name="Size(MB)"; Expression={"{0:N2}" -f ($_.Length / 1MB)}} | Format-Table -AutoSize

    if ($PublishGithub) {
        Write-Step "Publishing GitHub Release"
        Publish-CorridorKeyGithubRelease `
            -Version $Version `
            -DisplayVersionLabel $DisplayVersionLabel `
            -Track $Track `
            -GithubRepo $GithubRepo `
            -RepoRoot $repoRoot
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
