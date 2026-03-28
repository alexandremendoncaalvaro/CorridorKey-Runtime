param(
    [string]$BuildDir = "",
    [string]$ModelsDir = "",
    [string]$RtxOrtRoot = "",
    [string]$DirectMlOrtRoot = "",
    [switch]$SyncDirectML,
    [switch]$Skip2048
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot "windows_runtime_helpers.ps1")

if ([string]::IsNullOrWhiteSpace($BuildDir)) {
    $BuildDir = Join-Path $repoRoot "build\release"
}
if ([string]::IsNullOrWhiteSpace($ModelsDir)) {
    $ModelsDir = Join-Path $repoRoot "models"
}
if ([string]::IsNullOrWhiteSpace($RtxOrtRoot)) {
    $RtxOrtRoot = Get-CorridorKeyWindowsOrtRootPath -RepoRoot $repoRoot -Track "rtx"
}
if ([string]::IsNullOrWhiteSpace($DirectMlOrtRoot)) {
    $DirectMlOrtRoot = Get-CorridorKeyWindowsOrtRootPath -RepoRoot $repoRoot -Track "dml"
}

$installerScript = Join-Path $PSScriptRoot "package_ofx_installer_windows.ps1"
$syncDirectMlScript = Join-Path $PSScriptRoot "sync_onnxruntime_directml.ps1"

if (-not (Test-Path $installerScript)) {
    throw "Windows OFX installer script not found: $installerScript"
}

function Invoke-PackageVariant {
    param(
        [string]$Label,
        [string]$OrtRoot,
        [string]$ReleaseSuffix,
        [string]$ModelProfile
    )

    if (-not (Test-Path $OrtRoot)) {
        throw "$Label ORT root not found: $OrtRoot"
    }

    $arguments = @(
        "-ExecutionPolicy", "Bypass",
        "-File", $installerScript,
        "-BuildDir", $BuildDir,
        "-OrtRoot", $OrtRoot,
        "-ModelsDir", $ModelsDir
    )
    if (-not [string]::IsNullOrWhiteSpace($ReleaseSuffix)) {
        $arguments += @("-ReleaseSuffix", $ReleaseSuffix)
    }
    if (-not [string]::IsNullOrWhiteSpace($ModelProfile)) {
        $arguments += @("-ModelProfile", $ModelProfile)
    }
    if ($Skip2048.IsPresent) {
        $arguments += "-Skip2048"
    }

    Write-Host "[package] Building $Label Resolve OFX package..." -ForegroundColor Cyan
    & powershell.exe @arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to build $Label Resolve OFX package."
    }
}

if ($SyncDirectML.IsPresent -or -not (Test-Path $DirectMlOrtRoot)) {
    if (-not (Test-Path $syncDirectMlScript)) {
        throw "DirectML sync script not found: $syncDirectMlScript"
    }

    Write-Host "[sync] Refreshing official DirectML runtime..." -ForegroundColor Cyan
    & powershell.exe -ExecutionPolicy Bypass -File $syncDirectMlScript -OutputDir $DirectMlOrtRoot
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to sync the official DirectML runtime."
    }
}

Invoke-PackageVariant -Label "RTX Stable" -OrtRoot $RtxOrtRoot -ReleaseSuffix "RTX_Stable" -ModelProfile "rtx-stable"
Invoke-PackageVariant -Label "RTX Full" -OrtRoot $RtxOrtRoot -ReleaseSuffix "RTX_Full" -ModelProfile "rtx-full"
Invoke-PackageVariant -Label "DirectML" -OrtRoot $DirectMlOrtRoot -ReleaseSuffix "DirectML" -ModelProfile "windows-universal"

Write-Host "[done] Windows Resolve OFX package matrix is ready under dist\\" -ForegroundColor Green
