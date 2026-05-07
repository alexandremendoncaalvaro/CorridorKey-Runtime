param(
    [string]$Version = "",
    [string]$BuildDir = "",
    [string]$OrtRoot = "",
    [string]$ModelsDir = "",
    [string]$ArtifactManifestPath = "",
    [string]$ReleaseSuffix = "",
    [ValidateSet("windows-rtx", "windows-universal")]
    [string]$ModelProfile = "",
    [string]$DisplayVersionLabel = "",
    [switch]$Skip2048
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot "windows_runtime_helpers.ps1")

function Write-ReleaseReadme {
    param(
        [string]$Path,
        [string]$Version,
        [string]$ReleaseBasename,
        [string]$ReleaseLabel,
        [string]$ModelProfile
    )

    $modelCoverageText = switch ($ModelProfile) {
        "windows-rtx" { "This Windows RTX package includes the dynamic LibTorch/Torch-TensorRT runtime contract recorded in CorridorKey.ofx.bundle\\model_inventory.json." }
        "windows-universal" { "This Windows DirectML package includes the Windows universal GPU and CPU model set." }
        default { "This package includes the packaged model set recorded in CorridorKey.ofx.bundle\\model_inventory.json." }
    }

@"
CorridorKey OFX v$Version - $ReleaseLabel
=========================================

$modelCoverageText

Files in this release:
- CorridorKey.ofx.bundle: the packaged OFX bundle payload
- install_plugin.bat: manual installer helper for the bundle
- bundle_validation.json: packaging-time validation and doctor status
- CorridorKey.ofx.bundle\model_inventory.json: packaged model inventory

Recommended install path:
1. Run the Windows setup executable produced by the release wrapper as Administrator.
2. Open your OFX host of choice (DaVinci Resolve or Foundry Nuke). The
   plugin is registered for both at the standard OpenFX bundle location.

Installer behavior:
- The installer replaces any existing CorridorKey Windows OFX installation before copying the new bundle.
- It detects DaVinci Resolve and/or Foundry Nuke and only acts on hosts that are present (closes the host if running and clears its OFX metadata cache).
- The CorridorKey CLI (corridorkey.exe) directory is registered on the system PATH, so `corridorkey` is available from any terminal after installation. Open a new shell to pick up the change.
- Uninstalling restores the previous system PATH.
- The installer never auto-launches a host; you choose when to open Resolve or Nuke.

Manual fallback path:
1. Run install_plugin.bat as Administrator from this folder.
2. Open DaVinci Resolve or Foundry Nuke when you are ready to use the plugin.
"@ | Set-Content -Path $Path -Encoding ASCII
}

function Write-PathUpdateScript {
    param(
        [string]$BundlePath
    )

    $targetDir = Join-Path $BundlePath "Contents\Win64"
    if (-not (Test-Path $targetDir)) {
        throw "Bundle Win64 directory not found at $targetDir. Packaging step must run before writing update_path.ps1."
    }

    $targetPath = Join-Path $targetDir "update_path.ps1"
    $content = @'
param(
    [ValidateSet("Install", "Uninstall")]
    [string]$Mode = "Install"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

# The directory holding this script is the directory we want on PATH.
# For CorridorKey installs that is <bundle>\Contents\Win64 which contains
# corridorkey.exe alongside its ONNX Runtime / TensorRT DLLs.
$binDir = $PSScriptRoot

$current = [Environment]::GetEnvironmentVariable("Path", "Machine")
if ($null -eq $current) { $current = "" }

# Drop empty entries and any previous registration of our directory (case-insensitive).
$entries = @(
    $current -split ';' | Where-Object {
        $_ -and ($_.Trim().TrimEnd('\') -ine $binDir.TrimEnd('\'))
    }
)

if ($Mode -eq "Install") {
    $entries += $binDir
}

$newPath = ($entries -join ';').Trim(';')

# SetEnvironmentVariable on "Machine" scope broadcasts WM_SETTINGCHANGE so new
# shells pick up the change immediately. Existing shells must be reopened.
[Environment]::SetEnvironmentVariable("Path", $newPath, "Machine")

Write-Host "CorridorKey CLI PATH $Mode complete: $binDir"
'@

    Set-Content -Path $targetPath -Value $content -Encoding ASCII
    Write-Host "Wrote CLI PATH helper: $targetPath" -ForegroundColor Gray
}

$Version = Initialize-CorridorKeyVersion -RepoRoot $repoRoot -Version $Version
if ([string]::IsNullOrWhiteSpace($BuildDir)) {
    $BuildDir = Join-Path $repoRoot "build\release"
}
$preferredTrack = Get-CorridorKeyWindowsTrackFromReleaseSuffix -ReleaseSuffix $ReleaseSuffix -DefaultTrack "rtx"
if ([string]::IsNullOrWhiteSpace($ModelsDir)) {
    $ModelsDir = Join-Path $repoRoot "models"
}
if ([string]::IsNullOrWhiteSpace($ModelProfile)) {
    $ModelProfile = Get-CorridorKeyOfxModelProfileFromReleaseSuffix -ReleaseSuffix $ReleaseSuffix
}
$profileContract = Get-CorridorKeyModelProfileContract -ModelProfile $ModelProfile
if ($profileContract.bundle_track -eq "rtx" -and $profileContract.backend_intent -eq "torchtrt") {
    $OrtRoot = ""
} else {
    $OrtRoot = Resolve-CorridorKeyWindowsOrtRoot -RepoRoot $repoRoot -ExplicitRoot $OrtRoot -PreferredTrack $preferredTrack
}
$releaseLabel = Get-CorridorKeyWindowsReleaseLabelFromSuffix -ReleaseSuffix $ReleaseSuffix

$normalizedSuffix = ""
if (-not [string]::IsNullOrWhiteSpace($ReleaseSuffix)) {
    $normalizedSuffix = "_" + $ReleaseSuffix.Trim("_")
}

# When a `-DisplayVersionLabel` (e.g. `0.8.2-win.1`) is supplied, the
# staged bundle folder uses it instead of the base version. This keeps
# the packaged payload identity aligned with the setup executable and
# the binaries it installs.
$artifactVersionTag = if ([string]::IsNullOrWhiteSpace($DisplayVersionLabel)) { $Version } else { $DisplayVersionLabel }
$releaseBasename = "CorridorKey_OFX_v${artifactVersionTag}_Windows${normalizedSuffix}"
$releaseDir = Join-Path $repoRoot ("dist\" + $releaseBasename)
$bundlePath = Join-Path $releaseDir "CorridorKey.ofx.bundle"
$installScriptPath = Join-Path $releaseDir "install_plugin.bat"
$readmePath = Join-Path $releaseDir "README.txt"

Write-Host "[1/5] Preparing release directory..." -ForegroundColor Cyan
if (Test-Path $releaseDir) {
    Remove-Item $releaseDir -Recurse -Force
}
New-Item -ItemType Directory -Path $releaseDir -Force | Out-Null

$bundleArgs = @{
    BuildDir = $BuildDir
    OrtRoot = $OrtRoot
    ModelsDir = $ModelsDir
    OutputDir = $bundlePath
    ModelProfile = $ModelProfile
}
if (-not [string]::IsNullOrWhiteSpace($ArtifactManifestPath)) {
    $bundleArgs["ArtifactManifestPath"] = $ArtifactManifestPath
}
if ($Skip2048.IsPresent) {
    $bundleArgs["Skip2048"] = $true
}

Write-Host "[2/5] Packaging the OFX bundle..." -ForegroundColor Cyan
& (Join-Path $repoRoot "scripts\package_ofx.ps1") @bundleArgs
$bundlePackaged = $?
if (-not $bundlePackaged) {
    throw "Windows OFX bundle packaging failed."
}

Write-PathUpdateScript -BundlePath $bundlePath

Write-Host "[3/5] Validating the OFX bundle..." -ForegroundColor Cyan
if ([string]::IsNullOrWhiteSpace($DisplayVersionLabel)) {
    & (Join-Path $repoRoot "scripts\validate_ofx_win.ps1") -BundlePath $bundlePath
} else {
    & (Join-Path $repoRoot "scripts\validate_ofx_win.ps1") `
        -BundlePath $bundlePath `
        -ExpectedDisplayVersionLabel $DisplayVersionLabel
}
$bundleValidated = $?
if (-not $bundleValidated) {
    throw "Windows OFX bundle validation failed."
}

$bundleValidationPath = Join-Path $releaseDir "bundle_validation.json"
Assert-CorridorKeyBundleValidationHealthy `
    -ValidationReportPath $bundleValidationPath `
    -Label "$releaseLabel bundle" | Out-Null

Write-Host "[4/5] Assembling release folder..." -ForegroundColor Cyan
Copy-Item (Join-Path $repoRoot "scripts\install_plugin.bat") $installScriptPath -Force
Write-ReleaseReadme -Path $readmePath `
    -Version $Version `
    -ReleaseBasename $releaseBasename `
    -ReleaseLabel $releaseLabel `
    -ModelProfile $ModelProfile

Write-Host "[5/5] OFX bundle staged for the Windows setup builder." -ForegroundColor Cyan
Write-Host "Release directory ready at: $releaseDir" -ForegroundColor Green
Write-Host "Windows setup is produced by scripts\installer\build_installer.ps1 through scripts\windows.ps1." -ForegroundColor Green
