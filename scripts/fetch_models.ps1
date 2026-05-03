<#
.SYNOPSIS
    Downloads CorridorKey model files from Hugging Face Hub into the local models/ directory.

.DESCRIPTION
    Fetches ONNX, Torch-TensorRT TorchScript, MLX, and PyTorch model files from the Hugging Face repository
    (alexandrealvaro/CorridorKey) into the local models/ directory. The release
    pipeline and runtime continue to read models from this directory unchanged.

    By default, the Windows RTX profile downloads the FP16 ONNX ladder and any
    packaged Torch-TensorRT TorchScript artifacts used by the experimental
    Windows RTX engine selector. INT8 ONNX models were retired together with
    CPU rendering: FP16 is now the only quality the runtime ships.

.PARAMETER Profile
    Model set to download:
      windows-rtx       : FP16 ONNX models + Torch-TensorRT TorchScript artifacts (default)
      windows-rtx-blue  : Dedicated CorridorKeyBlue Torch-TensorRT (.ts) engines for blue-screen plates (512/1024 FP16, 1536/2048 FP32)
      windows-turing-source : checkpoint + reference FP16 ONNX models for the Turing collaboration kit
      windows-all       : FP16 + FP16 context ONNX models + Torch-TensorRT TorchScript artifacts + blue
      apple             : MLX safetensors + bridge files
      pytorch           : Training checkpoint (.pth)
      all               : Everything

.PARAMETER HfRepo
    Hugging Face repository identifier. Defaults to alexandrealvaro/CorridorKey.

.PARAMETER Revision
    Branch, tag, or commit to download from. Defaults to main.

.PARAMETER Force
    Re-download files even if they already exist locally.

.EXAMPLE
    .\scripts\fetch_models.ps1
    .\scripts\fetch_models.ps1 -Profile all
    .\scripts\fetch_models.ps1 -Profile apple -Revision v0.5
#>

param(
    [ValidateSet("windows-rtx", "windows-rtx-blue", "windows-turing-source", "windows-all",
                 "apple", "pytorch", "hint-tracker", "all")]
    [string]$Profile = "windows-rtx",

    [string]$HfRepo = "alexandrealvaro/CorridorKey",
    # Separate repo for the dedicated CorridorKeyBlue ladder. The user-visible
    # canonical home for CorridorKey assets is alexandrealvaro/corridorkey-models;
    # the green ladder still lives at $HfRepo above and will be migrated
    # separately. Keeping the two repos addressable here lets us ship blue
    # without blocking on the green migration.
    [string]$HfBlueRepo = "alexandrealvaro/corridorkey-models",
    [string]$Revision = "main",
    [switch]$Force
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$modelsDir = Join-Path $repoRoot "models"

if (-not (Test-Path $modelsDir)) {
    New-Item -ItemType Directory -Path $modelsDir | Out-Null
}

$hfBaseUrl = "https://huggingface.co/$HfRepo/resolve/$Revision"
$hfBlueBaseUrl = "https://huggingface.co/$HfBlueRepo/resolve/$Revision"

$windowsRtxFiles = @{
    "onnx/fp16/corridorkey_fp16_512.onnx"   = "corridorkey_fp16_512.onnx"
    "onnx/fp16/corridorkey_fp16_1024.onnx"  = "corridorkey_fp16_1024.onnx"
    "onnx/fp16/corridorkey_fp16_1536.onnx"  = "corridorkey_fp16_1536.onnx"
    "onnx/fp16/corridorkey_fp16_2048.onnx"  = "corridorkey_fp16_2048.onnx"
}

# Sprint 1 PR 3 swapped the blue ladder from ONNX/TRT-RTX-EP to
# Torch-TensorRT (.ts) compiled engines. Filenames + precision must match
# src/app/runtime_contracts.cpp (`make_model_entry("fp16-blue", ...)` /
# `"fp32-blue"`) and src/plugins/ofx/ofx_model_selection.hpp
# (`artifact_path_for_backend`, `kBluePrecisionFp32Threshold`). 512 /
# 1024 are FP16; 1536 / 2048 use FP32 because Sprint 0 measured FP16
# NaNs at those graph sizes for the blue checkpoint (see
# temp/blue-diagnose/HANDOFF.md section 2.2).
$windowsRtxBlueFiles = @{
    "torchtrt/fp16-blue/corridorkey_blue_torchtrt_fp16_512.ts"  = "corridorkey_blue_torchtrt_fp16_512.ts"
    "torchtrt/fp16-blue/corridorkey_blue_torchtrt_fp16_1024.ts" = "corridorkey_blue_torchtrt_fp16_1024.ts"
    "torchtrt/fp32-blue/corridorkey_blue_torchtrt_fp32_1536.ts" = "corridorkey_blue_torchtrt_fp32_1536.ts"
    "torchtrt/fp32-blue/corridorkey_blue_torchtrt_fp32_2048.ts" = "corridorkey_blue_torchtrt_fp32_2048.ts"
}

$windowsTorchTensorRtFiles = @{
    "torchtrt/fp16/corridorkey_torchtrt_fp16_512.ts"  = "corridorkey_torchtrt_fp16_512.ts"
    "torchtrt/fp16/corridorkey_torchtrt_fp16_1024.ts" = "corridorkey_torchtrt_fp16_1024.ts"
    "torchtrt/fp16/corridorkey_torchtrt_fp16_1536.ts" = "corridorkey_torchtrt_fp16_1536.ts"
    "torchtrt/fp16/corridorkey_torchtrt_fp16_2048.ts" = "corridorkey_torchtrt_fp16_2048.ts"
}

$windowsTuringSourceFiles = @{
    "onnx/fp16/corridorkey_fp16_512.onnx"   = "corridorkey_fp16_512.onnx"
    "onnx/fp16/corridorkey_fp16_1024.onnx"  = "corridorkey_fp16_1024.onnx"
    "onnx/fp16/corridorkey_fp16_1536.onnx"  = "corridorkey_fp16_1536.onnx"
    "onnx/fp16/corridorkey_fp16_2048.onnx"  = "corridorkey_fp16_2048.onnx"
    "pytorch/CorridorKey.pth"               = "CorridorKey.pth"
}

$windowsCtxFiles = @{
    "onnx/fp16_ctx/corridorkey_fp16_512_ctx.onnx"   = "corridorkey_fp16_512_ctx.onnx"
    "onnx/fp16_ctx/corridorkey_fp16_1024_ctx.onnx"  = "corridorkey_fp16_1024_ctx.onnx"
    "onnx/fp16_ctx/corridorkey_fp16_1536_ctx.onnx"  = "corridorkey_fp16_1536_ctx.onnx"
    "onnx/fp16_ctx/corridorkey_fp16_2048_ctx.onnx"  = "corridorkey_fp16_2048_ctx.onnx"
}

$appleFiles = @{
    "mlx/corridorkey_mlx.safetensors"            = "corridorkey_mlx.safetensors"
    "mlx/corridorkey_mlx_bridge_512.mlxfn"       = "corridorkey_mlx_bridge_512.mlxfn"
    "mlx/corridorkey_mlx_bridge_768.mlxfn"       = "corridorkey_mlx_bridge_768.mlxfn"
    "mlx/corridorkey_mlx_bridge_1024.mlxfn"      = "corridorkey_mlx_bridge_1024.mlxfn"
    "mlx/corridorkey_mlx_bridge_1536.mlxfn"      = "corridorkey_mlx_bridge_1536.mlxfn"
    "mlx/corridorkey_mlx_bridge_2048.mlxfn"      = "corridorkey_mlx_bridge_2048.mlxfn"
}

$pytorchFiles = @{
    "pytorch/CorridorKey.pth" = "CorridorKey.pth"
}

$hintTrackerFiles = @{
    "hint/mobilesam_image_encoder_fp16.onnx"  = "mobilesam_image_encoder_fp16.onnx"
    "hint/mobilesam_prompt_decoder_fp16.onnx" = "mobilesam_prompt_decoder_fp16.onnx"
    "hint/cutie_feature_extractor_fp16.onnx"  = "cutie_feature_extractor_fp16.onnx"
    "hint/cutie_init_memory_fp16.onnx"        = "cutie_init_memory_fp16.onnx"
    "hint/cutie_tracking_memory_fp16.onnx"    = "cutie_tracking_memory_fp16.onnx"
}

$filesToDownload = @{}
# Files in $filesToDownloadBlue are fetched from $HfBlueRepo
# (alexandrealvaro/corridorkey-models) instead of $HfRepo. Tracked separately
# while the green ladder migration to the canonical repo is pending.
$filesToDownloadBlue = @{}

switch ($Profile) {
    "windows-rtx" {
        $filesToDownload = $windowsRtxFiles.Clone()
        foreach ($entry in $windowsTorchTensorRtFiles.GetEnumerator()) {
            $filesToDownload[$entry.Key] = $entry.Value
        }
        foreach ($entry in $hintTrackerFiles.GetEnumerator()) {
            $filesToDownload[$entry.Key] = $entry.Value
        }
    }
    "windows-rtx-blue" {
        $filesToDownloadBlue = $windowsRtxBlueFiles.Clone()
    }
    "windows-turing-source" {
        $filesToDownload = $windowsTuringSourceFiles.Clone()
    }
    "windows-all" {
        $filesToDownload = $windowsRtxFiles.Clone()
        foreach ($entry in $windowsCtxFiles.GetEnumerator()) {
            $filesToDownload[$entry.Key] = $entry.Value
        }
        foreach ($entry in $windowsTorchTensorRtFiles.GetEnumerator()) {
            $filesToDownload[$entry.Key] = $entry.Value
        }
        foreach ($entry in $hintTrackerFiles.GetEnumerator()) {
            $filesToDownload[$entry.Key] = $entry.Value
        }
        $filesToDownloadBlue = $windowsRtxBlueFiles.Clone()
    }
    "apple" {
        $filesToDownload = $appleFiles.Clone()
        foreach ($entry in $hintTrackerFiles.GetEnumerator()) {
            $filesToDownload[$entry.Key] = $entry.Value
        }
    }
    "pytorch" {
        $filesToDownload = $pytorchFiles.Clone()
    }
    "hint-tracker" {
        $filesToDownload = $hintTrackerFiles.Clone()
    }
    "all" {
        foreach ($table in @($windowsRtxFiles, $windowsCtxFiles, $windowsTorchTensorRtFiles, $appleFiles, $pytorchFiles, $hintTrackerFiles)) {
            foreach ($entry in $table.GetEnumerator()) {
                $filesToDownload[$entry.Key] = $entry.Value
            }
        }
        $filesToDownloadBlue = $windowsRtxBlueFiles.Clone()
    }
}

$totalFiles = $filesToDownload.Count + $filesToDownloadBlue.Count
Write-Host "[fetch-models] Profile: $Profile ($totalFiles files)"
Write-Host "[fetch-models] Source:  $HfRepo@$Revision"
if ($filesToDownloadBlue.Count -gt 0) {
    Write-Host "[fetch-models] Blue source: $HfBlueRepo@$Revision"
}
Write-Host "[fetch-models] Target:  $modelsDir"
Write-Host ""

$downloaded = 0
$skipped = 0
$failed = 0

# Iterate green files (default $HfRepo) and blue files ($HfBlueRepo) with the
# correct base URL for each. Output names land in models/ with the same
# filename regardless of which repo they came from.
$allEntries = @()
foreach ($entry in $filesToDownload.GetEnumerator()) {
    $allEntries += [pscustomobject]@{
        RemotePath = $entry.Key; LocalName = $entry.Value; BaseUrl = $hfBaseUrl
    }
}
foreach ($entry in $filesToDownloadBlue.GetEnumerator()) {
    $allEntries += [pscustomobject]@{
        RemotePath = $entry.Key; LocalName = $entry.Value; BaseUrl = $hfBlueBaseUrl
    }
}

foreach ($entry in $allEntries | Sort-Object LocalName) {
    $remotePath = $entry.RemotePath
    $localName = $entry.LocalName
    $localPath = Join-Path $modelsDir $localName
    $url = "$($entry.BaseUrl)/$remotePath"

    if ((Test-Path $localPath) -and -not $Force.IsPresent) {
        Write-Host "  [skip] $localName (already exists, use -Force to re-download)"
        $skipped++
        continue
    }

    Write-Host "  [download] $localName ..." -NoNewline
    try {
        $tempPath = "$localPath.download"
        Invoke-WebRequest -Uri $url -OutFile $tempPath -UseBasicParsing
        Move-Item -Path $tempPath -Destination $localPath -Force
        $sizeMb = [math]::Round((Get-Item $localPath).Length / 1MB, 1)
        Write-Host " OK (${sizeMb} MB)" -ForegroundColor Green
        $downloaded++
    } catch {
        Write-Host " FAILED" -ForegroundColor Red
        Write-Host "    Error: $($_.Exception.Message)" -ForegroundColor Red
        if (Test-Path "$localPath.download") {
            Remove-Item "$localPath.download" -Force -ErrorAction SilentlyContinue
        }
        $failed++
    }
}

Write-Host ""
Write-Host "[fetch-models] Done: $downloaded downloaded, $skipped skipped, $failed failed."

if ($failed -gt 0) {
    Write-Host "[fetch-models] Some downloads failed. Re-run the script to retry." -ForegroundColor Yellow
    exit 1
}
