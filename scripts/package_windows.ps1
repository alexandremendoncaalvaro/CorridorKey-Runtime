# CorridorKey Runtime — Ultimate Windows Packaging Script (GUI + Engine)
# This script creates a 100% portable "Zero-Install" folder for distribution.

$ErrorActionPreference = "Stop"
$projectRoot = $PSScriptRoot | Split-Path -Parent
$distDir = Join-Path $projectRoot "dist\CorridorKey-Windows-Portable"
$engineBin = Join-Path $projectRoot "build\release\src\cli\corridorkey.exe"
$guiBin = Join-Path $projectRoot "src\gui\src-tauri\target\release\CorridorKey.exe"
$vendorDir = Join-Path $projectRoot "vendor"
$modelsDir = Join-Path $projectRoot "models"

Write-Host "--- Packaging CorridorKey Ultimate Portable Bundle ---" -ForegroundColor Cyan

# 1. Clean and Setup
if (Test-Path $distDir) { Remove-Item $distDir -Recurse -Force }
New-Item -ItemType Directory -Path $distDir -Force | Out-Null
$sidecarDir = New-Item -ItemType Directory -Path (Join-Path $distDir "bin") -Force
$modelsDistDir = New-Item -ItemType Directory -Path (Join-Path $distDir "models") -Force

# 2. Copy Engine & Runtimes (Sidecar Folder)
Write-Host "[1/4] Copying Native Engine and AI Runtimes..."
Copy-Item $engineBin $sidecarDir -Force

# AI DLLs (DirectML, ONNX, CUDA)
$universalOrt = Join-Path $vendorDir "onnxruntime-universal"
$cudaBin = Join-Path $vendorDir "cuda-12.9.1-local\bin"

if (Test-Path $universalOrt) { Copy-Item (Join-Path $universalOrt "*.dll") $sidecarDir -Force }
if (Test-Path $cudaBin) { Copy-Item (Join-Path $cudaBin "*.dll") $sidecarDir -Force }

# 3. Copy GUI (Main Executable)
Write-Host "[2/4] Copying GUI Frontend..."
if (Test-Path $guiBin) {
    Copy-Item $guiBin $distDir -Force
} else {
    Write-Host "Warning: GUI Binary not found. Run 'pnpm tauri build' in src/gui first." -ForegroundColor Yellow
}

# 4. Copy Models
Write-Host "[3/4] Copying Models..."
$targetModels = @("corridorkey_fp16_512.onnx", "corridorkey_fp16_768.onnx", "corridorkey_int8_512.onnx")
foreach ($m in $targetModels) {
    $src = Join-Path $modelsDir $m
    if (Test-Path $src) { Copy-Item $src $modelsDistDir -Force }
}

# 5. Create Metadata / Smoke Test
Write-Host "[4/4] Finalizing Bundle..."
$readme = @"
# CorridorKey Portable v0.1.4
To run: Double-click CorridorKey.exe

Requirements:
- Windows 11
- NVIDIA (RTX/GTX) or AMD/Intel GPU with latest drivers.
"@
$readme | Out-File (Join-Path $distDir "README.txt") -Encoding ascii

Write-Host "`n--- Bundle Ready at: $distDir ---" -ForegroundColor Green
Write-Host "You can now ZIP this folder and share it with testers."
