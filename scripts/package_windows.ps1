# CorridorKey Runtime — Windows Packaging Script
# This script prepares a portable distribution folder for Windows RTX users.

$ErrorActionPreference = "Stop"
$distDir = Join-Path $PSScriptRoot "..\dist\CorridorKey"
$binDir = Join-Path $PSScriptRoot "..\build\release\src\cli"
$vendorDir = Join-Path $PSScriptRoot "..\vendor"
$modelsDir = Join-Path $PSScriptRoot "..\models"

Write-Host "--- Packaging CorridorKey for Windows RTX ---" -ForegroundColor Cyan

# 1. Clean and create dist directory
if (Test-Path $distDir) { Remove-Item $distDir -Recurse -Force }
New-Item -ItemType Directory -Path $distDir -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $distDir "models") -Force | Out-Null

# 2. Copy Executable
Write-Host "Copying binary..."
Copy-Item (Join-Path $binDir "corridorkey.exe") $distDir -Force

# 3. Copy AI Runtimes (DLLs)
Write-Host "Copying AI runtimes (DLLs)..."
$ortBin = Join-Path $vendorDir "onnxruntime-windows-rtx\bin"
$cudaBin = Join-Path $vendorDir "cuda-12.9.1-local\bin"

# We copy DLLs directly to the executable folder for "Side-by-Side" loading
if (Test-Path $ortBin) { Copy-Item (Join-Path $ortBin "*.dll") $distDir -Force }
if (Test-Path $cudaBin) { Copy-Item (Join-Path $cudaBin "*.dll") $distDir -Force }

# 4. Copy and Pre-compile Models
Write-Host "Preparing models..."
$targetModels = @("corridorkey_fp16_512.onnx", "corridorkey_fp16_768.onnx")

foreach ($modelName in $targetModels) {
    $src = Join-Path $modelsDir $modelName
    if (Test-Path $src) {
        Copy-Item $src (Join-Path $distDir "models") -Force
        
        # Pre-compile Context (The Magic for Zero Startup)
        Write-Host "Pre-compiling context for $modelName (this takes a moment)..."
        $env:PATH = "$ortBin;$cudaBin;$env:PATH"
        $exe = Join-Path $distDir "corridorkey.exe"
        
        # Running a dummy process to trigger TensorRT compilation and ctx generation
        # The engine is smart enough to generate the _ctx.onnx if configured
        & $exe doctor --json | Out-Null 
    }
}

# 5. Create a simple launcher for users who still want terminal
$launcherContent = @"
@echo off
set "PATH=%~dp0;%PATH%"
"%~dp0corridorkey.exe" %*
"@
$launcherContent | Out-File (Join-Path $distDir "corridorkey.bat") -Encoding ascii

Write-Host "`n--- Packaging Complete! ---" -ForegroundColor Green
Write-Host "Location: $distDir"
Write-Host "Ready for GUI integration or Zipping."
