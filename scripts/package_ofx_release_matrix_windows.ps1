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

if ([string]::IsNullOrWhiteSpace($BuildDir)) {
    $BuildDir = Join-Path $repoRoot "build\release"
}
if ([string]::IsNullOrWhiteSpace($ModelsDir)) {
    $ModelsDir = Join-Path $repoRoot "models"
}
if ([string]::IsNullOrWhiteSpace($RtxOrtRoot)) {
    $rtxOrt = Join-Path $repoRoot "vendor\onnxruntime-windows-rtx"
    $universalOrt = Join-Path $repoRoot "vendor\onnxruntime-universal"
    if (Test-Path $rtxOrt) {
        $RtxOrtRoot = $rtxOrt
    } elseif (Test-Path $universalOrt) {
        $RtxOrtRoot = $universalOrt
    } else {
        $RtxOrtRoot = $rtxOrt
    }
}
if ([string]::IsNullOrWhiteSpace($DirectMlOrtRoot)) {
    $DirectMlOrtRoot = Join-Path $repoRoot "vendor\onnxruntime-windows-dml"
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
        [string]$ReleaseSuffix
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

Invoke-PackageVariant -Label "RTX" -OrtRoot $RtxOrtRoot -ReleaseSuffix "RTX"
Invoke-PackageVariant -Label "DirectML" -OrtRoot $DirectMlOrtRoot -ReleaseSuffix "DirectML"

Write-Host "[done] Windows Resolve OFX package matrix is ready under dist\\" -ForegroundColor Green
