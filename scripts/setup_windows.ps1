# CorridorKey Runtime — Windows Development Setup (Universal GPU Edition)
# This script automates the environment configuration for Windows developers with multi-GPU support.

$ErrorActionPreference = "Stop"

Write-Host "--- CorridorKey Runtime: Windows Universal Setup ---" -ForegroundColor Cyan

# 1. Locate Visual Studio 2022
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vsPath = & $vswhere -latest -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$vsPath) { Write-Error "Visual Studio 2022 not found." }
Write-Host "Found Visual Studio at: $vsPath" -ForegroundColor Green

# 2. Universal AI Runtimes (NVIDIA + AMD + Intel)
$vendorDir = Join-Path $PSScriptRoot "..\vendor"
if (!(Test-Path $vendorDir)) { New-Item -ItemType Directory -Path $vendorDir -Force | Out-Null }

$ortTarget = Join-Path $vendorDir "onnxruntime-universal"
if (!(Test-Path $ortTarget)) {
    New-Item -ItemType Directory -Path $ortTarget -Force | Out-Null
    
    # Pillar 1: DirectML
    Write-Host "Downloading DirectML Core..." -ForegroundColor Yellow
    $dmlUrl = "https://www.nuget.org/api/v2/package/Microsoft.AI.DirectML/1.15.2"
    $dmlZip = Join-Path $vendorDir "dml.zip"
    Invoke-WebRequest -Uri $dmlUrl -OutFile $dmlZip
    Expand-Archive -Path $dmlZip -DestinationPath "$vendorDir\dml-tmp" -Force
    Copy-Item "$vendorDir\dml-tmp\bin\x64-win\DirectML.dll" $ortTarget -Force
    Remove-Item $dmlZip -Force
    Remove-Item "$vendorDir\dml-tmp" -Recurse -Force

    # Pillar 2: ONNX Runtime
    $srcOrt = Join-Path $vendorDir "onnxruntime-windows-rtx"
    if (Test-Path $srcOrt) {
        Write-Host "Merging with existing RTX runtime..." -ForegroundColor Green
        Copy-Item "$srcOrt\bin\*" $ortTarget -Recurse -Force
        Copy-Item "$srcOrt\include\*" $ortTarget -Recurse -Force
        Copy-Item "$srcOrt\lib\*" $ortTarget -Recurse -Force
    }
}

# 3. Finalize Paths (STRICT TRIM)
$vcpkgRoot = "C:\tools\vcpkg"
$ortRoot = [System.IO.Path]::GetFullPath($ortTarget).TrimEnd("\")
$cudaHome = [System.IO.Path]::GetFullPath((Join-Path $vendorDir "cuda-12.9.1-local")).TrimEnd("\")

# 4. Run CMake Configuration and Build (ULTRA STATIC MODE)
Write-Host "Running Deep Static Configuration and Build (Release preset)..."
$vsDevCmd = Join-Path $vsPath "Common7\Tools\VsDevCmd.bat"

# Force static triplet and static CRT at configuration time
$fullCmd = "call `"$vsDevCmd`" -arch=x64 && set `"VCPKG_ROOT=$vcpkgRoot`" && set `"CUDA_PATH=$cudaHome`" && set `"CORRIDORKEY_WINDOWS_ORT_ROOT=$ortRoot`" && cmake --preset release -DVCPKG_TARGET_TRIPLET=x64-windows-static -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded && cmake --build --preset release"
& cmd.exe /c $fullCmd

if ($LASTEXITCODE -eq 0) {
    Write-Host "`n--- Setup and Build Complete (Universal GPU Edition)! ---" -ForegroundColor Cyan
} else {
    Write-Error "Build failed."
}
