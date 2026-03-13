# CorridorKey Runtime — Ultimate Portable Packaging Script (FIXED)
# This script ensures a 100% reliable bundle with the sidecar named 'ck-engine.exe'.

$ErrorActionPreference = "Stop"
$projectRoot = $PSScriptRoot | Split-Path -Parent
$distDir = Join-Path $projectRoot "dist\CorridorKey-Windows-Portable"
$engineBuildDir = Join-Path $projectRoot "build\release\src\cli"
$guiBin = Join-Path $projectRoot "src\gui\src-tauri\target\release\corridorkey-gui.exe"
$vendorDir = Join-Path $projectRoot "vendor"
$modelsDir = Join-Path $projectRoot "models"

Write-Host "--- Packaging CorridorKey Ultimate (Anti-Loop Version) ---" -ForegroundColor Cyan

# 1. Clean and Setup
if (Test-Path $distDir) { Remove-Item $distDir -Recurse -Force }
New-Item -ItemType Directory -Path $distDir -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $distDir "models") -Force | Out-Null

# 2. Copy Sidecar Engine as 'ck-engine.exe' (No suffix, as proven to work)
Write-Host "[1/4] Copying Sidecar Engine as 'ck-engine.exe'..."
Copy-Item (Join-Path $engineBuildDir "corridorkey.exe") (Join-Path $distDir "ck-engine.exe") -Force

# 3. Copy ALL DLLs to Root
Write-Host "[2/4] Copying all Runtimes and Core DLLs to root..."
Copy-Item (Join-Path $engineBuildDir "*.dll") $distDir -Force

# AI Runtimes from vendor
$universalOrt = Join-Path $vendorDir "onnxruntime-universal"
$cudaBin = Join-Path $vendorDir "cuda-12.9.1-local\bin"
if (Test-Path $universalOrt) { Copy-Item (Join-Path $universalOrt "*.dll") $distDir -Force }
if (Test-Path $cudaBin) { Copy-Item (Join-Path $cudaBin "*.dll") $distDir -Force }

# 4. Copy GUI (Main Application)
Write-Host "[3/4] Copying GUI Frontend as 'CorridorKey.exe'..."
if (Test-Path $guiBin) {
    Copy-Item $guiBin (Join-Path $distDir "CorridorKey.exe") -Force
} else {
    Write-Error "GUI Binary not found. Run 'pnpm tauri build' in src/gui first."
}

# 5. Copy Models
Write-Host "[4/4] Copying Models..."
$targetModels = @("corridorkey_fp16_512.onnx", "corridorkey_fp16_768.onnx", "corridorkey_int8_512.onnx")
foreach ($m in $targetModels) {
    $src = Join-Path $modelsDir $m
    if (Test-Path $src) { Copy-Item $src (Join-Path $distDir "models") -Force }
}

Write-Host "`n--- Bundle Ready at: $distDir ---" -ForegroundColor Green
Write-Host "Collision-free version. Double-click CorridorKey.exe to start."
