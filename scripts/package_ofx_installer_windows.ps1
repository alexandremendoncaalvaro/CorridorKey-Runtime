param(
    [string]$Version = "",
    [string]$BuildDir = "",
    [string]$OrtRoot = "",
    [string]$ModelsDir = "",
    [string]$ArtifactManifestPath = "",
    [string]$ReleaseSuffix = "",
    [ValidateSet("windows-rtx", "windows-universal")]
    [string]$ModelProfile = "",
    [string]$DisplayVersionLabel = "",
    [switch]$Skip2048
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot "windows_runtime_helpers.ps1")

function Resolve-NsisCompiler {
    $candidates = @(
        "C:\Program Files (x86)\NSIS\makensis.exe",
        "C:\Program Files (x86)\NSIS\Bin\makensis.exe"
    )

    $command = Get-Command "makensis.exe" -ErrorAction SilentlyContinue
    if ($null -ne $command) {
        return $command.Source
    }

    foreach ($candidate in $candidates) {
        if (Test-Path $candidate) {
            return $candidate
        }
    }

    throw "makensis.exe was not found. Install NSIS before building the Windows OFX installer."
}

function Write-ReleaseReadme {
    param(
        [string]$Path,
        [string]$Version,
        [string]$ReleaseBasename,
        [string]$ReleaseLabel,
        [string]$ModelProfile
    )

    $modelCoverageText = switch ($ModelProfile) {
        "windows-rtx" { "This Windows RTX package includes the official FP16 ladder through 2048px plus the portable INT8 CPU artifacts." }
        "windows-universal" { "This Windows DirectML package includes the Windows universal GPU and CPU model set." }
        default { "This package includes the packaged model set recorded in CorridorKey.ofx.bundle\\model_inventory.json." }
    }

@"
CorridorKey OFX v$Version - $ReleaseLabel
=========================================

$modelCoverageText

Files in this release:
- CorridorKey.ofx.bundle: the packaged OFX bundle payload
- install_plugin.bat: manual installer helper for the bundle
- bundle_validation.json: packaging-time validation and doctor status
- CorridorKey.ofx.bundle\model_inventory.json: packaged model inventory

Recommended install path:
1. Run $ReleaseBasename`_Install.exe as Administrator.
2. Open your OFX host of choice (DaVinci Resolve or Foundry Nuke). The
   plugin is registered for both at the standard OpenFX bundle location.

Installer behavior:
- The installer replaces any existing CorridorKey Windows OFX installation before copying the new bundle.
- It detects DaVinci Resolve and/or Foundry Nuke and only acts on hosts that are present (closes the host if running and clears its OFX metadata cache).
- The CorridorKey CLI (corridorkey.exe) directory is registered on the system PATH, so `corridorkey` is available from any terminal after installation. Open a new shell to pick up the change.
- Uninstalling restores the previous system PATH.
- The installer never auto-launches a host; you choose when to open Resolve or Nuke.

Manual fallback path:
1. Run install_plugin.bat as Administrator from this folder.
2. Open DaVinci Resolve or Foundry Nuke when you are ready to use the plugin.
"@ | Set-Content -Path $Path -Encoding ASCII
}

function Write-PathUpdateScript {
    param(
        [string]$BundlePath
    )

    $targetDir = Join-Path $BundlePath "Contents\Win64"
    if (-not (Test-Path $targetDir)) {
        throw "Bundle Win64 directory not found at $targetDir. Packaging step must run before writing update_path.ps1."
    }

    $targetPath = Join-Path $targetDir "update_path.ps1"
    $content = @'
param(
    [ValidateSet("Install", "Uninstall")]
    [string]$Mode = "Install"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

# The directory holding this script is the directory we want on PATH.
# For CorridorKey installs that is <bundle>\Contents\Win64 which contains
# corridorkey.exe alongside its ONNX Runtime / TensorRT DLLs.
$binDir = $PSScriptRoot

$current = [Environment]::GetEnvironmentVariable("Path", "Machine")
if ($null -eq $current) { $current = "" }

# Drop empty entries and any previous registration of our directory (case-insensitive).
$entries = @(
    $current -split ';' | Where-Object {
        $_ -and ($_.Trim().TrimEnd('\') -ine $binDir.TrimEnd('\'))
    }
)

if ($Mode -eq "Install") {
    $entries += $binDir
}

$newPath = ($entries -join ';').Trim(';')

# SetEnvironmentVariable on "Machine" scope broadcasts WM_SETTINGCHANGE so new
# shells pick up the change immediately. Existing shells must be reopened.
[Environment]::SetEnvironmentVariable("Path", $newPath, "Machine")

Write-Host "CorridorKey CLI PATH $Mode complete: $binDir"
'@

    Set-Content -Path $targetPath -Value $content -Encoding ASCII
    Write-Host "Wrote CLI PATH helper: $targetPath" -ForegroundColor Gray
}

$Version = Initialize-CorridorKeyVersion -RepoRoot $repoRoot -Version $Version
if ([string]::IsNullOrWhiteSpace($BuildDir)) {
    $BuildDir = Join-Path $repoRoot "build\release"
}
$preferredTrack = Get-CorridorKeyWindowsTrackFromReleaseSuffix -ReleaseSuffix $ReleaseSuffix -DefaultTrack "rtx"
$OrtRoot = Resolve-CorridorKeyWindowsOrtRoot -RepoRoot $repoRoot -ExplicitRoot $OrtRoot -PreferredTrack $preferredTrack
if ([string]::IsNullOrWhiteSpace($ModelsDir)) {
    $ModelsDir = Join-Path $repoRoot "models"
}
if ([string]::IsNullOrWhiteSpace($ModelProfile)) {
    $ModelProfile = Get-CorridorKeyOfxModelProfileFromReleaseSuffix -ReleaseSuffix $ReleaseSuffix
}
$releaseLabel = Get-CorridorKeyWindowsReleaseLabelFromSuffix -ReleaseSuffix $ReleaseSuffix

$normalizedSuffix = ""
if (-not [string]::IsNullOrWhiteSpace($ReleaseSuffix)) {
    $normalizedSuffix = "_" + $ReleaseSuffix.Trim("_")
}

# When a `-DisplayVersionLabel` (e.g. `0.8.2-win.1`) is supplied the
# packaged artifact filenames use it instead of the base version. This
# keeps the installed build identity visible directly in the filename
# (`CorridorKey_OFX_v0.8.2-win.1_Windows_RTX_Install.exe`) so operators
# downloading a pre-release candidate never have to guess which cycle
# they are installing. Public releases leave `-DisplayVersionLabel`
# empty and filenames fall back to the base CMake version.
$artifactVersionTag = if ([string]::IsNullOrWhiteSpace($DisplayVersionLabel)) { $Version } else { $DisplayVersionLabel }
$releaseBasename = "CorridorKey_OFX_v${artifactVersionTag}_Windows${normalizedSuffix}"
$releaseDir = Join-Path $repoRoot ("dist\" + $releaseBasename)
$bundlePath = Join-Path $releaseDir "CorridorKey.ofx.bundle"
$installerPath = Join-Path $repoRoot ("dist\" + $releaseBasename + "_Install.exe")
$installScriptPath = Join-Path $releaseDir "install_plugin.bat"
$readmePath = Join-Path $releaseDir "README.txt"
$nsisCompiler = Resolve-NsisCompiler
$tempNsiPath = Join-Path $env:TEMP ("corridorkey_ofx_installer_" + [System.Guid]::NewGuid().ToString("N") + ".nsi")

Write-Host "[1/5] Preparing release directory..." -ForegroundColor Cyan
if (Test-Path $releaseDir) {
    Remove-Item $releaseDir -Recurse -Force
}
if (Test-Path $installerPath) {
    Remove-Item $installerPath -Force
}
New-Item -ItemType Directory -Path $releaseDir -Force | Out-Null

$bundleArgs = @{
    BuildDir = $BuildDir
    OrtRoot = $OrtRoot
    ModelsDir = $ModelsDir
    OutputDir = $bundlePath
    ModelProfile = $ModelProfile
}
if (-not [string]::IsNullOrWhiteSpace($ArtifactManifestPath)) {
    $bundleArgs["ArtifactManifestPath"] = $ArtifactManifestPath
}
if ($Skip2048.IsPresent) {
    $bundleArgs["Skip2048"] = $true
}

Write-Host "[2/5] Packaging the OFX bundle..." -ForegroundColor Cyan
& (Join-Path $repoRoot "scripts\package_ofx.ps1") @bundleArgs
if ($LASTEXITCODE -ne 0) {
    throw "Windows OFX bundle packaging failed."
}

Write-PathUpdateScript -BundlePath $bundlePath

Write-Host "[3/5] Validating the OFX bundle..." -ForegroundColor Cyan
& (Join-Path $repoRoot "scripts\validate_ofx_win.ps1") -BundlePath $bundlePath
if ($LASTEXITCODE -ne 0) {
    throw "Windows OFX bundle validation failed."
}

$bundleValidationPath = Join-Path $releaseDir "bundle_validation.json"
Assert-CorridorKeyBundleValidationHealthy `
    -ValidationReportPath $bundleValidationPath `
    -Label "$releaseLabel bundle" | Out-Null

Write-Host "[4/5] Assembling release folder..." -ForegroundColor Cyan
Copy-Item (Join-Path $repoRoot "scripts\install_plugin.bat") $installScriptPath -Force
Write-ReleaseReadme -Path $readmePath `
    -Version $Version `
    -ReleaseBasename $releaseBasename `
    -ReleaseLabel $releaseLabel `
    -ModelProfile $ModelProfile

$escapedBundlePath = $bundlePath.Replace('\', '\\')
$escapedInstallerPath = $installerPath.Replace('\', '\\')
$nsiScript = @"
Unicode True
RequestExecutionLevel admin
SetCompressor /SOLID zlib
!include "LogicLib.nsh"

Name "CorridorKey OFX (Nuke & Resolve) $Version ($releaseLabel)"
OutFile "$escapedInstallerPath"
InstallDir "`$PROGRAMFILES64\CorridorKey OFX"
BrandingText "$releaseLabel"
ShowInstDetails show
ShowUninstDetails show

!define PRODUCT_NAME "CorridorKey OFX (Nuke & Resolve) ($releaseLabel)"
!define PRODUCT_VERSION "$Version"
!define PLUGIN_SOURCE "$escapedBundlePath"
!define PLUGIN_DEST "`$COMMONFILES64\OFX\Plugins\CorridorKey.ofx.bundle"
!define RESOLVE_EXE "`$PROGRAMFILES64\Blackmagic Design\DaVinci Resolve\Resolve.exe"
!define RESOLVE_CACHE_FILE "`$APPDATA\Blackmagic Design\DaVinci Resolve\Support\OFXPluginCacheV2.xml"
!define NUKE_OFX_CACHE_DIR "`$LOCALAPPDATA\Temp\nuke\ofxplugincache"
; Registry uninstall key renamed from CorridorKeyResolveOFX in v0.8.2 alongside
; the host-agnostic naming standardization. Users upgrading from v0.7.x or
; earlier will see the legacy "CorridorKey Resolve OFX" entry persist in
; Apps & Features alongside the new entry until they uninstall it manually.
!define UNINSTALL_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\CorridorKeyOFX"

; Host-presence flags populated at install time. Both Install and Uninstall
; sections compute these per machine so the installer never assumes a host
; is present (today's reality is multi-host: Resolve, Nuke, both, neither).
Var ResolveDetected
Var NukeDetected
Var NukeRoot
Var NukeCacheFile
Var FindHandle

!macro DetectInstalledHosts
  StrCpy `$ResolveDetected "0"
  StrCpy `$NukeDetected "0"

  IfFileExists "`${RESOLVE_EXE}" 0 +3
    DetailPrint "Detected DaVinci Resolve."
    StrCpy `$ResolveDetected "1"

  ; Nuke versions live under `$PROGRAMFILES64\Nuke<ver>\Nuke<ver>.exe` and the
  ; user can have several side by side. We treat any Nuke* directory whose
  ; folder also contains a Nuke*.exe as a positive detection.
  FindFirst `$FindHandle `$NukeRoot "`$PROGRAMFILES64\Nuke*"
  `${DoWhile} `$NukeRoot != ""
    IfFileExists "`$PROGRAMFILES64\`$NukeRoot\Nuke*.exe" 0 +3
      DetailPrint "Detected Foundry Nuke at `$PROGRAMFILES64\`$NukeRoot."
      StrCpy `$NukeDetected "1"
    FindNext `$FindHandle `$NukeRoot
  `${Loop}
  FindClose `$FindHandle
!macroend

Section "Install"
  SetRegView 64

  !insertmacro DetectInstalledHosts

  ; Order matters: close hosts first so they release bundle handles, then the
  ; out-of-process runtime server + CLI which otherwise keep the previous
  ; bundle DLLs mapped and block RMDir below. taskkill returns 128 when the
  ; process is not running -- not an error for our flow.
  `${If} `$ResolveDetected == "1"
    DetailPrint "Closing DaVinci Resolve..."
    nsExec::ExecToStack 'taskkill /F /IM Resolve.exe'
    Pop `$0
  `${EndIf}

  `${If} `$NukeDetected == "1"
    DetailPrint "Closing Foundry Nuke..."
    nsExec::ExecToStack 'taskkill /F /IM Nuke*.exe'
    Pop `$0
  `${EndIf}

  nsExec::ExecToStack 'taskkill /F /IM corridorkey_ofx_runtime_server.exe'
  Pop `$0
  nsExec::ExecToStack 'taskkill /F /IM corridorkey.exe'
  Pop `$0
  Sleep 2000

  DetailPrint "Removing previous CorridorKey OFX bundle..."
  RMDir /r "`${PLUGIN_DEST}"

  DetailPrint "Installing CorridorKey OFX bundle..."
  SetOutPath "`${PLUGIN_DEST}"
  File /r "`${PLUGIN_SOURCE}\*"

  DetailPrint "Registering CorridorKey CLI on system PATH..."
  nsExec::ExecToStack 'powershell.exe -NoProfile -ExecutionPolicy Bypass -File "`${PLUGIN_DEST}\Contents\Win64\update_path.ps1" -Mode Install'
  Pop `$0
  Pop `$1
  StrCmp `$0 "0" path_install_ok 0
    DetailPrint "Warning: PATH registration exited with code `$0. Details: `$1"
  path_install_ok:

  DetailPrint "Writing uninstaller..."
  SetOutPath "`$INSTDIR"
  WriteUninstaller "`$INSTDIR\Uninstall CorridorKey OFX.exe"

  WriteRegStr HKLM "`${UNINSTALL_KEY}" "DisplayName" "`${PRODUCT_NAME}"
  WriteRegStr HKLM "`${UNINSTALL_KEY}" "DisplayVersion" "`${PRODUCT_VERSION}"
  WriteRegStr HKLM "`${UNINSTALL_KEY}" "Publisher" "CorridorKey"
  WriteRegStr HKLM "`${UNINSTALL_KEY}" "InstallLocation" "`$INSTDIR"
  WriteRegStr HKLM "`${UNINSTALL_KEY}" "UninstallString" "`$INSTDIR\Uninstall CorridorKey OFX.exe"
  WriteRegDWORD HKLM "`${UNINSTALL_KEY}" "NoModify" 1
  WriteRegDWORD HKLM "`${UNINSTALL_KEY}" "NoRepair" 1

  ; Intentionally do NOT clear %LOCALAPPDATA%\CorridorKey\Logs here. Runtime
  ; server logs are already versioned (`ofx_runtime_server_v<X.Y.Z>.log`) so
  ; they do not collide across installs; wiping them on every install
  ; destroys the cross-version comparison data the optimization measurement
  ; track depends on.

  `${If} `$ResolveDetected == "1"
    DetailPrint "Clearing DaVinci Resolve OFX cache..."
    Delete "`${RESOLVE_CACHE_FILE}"
  `${EndIf}

  `${If} `$NukeDetected == "1"
    DetailPrint "Clearing Foundry Nuke OFX cache..."
    FindFirst `$FindHandle `$NukeCacheFile "`${NUKE_OFX_CACHE_DIR}\ofxplugincache_Nuke*-64.xml"
    `${DoWhile} `$NukeCacheFile != ""
      Delete "`${NUKE_OFX_CACHE_DIR}\`$NukeCacheFile"
      FindNext `$FindHandle `$NukeCacheFile
    `${Loop}
    FindClose `$FindHandle
  `${EndIf}

  ; Industry standard for OFX installers (Sapphire, Mocha Pro, Re:Vision)
  ; is to never auto-launch a host; the user picks when and which to open.
  ; The previous behavior of auto-launching DaVinci Resolve was surprising
  ; on machines where the user had also installed the plugin for Nuke.
  `${If} `$ResolveDetected == "1"
  `${OrIf} `$NukeDetected == "1"
    DetailPrint "Plugin registered. Open DaVinci Resolve and/or Foundry Nuke when ready."
  `${Else}
    DetailPrint "Plugin installed at `${PLUGIN_DEST}. Install DaVinci Resolve or Foundry Nuke to use it."
  `${EndIf}
SectionEnd

Section "Uninstall"
  SetRegView 64

  !insertmacro DetectInstalledHosts

  DetailPrint "Stopping any running CorridorKey processes..."
  nsExec::ExecToStack 'taskkill /F /IM corridorkey_ofx_runtime_server.exe'
  Pop `$0
  nsExec::ExecToStack 'taskkill /F /IM corridorkey.exe'
  Pop `$0
  Sleep 1000

  DetailPrint "Unregistering CorridorKey CLI from system PATH..."
  nsExec::ExecToStack 'powershell.exe -NoProfile -ExecutionPolicy Bypass -File "`${PLUGIN_DEST}\Contents\Win64\update_path.ps1" -Mode Uninstall'
  Pop `$0
  Pop `$1
  StrCmp `$0 "0" path_uninstall_ok 0
    DetailPrint "Warning: PATH unregistration exited with code `$0. Details: `$1"
  path_uninstall_ok:

  DetailPrint "Removing CorridorKey OFX bundle..."
  RMDir /r "`${PLUGIN_DEST}"

  `${If} `$ResolveDetected == "1"
    DetailPrint "Clearing DaVinci Resolve OFX cache..."
    Delete "`${RESOLVE_CACHE_FILE}"
  `${EndIf}

  `${If} `$NukeDetected == "1"
    DetailPrint "Clearing Foundry Nuke OFX cache..."
    FindFirst `$FindHandle `$NukeCacheFile "`${NUKE_OFX_CACHE_DIR}\ofxplugincache_Nuke*-64.xml"
    `${DoWhile} `$NukeCacheFile != ""
      Delete "`${NUKE_OFX_CACHE_DIR}\`$NukeCacheFile"
      FindNext `$FindHandle `$NukeCacheFile
    `${Loop}
    FindClose `$FindHandle
  `${EndIf}

  Delete "`$INSTDIR\Uninstall CorridorKey OFX.exe"
  RMDir "`$INSTDIR"
  DeleteRegKey HKLM "`${UNINSTALL_KEY}"
SectionEnd
"@

Set-Content -Path $tempNsiPath -Value $nsiScript -Encoding ASCII

try {
    Write-Host "[5/5] Building the NSIS installer..." -ForegroundColor Cyan
    & $nsisCompiler $tempNsiPath
    if ($LASTEXITCODE -ne 0) {
        throw "NSIS installer build failed."
    }
} finally {
    if (Test-Path $tempNsiPath) {
        Remove-Item $tempNsiPath -Force
    }
}

Write-Host "Release directory ready at: $releaseDir" -ForegroundColor Green
Write-Host "Installer ready at: $installerPath" -ForegroundColor Green
