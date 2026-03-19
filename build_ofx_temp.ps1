$ErrorActionPreference = "Stop"

Write-Host "Configuring Visual Studio environment..." -ForegroundColor Cyan

# Find VS 2022
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vsPath = & $vswhere -latest -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$vsPath) { Write-Error "Visual Studio 2022 not found." }
Write-Host "Found Visual Studio at: $vsPath" -ForegroundColor Green

# Setup paths
$vsDevCmd = Join-Path $vsPath "Common7\Tools\VsDevCmd.bat"
$vendorDir = "C:\Dev\CorridorKey-Runtime\vendor"
$ortRoot = Join-Path $vendorDir "onnxruntime-windows-rtx"

# Setup Windows SDK
$windowsSdkDir = "C:\Program Files (x86)\Windows Kits\10"
$sdkLibPath = "$windowsSdkDir\Lib\10.0.26100.0"

# Build command
Write-Host "Building OFX plugin..." -ForegroundColor Cyan
$ortInclude = "$ortRoot/include".Replace('\', '/')

# Force MSVC compiler (not GCC from w64devkit)
$msvcCompiler = "$vsPath\VC\Tools\MSVC\14.44.35207\bin\Hostx64\x64\cl.exe"
# MSVC uses /I instead of -I for include paths
$fullCmd = "call `"$vsDevCmd`" -arch=x64 && set `"VCPKG_ROOT=C:\tools\vcpkg`" && set `"CORRIDORKEY_WINDOWS_ORT_ROOT=$ortRoot`" && set `"WindowsSdkDir=$windowsSdkDir`" && set `"WindowsSDKLibVersion=10.0.26100.0\`" && set `"CMAKE_CXX_COMPILER=$msvcCompiler`" && set `"CMAKE_C_COMPILER=$msvcCompiler`" && set `"CMAKE_CXX_FLAGS=/I$ortInclude`" && cmake --preset release && cmake --build --preset release --target corridorkey_ofx"

& cmd.exe /c $fullCmd

if ($LASTEXITCODE -eq 0) {
    Write-Host "`nBuild successful!" -ForegroundColor Green
} else {
    Write-Error "Build failed with exit code $LASTEXITCODE"
}
