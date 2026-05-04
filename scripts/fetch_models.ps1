<#
.SYNOPSIS
    Downloads CorridorKey model files from Hugging Face Hub into the local models/ directory.

.DESCRIPTION
    Fetches the runtime model variants (ONNX, Torch-TensorRT TorchScript,
    MLX, hint-tracker fixtures) from alexandrealvaro/CorridorKey into the
    local models/ directory. Training checkpoints (.pth) are pulled
    direct from the upstream nikopueringer/* repos and never republished
    by us.

    INT8 ONNX models stay shipped for the browser experiment; the
    OFX render path uses FP16 + TensorRT-RTX EP exclusively.

.PARAMETER Profile
    Model set to download:
      windows-rtx           : FP16 ONNX models + Torch-TensorRT TorchScript engines (default)
      windows-rtx-blue      : Dedicated CorridorKeyBlue dynamic TorchScript (.ts) model for blue-screen plates
      windows-turing-source : Reference FP16 ONNX ladder + upstream green training checkpoint
      windows-all           : FP16 + FP16 context ONNX models + Torch-TensorRT (green and blue)
      apple                 : MLX safetensors + bridge files
      pytorch               : Upstream green and blue training checkpoints (from nikopueringer/*)
      hint-tracker          : MobileSAM + Cutie auxiliary fixtures for alpha-hint generation
      all                   : Everything

.PARAMETER HfRepo
    Hugging Face repository for our re-exported runtime variants. Defaults to alexandrealvaro/CorridorKey.

.PARAMETER HfUpstreamGreenRepo
    Upstream green training checkpoint repo. Defaults to nikopueringer/CorridorKey_v1.0.

.PARAMETER HfUpstreamBlueRepo
    Upstream blue training checkpoint repo. Defaults to nikopueringer/CorridorKeyBlue_1.0.

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
    # Upstream training checkpoints. The runtime ships the re-exported
    # ONNX / Torch-TensorRT / MLX variants under $HfRepo above; the .pth
    # checkpoints themselves live at the canonical author-published
    # locations and we never republish them.
    [string]$HfUpstreamGreenRepo = "nikopueringer/CorridorKey_v1.0",
    [string]$HfUpstreamBlueRepo = "nikopueringer/CorridorKeyBlue_1.0",
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
$hfUpstreamGreenBaseUrl = "https://huggingface.co/$HfUpstreamGreenRepo/resolve/$Revision"
$hfUpstreamBlueBaseUrl = "https://huggingface.co/$HfUpstreamBlueRepo/resolve/$Revision"

$windowsRtxFiles = @{
    "onnx/fp16/corridorkey_fp16_512.onnx"   = "corridorkey_fp16_512.onnx"
    "onnx/fp16/corridorkey_fp16_1024.onnx"  = "corridorkey_fp16_1024.onnx"
    "onnx/fp16/corridorkey_fp16_1536.onnx"  = "corridorkey_fp16_1536.onnx"
    "onnx/fp16/corridorkey_fp16_2048.onnx"  = "corridorkey_fp16_2048.onnx"
}

# The blue Windows RTX pack is a single dynamic TorchScript artifact.
# Keep this filename aligned with src/app/runtime_contracts.cpp and
# src/plugins/ofx/ofx_model_selection.hpp.
$windowsRtxBlueFiles = @{
    "torchtrt/dynamic-blue/corridorkey_dynamic_blue_fp16.ts" = "corridorkey_dynamic_blue_fp16.ts"
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
}

# Upstream-hosted training checkpoints. We never republish these; fetch
# them direct from nikopueringer/* so the canonical source-of-truth and
# license terms stay attached. Keys are remote paths under the upstream
# repo, values are local filenames written under models/.
$upstreamGreenCheckpointFiles = @{
    "CorridorKey_v1.0.pth" = "CorridorKey_v1.0.pth"
}
$upstreamBlueCheckpointFiles = @{
    "CorridorKeyBlue_1.0.pth" = "CorridorKeyBlue_1.0.pth"
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

$hintTrackerFiles = @{
    "hint/mobilesam_image_encoder_fp16.onnx"  = "mobilesam_image_encoder_fp16.onnx"
    "hint/mobilesam_prompt_decoder_fp16.onnx" = "mobilesam_prompt_decoder_fp16.onnx"
    "hint/cutie_feature_extractor_fp16.onnx"  = "cutie_feature_extractor_fp16.onnx"
    "hint/cutie_init_memory_fp16.onnx"        = "cutie_init_memory_fp16.onnx"
    "hint/cutie_tracking_memory_fp16.onnx"    = "cutie_tracking_memory_fp16.onnx"
}

$filesToDownload = @{}
# Upstream training checkpoints fetched from nikopueringer/* instead of
# our own repo. Tracked separately because the base URL differs.
$filesToDownloadGreenUpstream = @{}
$filesToDownloadBlueUpstream = @{}

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
        $filesToDownload = $windowsRtxBlueFiles.Clone()
    }
    "windows-turing-source" {
        $filesToDownload = $windowsTuringSourceFiles.Clone()
        $filesToDownloadGreenUpstream = $upstreamGreenCheckpointFiles.Clone()
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
        foreach ($entry in $windowsRtxBlueFiles.GetEnumerator()) {
            $filesToDownload[$entry.Key] = $entry.Value
        }
    }
    "apple" {
        $filesToDownload = $appleFiles.Clone()
        foreach ($entry in $hintTrackerFiles.GetEnumerator()) {
            $filesToDownload[$entry.Key] = $entry.Value
        }
    }
    "pytorch" {
        $filesToDownloadGreenUpstream = $upstreamGreenCheckpointFiles.Clone()
        $filesToDownloadBlueUpstream = $upstreamBlueCheckpointFiles.Clone()
    }
    "hint-tracker" {
        $filesToDownload = $hintTrackerFiles.Clone()
    }
    "all" {
        foreach ($table in @($windowsRtxFiles, $windowsCtxFiles, $windowsTorchTensorRtFiles,
                             $windowsRtxBlueFiles, $appleFiles, $hintTrackerFiles)) {
            foreach ($entry in $table.GetEnumerator()) {
                $filesToDownload[$entry.Key] = $entry.Value
            }
        }
        $filesToDownloadGreenUpstream = $upstreamGreenCheckpointFiles.Clone()
        $filesToDownloadBlueUpstream = $upstreamBlueCheckpointFiles.Clone()
    }
}

$totalFiles = $filesToDownload.Count + $filesToDownloadGreenUpstream.Count +
              $filesToDownloadBlueUpstream.Count
Write-Host "[fetch-models] Profile: $Profile ($totalFiles files)"
Write-Host "[fetch-models] Source:  $HfRepo@$Revision"
if ($filesToDownloadGreenUpstream.Count -gt 0) {
    Write-Host "[fetch-models] Upstream green: $HfUpstreamGreenRepo@$Revision"
}
if ($filesToDownloadBlueUpstream.Count -gt 0) {
    Write-Host "[fetch-models] Upstream blue:  $HfUpstreamBlueRepo@$Revision"
}
Write-Host "[fetch-models] Target:  $modelsDir"
Write-Host ""

$downloaded = 0
$skipped = 0
$failed = 0

# Iterate runtime variants ($HfRepo) and upstream training checkpoints
# ($HfUpstreamGreenRepo / $HfUpstreamBlueRepo) with the correct base URL
# for each. Output names land in models/ with the same filename
# regardless of which repo they came from.
$allEntries = @()
foreach ($entry in $filesToDownload.GetEnumerator()) {
    $allEntries += [pscustomobject]@{
        RemotePath = $entry.Key; LocalName = $entry.Value; BaseUrl = $hfBaseUrl
    }
}
foreach ($entry in $filesToDownloadGreenUpstream.GetEnumerator()) {
    $allEntries += [pscustomobject]@{
        RemotePath = $entry.Key; LocalName = $entry.Value; BaseUrl = $hfUpstreamGreenBaseUrl
    }
}
foreach ($entry in $filesToDownloadBlueUpstream.GetEnumerator()) {
    $allEntries += [pscustomobject]@{
        RemotePath = $entry.Key; LocalName = $entry.Value; BaseUrl = $hfUpstreamBlueBaseUrl
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
