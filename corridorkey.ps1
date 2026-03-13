# CorridorKey Runtime — Execution Wrapper
# This script ensures the local AI DLLs (CUDA/TensorRT) are in the PATH before running the app.

$appPath = Join-Path $PSScriptRoot "build\release\src\cli\corridorkey.exe"
if (!(Test-Path $appPath)) {
    Write-Error "Binary not found at $appPath. Please run scripts\setup_windows.ps1 first."
    exit 1
}

# 1. Setup Portable PATH for AI Runtimes
$ortBin = Join-Path $PSScriptRoot "vendor\onnxruntime-windows-rtx\bin"
$cudaBin = Join-Path $PSScriptRoot "vendor\cuda-12.9.1-local\bin"

if (Test-Path $ortBin) { $env:PATH = "$ortBin;$env:PATH" }
if (Test-Path $cudaBin) { $env:PATH = "$cudaBin;$env:PATH" }

# 2. Execute with passed arguments
& $appPath @args
