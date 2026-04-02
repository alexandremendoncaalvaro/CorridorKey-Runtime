param(
    [string]$BuildDir = "",
    [string]$ModelsDir = "",
    [string]$OutputDir = "",
    [string]$ArtifactManifestPath = "",
    [string]$ModelProfile = "windows-rtx",
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
if ([string]::IsNullOrWhiteSpace($OutputDir)) {
    $OutputDir = Join-Path $repoRoot "dist\CorridorKey.ofx.bundle"
}

$pluginBinary = Join-Path $BuildDir "src\plugins\ofx\CorridorKey.ofx"
$cliBinary = Join-Path $BuildDir "src\cli\corridorkey.exe"
$runtimeServerBinary = Join-Path $BuildDir "src\plugins\ofx\corridorkey_ofx_runtime_server.exe"
$win64Dir = Join-Path $OutputDir "Contents\Win64"
$resourcesDir = Join-Path $OutputDir "Contents\Resources\models"
$modelInventoryPath = Join-Path $OutputDir "model_inventory.json"
$artifactManifestOutputPath = Join-Path $OutputDir "artifact_manifest.json"

function Assert-FileExists {
    param([string]$Path, [string]$Message)
    if (-not (Test-Path $Path)) {
        throw $Message
    }
}

function Copy-StagedRuntimeDlls {
    param(
        [string]$SourceDir,
        [string]$DestinationDir
    )

    if (-not (Test-Path $SourceDir)) {
        return
    }

    Get-ChildItem -Path $SourceDir -Filter "*.dll" -File -ErrorAction SilentlyContinue |
        ForEach-Object {
            Copy-Item $_.FullName $DestinationDir -Force
        }
}

if (Test-Path $OutputDir) {
    Remove-Item $OutputDir -Recurse -Force
}

New-Item -ItemType Directory -Path $win64Dir -Force | Out-Null
New-Item -ItemType Directory -Path $resourcesDir -Force | Out-Null

Assert-FileExists -Path $pluginBinary -Message "OpenFX plugin binary not found at $pluginBinary"
Assert-FileExists -Path $cliBinary -Message "CLI binary not found at $cliBinary"
Assert-FileExists -Path $runtimeServerBinary -Message "Runtime server binary not found at $runtimeServerBinary"
Copy-Item $pluginBinary $win64Dir -Force
Copy-Item $cliBinary $win64Dir -Force
Copy-Item $runtimeServerBinary $win64Dir -Force


# Minimum DLLs required for Torch-TensorRT FP16 inference on NVIDIA RTX GPUs.
# Builder resources (nvinfer_builder_resource_sm*.dll, cudnn_engines_*.dll) are
# excluded — they are only needed during engine compilation, not at inference time.
# onnxruntime.dll and its providers are also excluded: the Windows RTX release is
# Torch-TRT exclusive.
$kTorchTrtRequiredDlls = @(
    # Core TorchTRT runtime
    "torchtrt.dll",
    # PyTorch core
    "torch.dll",
    "torch_cpu.dll",
    "torch_cuda.dll",
    "torch_global_deps.dll",
    # C10 runtime
    "c10.dll",
    "c10_cuda.dll",
    # TensorRT inference runtime (no builder resources)
    "nvinfer_10.dll",
    "nvinfer_plugin_10.dll",
    # CUDA runtime
    "cudart64_12.dll",
    "cudart64_13.dll",
    # cuBLAS
    "cublas64_13.dll",
    "cublasLt64_13.dll",
    # cuDNN inference (ops only, not engines/precompiled)
    "cudnn64_9.dll",
    "cudnn_adv64_9.dll",
    "cudnn_cnn64_9.dll",
    "cudnn_graph64_9.dll",
    "cudnn_heuristic64_9.dll",
    "cudnn_ops64_9.dll",
    # cuSPARSE, cuSOLVER required by Torch internals
    "cusparse64_12.dll",
    "cusolver64_12.dll",
    # cuFFT required by some Torch ops
    "cufft64_12.dll",
    "cufftw64_12.dll",
    # NVRTC for JIT kernels
    "nvrtc64_130_0.dll",
    "nvrtc-builtins64_130.dll",
    "nvJitLink_130_0.dll",
    # CUDA Profiling Tools Interface (implicitly required by torch_cpu.dll)
    "cupti64_*.dll",
    # OpenMP runtime (Torch CPU dependency)
    "libiomp5md.dll",
    # NVTX profiling (small, no-op stub in release)
    "nvToolsExt64_1.dll",
    # Caffe2 NVRTC wrapper
    "caffe2_nvrtc.dll",
    # TorchTRT Python/UV runtime deps
    "torch_python.dll",
    "uv.dll",
    # PyTorch shared memory transport (required by torch_cpu.dll)
    "shm.dll",
    # zlib (I/O dep)
    "zlibwapi.dll"
)

$srcDllDir = Split-Path -Parent $pluginBinary
Write-Host "Staging Torch-TensorRT runtime DLLs (allowlist only)..." -ForegroundColor Cyan
$copied = 0
$skipped = 0
foreach ($dllName in $kTorchTrtRequiredDlls) {
    $srcPath = Join-Path $srcDllDir $dllName
    if (Test-Path $srcPath) {
        Copy-Item $srcPath $win64Dir -Force
        $copied++
    } else {
        Write-Host "  [SKIP] Not found in build output: $dllName" -ForegroundColor DarkGray
        $skipped++
    }
}
Write-Host "  Copied $copied DLLs, skipped $skipped not-present entries." -ForegroundColor Cyan

$targetModels = Get-CorridorKeyOfxBundleTargetModels -ModelProfile $ModelProfile
if ($Skip2048.IsPresent) {
    $targetModels = @($targetModels | Where-Object { $_ -ne "corridorkey_torchtrt_fp16_2048.ts" })
}

$modelInventory = Get-CorridorKeyModelInventory -ModelsDir $ModelsDir -ExpectedModels $targetModels

foreach ($model in $modelInventory.present_models) {
    $sourcePath = Join-Path $ModelsDir $model
    Copy-Item $sourcePath $resourcesDir -Force
}

$inventoryPayload = [ordered]@{
    package_type = "Windows Torch-TRT Release"
    models_dir = [System.IO.Path]::GetFullPath($ModelsDir)
    expected_models = @($modelInventory.expected_models)
    present_models = @($modelInventory.present_models)
    missing_models = @($modelInventory.missing_models)
    present_count = $modelInventory.present_count
    missing_count = $modelInventory.missing_count
}
Write-CorridorKeyJsonFile -Path $modelInventoryPath -Payload $inventoryPayload

if ($modelInventory.missing_count -gt 0) {
    Write-Host "[INFO] Packaging OFX bundle with partial model coverage: $($modelInventory.missing_models -join ', ')" -ForegroundColor Cyan
    Write-Host "[INFO] Wrote model inventory: $modelInventoryPath" -ForegroundColor Cyan
} else {
    Write-Host "[PASS] All targeted OFX models were packaged." -ForegroundColor Green
}

Write-Host "OpenFX bundle ready at: $OutputDir"
