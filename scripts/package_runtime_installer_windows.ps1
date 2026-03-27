param(
    [string]$Version = "",
    [string]$BuildDir = "",
    [string]$OrtRoot = "",
    [string]$ReleaseSuffix = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$guiRoot = Join-Path $repoRoot "src\gui"
$tauriRoot = Join-Path $guiRoot "src-tauri"
$runtimeResourceDir = Join-Path $tauriRoot "resources\runtime"
. (Join-Path $PSScriptRoot "windows_runtime_helpers.ps1")

function Resolve-VsDevCmd {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) {
        throw "vswhere.exe was not found at $vswhere"
    }

    $installationPath = & $vswhere -latest -products * `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
        -property installationPath
    if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($installationPath)) {
        throw "Visual Studio with C++ tools was not found."
    }

    $vsDevCmd = Join-Path $installationPath "Common7\Tools\VsDevCmd.bat"
    if (-not (Test-Path $vsDevCmd)) {
        throw "VsDevCmd.bat was not found at $vsDevCmd"
    }

    return $vsDevCmd
}

function Resolve-CargoBinDir {
    $cargoCommand = Get-Command cargo -ErrorAction SilentlyContinue
    if ($null -ne $cargoCommand) {
        return Split-Path -Parent $cargoCommand.Source
    }

    $miseCommand = Get-Command mise -ErrorAction SilentlyContinue
    if ($null -eq $miseCommand) {
        throw "Neither cargo nor mise is available on PATH."
    }

    $cargoPath = & $miseCommand.Source which cargo
    if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($cargoPath)) {
        throw "cargo is not available via mise."
    }

    return Split-Path -Parent $cargoPath.Trim()
}

function Resolve-NsisBinDirs {
    $candidates = @(
        "C:\Program Files (x86)\NSIS",
        "C:\Program Files (x86)\NSIS\Bin"
    )

    $resolved = @(
        $candidates | Where-Object { Test-Path (Join-Path $_ "makensis.exe") }
    )
    if ($resolved.Count -eq 0) {
        throw "makensis.exe was not found. Install NSIS before building the installer."
    }

    return $resolved
}

function Clear-StagedRuntimePayload {
    param([string]$RuntimeResourceDir)

    if (-not (Test-Path $RuntimeResourceDir)) {
        return
    }

    Get-ChildItem -Path $RuntimeResourceDir -Force | Where-Object { $_.Name -ne ".gitignore" } |
        Remove-Item -Recurse -Force
}

$Version = Initialize-CorridorKeyVersion -RepoRoot $repoRoot -Version $Version -SyncGuiMetadata
if ([string]::IsNullOrWhiteSpace($BuildDir)) {
    $BuildDir = Join-Path $repoRoot "build\release"
}
$preferredTrack = Get-CorridorKeyWindowsTrackFromReleaseSuffix -ReleaseSuffix $ReleaseSuffix -DefaultTrack "rtx"
$OrtRoot = Resolve-CorridorKeyWindowsOrtRoot -RepoRoot $repoRoot -ExplicitRoot $OrtRoot -PreferredTrack $preferredTrack

$vsDevCmd = Resolve-VsDevCmd
$cargoBinDir = Resolve-CargoBinDir
$nsisBinDirs = Resolve-NsisBinDirs
$installerBundleDir = Join-Path $tauriRoot "target\release\bundle\nsis"

$normalizedSuffix = ""
if (-not [string]::IsNullOrWhiteSpace($ReleaseSuffix)) {
    $normalizedSuffix = "_" + $ReleaseSuffix.Trim("_")
}
$releaseInstallerPath = Join-Path $repoRoot ("dist\CorridorKey_Runtime_v${Version}_Windows${normalizedSuffix}_Installer.exe")
$tempCmdPath = Join-Path $env:TEMP ("corridorkey_tauri_nsis_" + [System.Guid]::NewGuid().ToString("N") + ".cmd")

$stageArgs = @{
    Version = $Version
    BuildDir = $BuildDir
    OrtRoot = $OrtRoot
}
if (-not [string]::IsNullOrWhiteSpace($ReleaseSuffix)) {
    $stageArgs["ReleaseSuffix"] = $ReleaseSuffix
}

try {
    Write-Host "[1/4] Staging runtime payload for the Windows installer..." -ForegroundColor Cyan
    & (Join-Path $repoRoot "scripts\stage_tauri_runtime_windows.ps1") @stageArgs
    if ($LASTEXITCODE -ne 0) {
        throw "Runtime staging failed."
    }

    Write-Host "[2/4] Building the Tauri NSIS installer..." -ForegroundColor Cyan
    $nsisPathPrefix = (($nsisBinDirs + $cargoBinDir) -join ";")
    $cmdScript = @"
@echo off
call "$vsDevCmd" -arch=amd64 -host_arch=amd64 >nul
if errorlevel 1 exit /b 1
set "PATH=$nsisPathPrefix;%PATH%"
cd /d "$guiRoot"
pnpm tauri build --bundles nsis
"@
    Set-Content -Path $tempCmdPath -Value $cmdScript -Encoding ASCII
    & cmd /c $tempCmdPath
    if ($LASTEXITCODE -ne 0) {
        throw "Tauri NSIS build failed."
    }

    Write-Host "[3/4] Collecting installer artifact..." -ForegroundColor Cyan
    $latestInstaller = Get-ChildItem -Path $installerBundleDir -Filter "*-setup.exe" -File |
        Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if ($null -eq $latestInstaller) {
        throw "No NSIS installer was produced under $installerBundleDir"
    }

    Copy-Item $latestInstaller.FullName $releaseInstallerPath -Force

    Write-Host "[4/4] Installer ready at: $releaseInstallerPath" -ForegroundColor Green
} finally {
    if (Test-Path $tempCmdPath) {
        Remove-Item $tempCmdPath -Force
    }
    Clear-StagedRuntimePayload -RuntimeResourceDir $runtimeResourceDir
}
