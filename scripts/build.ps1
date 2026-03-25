param(
    [ValidateSet("debug", "release", "release-lto")]
    [string]$Preset = "release"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot "windows_runtime_helpers.ps1")

if ([string]::IsNullOrWhiteSpace($env:VCPKG_ROOT)) {
    throw "VCPKG_ROOT is required by CMakePresets.json. Set VCPKG_ROOT before running scripts/build.ps1."
}

if (-not (Test-Path $env:VCPKG_ROOT)) {
    throw "VCPKG_ROOT does not exist: $env:VCPKG_ROOT"
}

$isWindowsHost = Test-CorridorKeyWindowsHost
if ($isWindowsHost) {
    # If cl.exe is missing, activate the VS Developer Shell before configuring.
    $clFound = Get-Command cl.exe -ErrorAction SilentlyContinue
    if (-not $clFound) {
        $vsWhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
        if (-not (Test-Path $vsWhere)) {
            throw "Visual Studio is not installed. Cannot locate vswhere.exe."
        }

        $vsInstallDir = & $vsWhere -latest -property installationPath 2>$null
        if ([string]::IsNullOrWhiteSpace($vsInstallDir)) {
            throw "No Visual Studio installation found by vswhere."
        }

        $launchScript = Join-Path $vsInstallDir "Common7\Tools\Launch-VsDevShell.ps1"
        if (-not (Test-Path $launchScript)) {
            throw "Launch-VsDevShell.ps1 not found at: $launchScript"
        }

        Write-Host "[build] Injecting MSVC environment from: $vsInstallDir" -ForegroundColor Yellow
        & $launchScript -Arch amd64 -SkipAutomaticLocation | Out-Null
    }
}

$configureArgs = @("--preset", $Preset)

if ($isWindowsHost) {
    $windowsOrtRoot = Resolve-CorridorKeyWindowsOrtRoot -RepoRoot $repoRoot -PreferredTrack "any" -AllowEnvironmentOverride
    $env:CORRIDORKEY_WINDOWS_ORT_ROOT = $windowsOrtRoot
    $configureArgs += "-DCORRIDORKEY_WINDOWS_ORT_ROOT=$windowsOrtRoot"
    Write-Host "[build] Using curated Windows ONNX Runtime: $windowsOrtRoot" -ForegroundColor Yellow
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
