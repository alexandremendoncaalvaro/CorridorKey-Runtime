param(
    [string]$BundlePath = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($BundlePath)) {
    $BundlePath = Join-Path $repoRoot "dist\CorridorKey.ofx"
}

Write-Host "Validating OFX bundle: $BundlePath" -ForegroundColor Cyan
Write-Host ""

$win64Dir = Join-Path $BundlePath "Contents\Win64"
$resourcesDir = Join-Path $BundlePath "Contents\Resources\models"
$bundleDescriptor = [System.IO.Path]::GetFullPath($BundlePath)
$expectsUniversalGpuPath = $bundleDescriptor -match 'Universal'

# Check bundle structure
if (-not (Test-Path $BundlePath)) {
    throw "Bundle directory not found: $BundlePath"
}

if (-not (Test-Path $win64Dir)) {
    throw "Missing Contents\Win64 directory"
}

if (-not (Test-Path $resourcesDir)) {
    throw "Missing Contents\Resources\models directory"
}

Write-Host "[PASS] Bundle directory structure exists" -ForegroundColor Green

# CRITICAL: Check for correct ONNX Runtime DLL name
$onnxDll = Join-Path $win64Dir "onnxruntime.dll"
if (-not (Test-Path $onnxDll)) {
    Write-Host "[FAIL] onnxruntime.dll not found!" -ForegroundColor Red
    throw "ERROR: onnxruntime.dll not found in Win64 directory"
}

Write-Host "[PASS] onnxruntime.dll exists" -ForegroundColor Green

# Check all required DLLs
$requiredDlls = @(
    "onnxruntime.dll",
    "onnxruntime_providers_shared.dll",
    "DirectML.dll"
)

foreach ($dll in $requiredDlls) {
    $path = Join-Path $win64Dir $dll
    if (-not (Test-Path $path)) {
        Write-Host "[FAIL] Missing required DLL: $dll" -ForegroundColor Red
        throw "Missing required DLL: $dll"
    }
    Write-Host "[PASS] Found $dll" -ForegroundColor Green
}

# Check plugin binary
$plugin = Join-Path $win64Dir "CorridorKey.ofx"
if (-not (Test-Path $plugin)) {
    Write-Host "[FAIL] Plugin binary not found" -ForegroundColor Red
    throw "Plugin binary not found: CorridorKey.ofx"
}

$pluginSize = (Get-Item $plugin).Length
Write-Host "[PASS] Found plugin binary ($([math]::Round($pluginSize / 1MB, 2)) MB)" -ForegroundColor Green

# Check CUDA runtime (optional but should be present for NVIDIA systems)
$cudartFiles = @(Get-ChildItem -Path $win64Dir -Filter "cudart64_*.dll" -File -ErrorAction SilentlyContinue)
if ($cudartFiles.Count -eq 0) {
    Write-Host "[WARN] No CUDA runtime DLL found (cudart64_*.dll)" -ForegroundColor Yellow
} else {
    foreach ($cudart in $cudartFiles) {
        Write-Host "[PASS] Found $($cudart.Name)" -ForegroundColor Green
    }
}

# Check TensorRT provider (optional)
$tensorrtProvider = @(Get-ChildItem -Path $win64Dir -Filter "onnxruntime_providers_*tensorrt*.dll" -File -ErrorAction SilentlyContinue)
if ($tensorrtProvider.Count -eq 0) {
    Write-Host "[INFO] No TensorRT provider found (DirectML will be used)" -ForegroundColor Cyan
} else {
    foreach ($provider in $tensorrtProvider) {
        Write-Host "[PASS] Found $($provider.Name)" -ForegroundColor Green
    }
}

# Check Windows universal GPU providers
$universalProviderDlls = @(
    "onnxruntime_providers_dml.dll",
    "onnxruntime_providers_winml.dll",
    "onnxruntime_providers_openvino.dll"
)
$foundUniversalProviders = @()
foreach ($provider in $universalProviderDlls) {
    $path = Join-Path $win64Dir $provider
    if (Test-Path $path) {
        $foundUniversalProviders += $provider
        Write-Host "[PASS] Found $provider" -ForegroundColor Green
    }
}
if ($foundUniversalProviders.Count -eq 0) {
    $message = "No Windows universal GPU provider DLL found; AMD/Intel systems will fall back to CPU."
    if ($expectsUniversalGpuPath) {
        Write-Host "[FAIL] $message" -ForegroundColor Red
        throw $message
    }
    Write-Host "[WARN] $message" -ForegroundColor Yellow
}

# Check models
$requiredModels = @(
    "corridorkey_fp16_512.onnx",
    "corridorkey_fp16_768.onnx",
    "corridorkey_fp16_1024.onnx",
    "corridorkey_fp16_1536.onnx",
    "corridorkey_int8_512.onnx",
    "corridorkey_int8_768.onnx",
    "corridorkey_int8_1024.onnx"
)

$optionalModels = @(
    "corridorkey_fp16_2048.onnx"
)

foreach ($model in $requiredModels) {
    $path = Join-Path $resourcesDir $model
    if (-not (Test-Path $path)) {
        Write-Host "[FAIL] Required model not found: $model" -ForegroundColor Red
        throw "Missing required model: $model"
    }
    $modelSize = (Get-Item $path).Length
    Write-Host "[PASS] Found $model ($([math]::Round($modelSize / 1MB, 2)) MB)" -ForegroundColor Green
}

foreach ($model in $optionalModels) {
    $path = Join-Path $resourcesDir $model
    if (-not (Test-Path $path)) {
        Write-Host "[INFO] Optional model not found: $model" -ForegroundColor Cyan
    } else {
        $modelSize = (Get-Item $path).Length
        Write-Host "[PASS] Found $model ($([math]::Round($modelSize / 1MB, 2)) MB)" -ForegroundColor Green
    }
}

Write-Host ""
Write-Host "================================" -ForegroundColor Green
Write-Host "Bundle validation PASSED" -ForegroundColor Green
Write-Host "================================" -ForegroundColor Green
Write-Host ""
Write-Host "Bundle is ready for installation and should work with DaVinci Resolve."
