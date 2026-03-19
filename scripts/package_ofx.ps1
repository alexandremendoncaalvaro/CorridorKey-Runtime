param(
    [string]$BuildDir = "",
    [string]$OrtRoot = "",
    [string]$ModelsDir = "",
    [string]$OutputDir = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot

if ([string]::IsNullOrWhiteSpace($BuildDir)) {
    $BuildDir = Join-Path $repoRoot "build\release"
}
if ([string]::IsNullOrWhiteSpace($OrtRoot)) {
    $universalOrt = Join-Path $repoRoot "vendor\onnxruntime-universal"
    if (Test-Path $universalOrt) {
        $OrtRoot = $universalOrt
    } else {
        $OrtRoot = Join-Path $repoRoot "vendor\onnxruntime-windows-rtx"
    }
}
if ([string]::IsNullOrWhiteSpace($ModelsDir)) {
    $ModelsDir = Join-Path $repoRoot "models"
}
if ([string]::IsNullOrWhiteSpace($OutputDir)) {
    $OutputDir = Join-Path $repoRoot "dist\CorridorKey.ofx"
}

$pluginBinary = Join-Path $BuildDir "src\plugins\ofx\CorridorKey.ofx"
$win64Dir = Join-Path $OutputDir "Contents\Win64"
$resourcesDir = Join-Path $OutputDir "Contents\Resources\models"

function Assert-FileExists {
    param([string]$Path, [string]$Message)
    if (-not (Test-Path $Path)) {
        throw $Message
    }
}

function Resolve-OrtDllPath {
    param([string]$Root, [string]$Name)
    $path1 = Join-Path $Root $Name
    $path2 = Join-Path (Join-Path $Root "bin") $Name
    $path3 = Join-Path (Join-Path $Root "lib") $Name
    foreach ($candidate in @($path1, $path2, $path3)) {
        if (Test-Path $candidate) {
            return $candidate
        }
    }
    return $null
}

function Copy-OrtDll {
    param([string]$Root, [string]$Name, [string]$DestinationDir)
    $resolved = Resolve-OrtDllPath -Root $Root -Name $Name
    if (-not $resolved) {
        throw "Required runtime DLL not found: $Name (searched under $Root)"
    }
    Copy-Item $resolved $DestinationDir -Force
}

if (Test-Path $OutputDir) {
    Remove-Item $OutputDir -Recurse -Force
}

New-Item -ItemType Directory -Path $win64Dir -Force | Out-Null
New-Item -ItemType Directory -Path $resourcesDir -Force | Out-Null

Assert-FileExists -Path $pluginBinary -Message "OpenFX plugin binary not found at $pluginBinary"
Copy-Item $pluginBinary $win64Dir -Force

Copy-OrtDll -Root $OrtRoot -Name "onnxruntime.dll" -DestinationDir $win64Dir
Copy-OrtDll -Root $OrtRoot -Name "onnxruntime_providers_shared.dll" -DestinationDir $win64Dir
Copy-OrtDll -Root $OrtRoot -Name "DirectML.dll" -DestinationDir $win64Dir

$tensorrtProvider = Resolve-OrtDllPath -Root $OrtRoot -Name "onnxruntime_providers_nv_tensorrt_rtx.dll"
if (-not $tensorrtProvider) {
    $tensorrtProvider = Resolve-OrtDllPath -Root $OrtRoot -Name "onnxruntime_providers_nvtensorrtrtx.dll"
}
if ($tensorrtProvider) {
    Copy-Item $tensorrtProvider $win64Dir -Force
    # Copy essential TensorRT-RTX support libs
    Copy-OrtDll -Root $OrtRoot -Name "tensorrt_onnxparser_rtx_1_3.dll" -DestinationDir $win64Dir
    Copy-OrtDll -Root $OrtRoot -Name "tensorrt_rtx_1_3.dll" -DestinationDir $win64Dir
}

$cudartCandidates = @()
$rootBin = Join-Path $OrtRoot "bin"
$rootLib = Join-Path $OrtRoot "lib"
foreach ($candidateDir in @($OrtRoot, $rootBin, $rootLib)) {
    if (Test-Path $candidateDir) {
        $cudartCandidates += Get-ChildItem -Path $candidateDir -Filter "cudart64_*.dll" -File -ErrorAction SilentlyContinue
    }
}
if ($cudartCandidates.Count -eq 0) {
    throw "Required CUDA runtime DLL not found (cudart64_*.dll)."
}
foreach ($candidate in $cudartCandidates) {
    Copy-Item $candidate.FullName $win64Dir -Force
}

$targetModels = @(
    "corridorkey_fp16_768.onnx",
    "corridorkey_fp16_1024.onnx",
    "corridorkey_fp16_1536.onnx",
    "corridorkey_fp16_2048.onnx",
    "corridorkey_int8_512.onnx"
)
foreach ($model in $targetModels) {
    $sourcePath = Join-Path $ModelsDir $model
    Assert-FileExists -Path $sourcePath -Message "Missing model: $sourcePath"
    Copy-Item $sourcePath $resourcesDir -Force
}

Write-Host "OpenFX bundle ready at: $OutputDir"
