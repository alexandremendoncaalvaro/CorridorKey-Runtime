param(
    [ValidateSet("debug", "release", "release-lto")]
    [string]$Preset = "release"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

# Detect whether the MSVC toolchain environment is already active.
# If 'cl.exe' is not on PATH, we need to inject the VS Developer Shell.
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

$repoRoot = Split-Path -Parent $PSScriptRoot

Write-Host "[build] Configuring preset: $Preset" -ForegroundColor Cyan
cmake --preset $Preset
if ($LASTEXITCODE -ne 0) {
    throw "CMake configure failed for preset '$Preset'."
}

Write-Host "[build] Building preset: $Preset" -ForegroundColor Cyan
cmake --build --preset $Preset
if ($LASTEXITCODE -ne 0) {
    throw "CMake build failed for preset '$Preset'."
}

Write-Host "[build] Build completed successfully." -ForegroundColor Green
