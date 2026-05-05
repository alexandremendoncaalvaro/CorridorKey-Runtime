param(
    [ValidateSet("debug", "release", "release-lto")]
    [string]$Preset = "release",
    [string]$DisplayVersionLabel = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot "windows_runtime_helpers.ps1")

if ([string]::IsNullOrWhiteSpace($env:VCPKG_ROOT)) {
    throw "VCPKG_ROOT is required by CMakePresets.json. Set VCPKG_ROOT before running scripts\windows.ps1 -Task build."
}

if (-not (Test-Path $env:VCPKG_ROOT)) {
    throw "VCPKG_ROOT does not exist: $env:VCPKG_ROOT"
}

$isWindowsHost = Test-CorridorKeyWindowsHost
if ($isWindowsHost) {
    Initialize-CorridorKeyMsvcEnvironment
}

$configureArgs = @("--preset", $Preset)

if ($isWindowsHost) {
    $windowsOrtRoot = Resolve-CorridorKeyWindowsOrtRoot -RepoRoot $repoRoot -PreferredTrack "any" -AllowEnvironmentOverride
    $env:CORRIDORKEY_WINDOWS_ORT_ROOT = $windowsOrtRoot
    $configureArgs += "-DCORRIDORKEY_WINDOWS_ORT_ROOT=$windowsOrtRoot"
    Write-Host "[build] Using curated Windows ONNX Runtime: $windowsOrtRoot" -ForegroundColor Yellow
}

$configureArgs += "-DCORRIDORKEY_DISPLAY_VERSION_LABEL=$DisplayVersionLabel"
if (-not [string]::IsNullOrWhiteSpace($DisplayVersionLabel)) {
    Write-Host "[build] Using display version label: $DisplayVersionLabel" -ForegroundColor Yellow
}

Write-Host "[build] Configuring preset: $Preset" -ForegroundColor Cyan
cmake @configureArgs
if ($LASTEXITCODE -ne 0) {
    throw "CMake configure failed for preset '$Preset'."
}

Write-Host "[build] Building preset: $Preset" -ForegroundColor Cyan
cmake --build --preset $Preset
if ($LASTEXITCODE -ne 0) {
    throw "CMake build failed for preset '$Preset'."
}

Write-Host "[build] Build completed successfully." -ForegroundColor Green
