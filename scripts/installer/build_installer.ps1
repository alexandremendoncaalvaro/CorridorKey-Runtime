<#
.SYNOPSIS
    Build a CorridorKey Windows installer (Inno Setup 6) in either
    online or offline flavor.

.DESCRIPTION
    Reads `scripts/installer/distribution_manifest.json` for pack
    URLs, sha256 hashes and target subdirs; expands the
    `corridorkey.iss.template` for the chosen flavor; invokes ISCC.exe
    to compile the resulting .iss into the final installer .exe.

    Online flavor produces a small stub installer (~50 MB) that
    downloads selected packs from Hugging Face during install. Offline
    flavor produces a self-contained installer (~7 GB) with every pack
    pre-bundled.

    The PowerShell side here is intentionally thin: it injects values
    into the template (display label, output path, per-pack file
    blocks) and invokes the compiler. All installer behaviour lives
    in `corridorkey.iss.template` and the manifest. There are no
    flow decisions encoded in PowerShell that would not also be
    visible in the produced .iss.

.PARAMETER Flavor
    "online" or "offline". Drives template selection and dictates
    whether files are downloaded at install time or pre-bundled.

.PARAMETER Version
    Base CMakeLists version (X.Y.Z). The Inno Setup AppVersion field
    is set to this; the displayed wizard label uses the longer
    DisplayVersionLabel (typically derived from `git describe`).

.PARAMETER DisplayVersionLabel
    Long-form build identifier shown to the operator (in the wizard
    title bar, in the OFX panel after install, in the "About" dialog).
    Falls back to -Version when empty.

.PARAMETER PluginPayloadDir
    Path to the pre-staged OFX bundle layout, typically the
    output of `package_ofx_installer_windows.ps1` Phase 1 staging
    (Contents\Win64\* with all DLLs already laid out).

.PARAMETER ModelPayloadDir
    For OFFLINE flavor only: path to a directory containing the per-
    pack files. Layout is rooted at the pack's dest_subdir from the
    manifest (so models/foo.onnx, torchtrt-runtime/bin/bar.dll, etc).
    Ignored for ONLINE flavor.

.PARAMETER OutputDir
    Where the final installer .exe is written. Defaults to repo
    `dist/` per existing convention.

.PARAMETER ManifestPath
    Path to the distribution manifest JSON. Defaults to
    `scripts/installer/distribution_manifest.json`.

.PARAMETER InstallerIcon
    Path to the .ico file used for the installer setup icon.
    Defaults to the OFX bundle icon when present.

.PARAMETER ISCCPath
    Path to ISCC.exe. Auto-detected from common install paths when
    omitted.

.EXAMPLE
    pwsh scripts/installer/build_installer.ps1 -Flavor online \
      -Version 0.8.3 -DisplayVersionLabel '0.8.3-win.4' \
      -PluginPayloadDir build/release/CorridorKey.ofx.bundle

.EXAMPLE
    pwsh scripts/installer/build_installer.ps1 -Flavor offline \
      -Version 0.8.3 -DisplayVersionLabel '0.8.3-win.4' \
      -PluginPayloadDir build/release/CorridorKey.ofx.bundle \
      -ModelPayloadDir dist/_offline_payload/
#>

[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [ValidateSet("online", "offline")]
    [string]$Flavor,

    [Parameter(Mandatory)]
    [string]$Version,

    [string]$DisplayVersionLabel = "",

    [Parameter(Mandatory)]
    [string]$PluginPayloadDir,

    [string]$ModelPayloadDir = "",

    [string]$OutputDir = "",

    [string]$ManifestPath = "",

    [string]$InstallerIcon = "",

    [string]$ISCCPath = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
if ([string]::IsNullOrWhiteSpace($OutputDir)) {
    $OutputDir = Join-Path $repoRoot "dist"
}
if ([string]::IsNullOrWhiteSpace($ManifestPath)) {
    $ManifestPath = Join-Path $PSScriptRoot "distribution_manifest.json"
}
if ([string]::IsNullOrWhiteSpace($DisplayVersionLabel)) {
    $DisplayVersionLabel = $Version
}
if ([string]::IsNullOrWhiteSpace($InstallerIcon)) {
    $candidate = Join-Path $repoRoot "src\plugins\ofx\resources\corridorkey.ico"
    if (Test-Path $candidate) {
        $InstallerIcon = $candidate
    }
}

$templatePath = Join-Path $PSScriptRoot "corridorkey.iss.template"
if (-not (Test-Path $templatePath)) {
    throw "Template not found: $templatePath"
}
if (-not (Test-Path $ManifestPath)) {
    throw "Distribution manifest not found: $ManifestPath. Run scripts/installer/build_distribution_manifest.py first."
}
if (-not (Test-Path $PluginPayloadDir)) {
    throw "Plugin payload dir not found: $PluginPayloadDir. Stage the OFX bundle layout (Contents/Win64/*) before invoking this script."
}
if ($Flavor -eq "offline") {
    if ([string]::IsNullOrWhiteSpace($ModelPayloadDir)) {
        throw "Offline flavor requires -ModelPayloadDir pointing at a pre-populated pack tree."
    }
    if (-not (Test-Path $ModelPayloadDir)) {
        throw "Model payload dir not found: $ModelPayloadDir"
    }
}

# ---------------------------------------------------------------------------
# Tooling resolution.
# ---------------------------------------------------------------------------

function Resolve-IsccPath {
    param([string]$Override)
    if (-not [string]::IsNullOrWhiteSpace($Override)) {
        if (-not (Test-Path $Override)) {
            throw "ISCC override path does not exist: $Override"
        }
        return $Override
    }
    # Search order: user-scope first (winget --scope user puts it
    # under %LOCALAPPDATA%\Programs), then machine-wide install paths.
    # Inno Setup 7 paths are listed for forward-compat once the 7.x
    # series ships; the 6.x layout is what we author against today.
    $candidates = @(
        (Join-Path $env:LOCALAPPDATA "Programs\Inno Setup 6\ISCC.exe"),
        (Join-Path $env:LOCALAPPDATA "Programs\Inno Setup 7\ISCC.exe"),
        "C:\Program Files (x86)\Inno Setup 6\ISCC.exe",
        "C:\Program Files\Inno Setup 6\ISCC.exe",
        "C:\Program Files (x86)\Inno Setup 7\ISCC.exe",
        "C:\Program Files\Inno Setup 7\ISCC.exe"
    )
    foreach ($candidate in $candidates) {
        if (Test-Path $candidate) {
            return $candidate
        }
    }
    $cmd = Get-Command "ISCC.exe" -ErrorAction SilentlyContinue
    if ($null -ne $cmd) {
        return $cmd.Source
    }
    throw "ISCC.exe was not found. Install Inno Setup 6 (https://jrsoftware.org/isdl.php) or pass -ISCCPath."
}

# ---------------------------------------------------------------------------
# Manifest -> template block generation.
# ---------------------------------------------------------------------------

function ConvertTo-IssEscapedString {
    # Inno Setup pre-processor strings escape `"` as `""`. URLs and
    # paths in the manifest are quoted via single quotes in the
    # generated Pascal Script (idiomatic for Inno Setup), so we only
    # need to escape single quotes inside the string itself.
    param([string]$Value)
    return $Value -replace "'", "''"
}

function Build-OnlineExternalFilesBlock {
    param([object]$Manifest)
    $sb = [System.Text.StringBuilder]::new()
    [void]$sb.AppendLine('[Files]')
    foreach ($pack in $Manifest.packs.PSObject.Properties) {
        $packMeta = $pack.Value
        $component = $packMeta.component
        $destSubdir = $packMeta.dest_subdir -replace '/', '\'
        foreach ($file in $packMeta.files) {
            if ($file.status -ne 'ready') { continue }
            $line = "Source: `"{tmp}\$($file.filename)`"; DestDir: `"{app}\Contents\Resources\$destSubdir`"; Components: $component; Flags: external ignoreversion"
            if ($packMeta.PSObject.Properties.Match('extract').Count -gt 0 -and $packMeta.extract) {
                $line += " extractarchive recursesubdirs"
            }
            [void]$sb.AppendLine($line)
        }
    }
    return $sb.ToString().TrimEnd()
}

function Build-OfflineFilesBlock {
    # Offline flavor: every pack file is staged on disk under
    # $PayloadRoot/<dest_subdir>/<filename>. For "regular" packs we emit
    # one [Files] entry per file (granular + clear in the .iss). For
    # archive packs (is_archive + extract = true) the offline staging
    # script has ALREADY pre-extracted the archive into the same
    # subdir (Inno Setup's `extractarchive` flag is download-only;
    # bundling the .7z and trying to unpack at install raises
    # "Flag 'external' must be used"), so we emit a single
    # recursesubdirs entry that bakes every extracted file into the
    # installer.
    param([object]$Manifest, [string]$PayloadRoot)
    $sb = [System.Text.StringBuilder]::new()
    [void]$sb.AppendLine('[Files]')
    foreach ($pack in $Manifest.packs.PSObject.Properties) {
        $packMeta = $pack.Value
        $component = $packMeta.component
        $destSubdir = $packMeta.dest_subdir -replace '/', '\'
        $packDir = Join-Path $PayloadRoot $destSubdir
        $isExtractArchive = ($packMeta.PSObject.Properties.Match('is_archive').Count -gt 0 -and $packMeta.is_archive) `
                      -and ($packMeta.PSObject.Properties.Match('extract').Count -gt 0 -and $packMeta.extract)
        if ($isExtractArchive) {
            if (-not (Test-Path $packDir)) {
                throw "Offline payload missing pre-extracted archive dir: $packDir. Re-run scripts/installer/stage_offline_payload.ps1."
            }
            $hasContent = @(Get-ChildItem -Path $packDir -File -ErrorAction SilentlyContinue).Count -gt 0
            if (-not $hasContent) {
                throw "Offline payload pre-extraction dir is empty: $packDir. The archive download may have failed; re-run staging."
            }
            $sourceForIss = ((Join-Path $packDir '*') -replace '/', '\') -replace '\\\\', '\'
            [void]$sb.AppendLine("Source: `"$sourceForIss`"; DestDir: `"{app}\Contents\Resources\$destSubdir`"; Components: $component; Flags: ignoreversion recursesubdirs createallsubdirs")
            continue
        }
        foreach ($file in $packMeta.files) {
            if ($file.status -ne 'ready') { continue }
            $sourcePath = Join-Path $packDir $file.filename
            if (-not (Test-Path $sourcePath)) {
                throw "Offline payload missing file: $sourcePath. Pre-populate before invoking with -Flavor offline."
            }
            $sourceForIss = ($sourcePath -replace '/', '\') -replace '\\\\', '\'
            [void]$sb.AppendLine("Source: `"$sourceForIss`"; DestDir: `"{app}\Contents\Resources\$destSubdir`"; Components: $component; Flags: ignoreversion")
        }
    }
    return $sb.ToString().TrimEnd()
}

function Build-OnlineDownloadQueueProcedure {
    param([object]$Manifest)
    $sb = [System.Text.StringBuilder]::new()
    [void]$sb.AppendLine('procedure CorridorKeyEnqueueDownloads(const DownloadPage: TDownloadWizardPage);')
    [void]$sb.AppendLine('begin')
    foreach ($pack in $Manifest.packs.PSObject.Properties) {
        $packMeta = $pack.Value
        $component = $packMeta.component
        $readyFiles = @($packMeta.files | Where-Object { $_.status -eq 'ready' })
        if ($readyFiles.Count -eq 0) { continue }
        [void]$sb.AppendLine("  if WizardIsComponentSelected('$component') then begin")
        foreach ($file in $readyFiles) {
            $url = ConvertTo-IssEscapedString -Value $file.url
            $name = ConvertTo-IssEscapedString -Value $file.filename
            $hash = $file.sha256
            [void]$sb.AppendLine("    DownloadPage.Add('$url', '$name', '$hash');")
        }
        [void]$sb.AppendLine('  end;')
    }
    [void]$sb.AppendLine('end;')
    return $sb.ToString().TrimEnd()
}

# ---------------------------------------------------------------------------
# Compile.
# ---------------------------------------------------------------------------

$manifest = Get-Content -Raw -Path $ManifestPath | ConvertFrom-Json
$iscc = Resolve-IsccPath -Override $ISCCPath

$onlineFilesBlock = ""
$offlineFilesBlock = ""
$downloadQueueProcedure = ""
if ($Flavor -eq "online") {
    $onlineFilesBlock = Build-OnlineExternalFilesBlock -Manifest $manifest
    $downloadQueueProcedure = Build-OnlineDownloadQueueProcedure -Manifest $manifest
} else {
    $offlineFilesBlock = Build-OfflineFilesBlock -Manifest $manifest -PayloadRoot $ModelPayloadDir
}

$flavorLower = $Flavor.ToLowerInvariant()
$outputBaseFilename = "CorridorKey_v${DisplayVersionLabel}_Windows_${flavorLower}_Setup"

$template = Get-Content -Raw -Path $templatePath
$rendered = $template `
    -replace '@@DISPLAY_LABEL@@', $DisplayVersionLabel `
    -replace '@@BASE_VERSION@@', $Version `
    -replace '@@PLUGIN_PAYLOAD_DIR@@', ($PluginPayloadDir -replace '/', '\') `
    -replace '@@MODEL_PAYLOAD_DIR@@', (($ModelPayloadDir -replace '/', '\')) `
    -replace '@@OUTPUT_DIR@@', ($OutputDir -replace '/', '\') `
    -replace '@@OUTPUT_BASE_FILENAME@@', $outputBaseFilename `
    -replace '@@INSTALLER_ICON@@', ($InstallerIcon -replace '/', '\') `
    -replace '@@MANIFEST_PATH@@', ($ManifestPath -replace '/', '\') `
    -replace '@@FLAVOR@@', $flavorLower

# Inject generated Pascal/.iss blocks AFTER simple token replacement.
# These blocks may contain regex metacharacters (`$`, `{`), so use
# String.Replace (literal, no interpretation) instead of -replace.
$rendered = $rendered.Replace('@@OFFLINE_FILES_BLOCK@@', $offlineFilesBlock)
$rendered = $rendered.Replace('@@ONLINE_EXTERNAL_FILES_BLOCK@@', $onlineFilesBlock)
$rendered = $rendered.Replace('@@ONLINE_DOWNLOAD_QUEUE_PROCEDURE@@', $downloadQueueProcedure)

$tempIssDir = Join-Path $env:TEMP ("corridorkey_iss_" + [System.Guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $tempIssDir -Force | Out-Null
$tempIssPath = Join-Path $tempIssDir "corridorkey_setup.iss"
Set-Content -Path $tempIssPath -Value $rendered -Encoding UTF8

Write-Host "[installer] Flavor:        $Flavor" -ForegroundColor Cyan
Write-Host "[installer] Display label: $DisplayVersionLabel" -ForegroundColor Cyan
Write-Host "[installer] Plugin dir:    $PluginPayloadDir" -ForegroundColor Cyan
if ($Flavor -eq "offline") {
    Write-Host "[installer] Model dir:     $ModelPayloadDir" -ForegroundColor Cyan
}
Write-Host "[installer] Output:        $OutputDir\$outputBaseFilename.exe" -ForegroundColor Cyan
Write-Host "[installer] ISCC:          $iscc" -ForegroundColor Cyan
Write-Host "[installer] Generated iss: $tempIssPath" -ForegroundColor DarkGray

if (-not (Test-Path $OutputDir)) {
    New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null
}

& $iscc /Q $tempIssPath
if ($LASTEXITCODE -ne 0) {
    throw "ISCC failed with exit code $LASTEXITCODE. Inspect $tempIssPath for diagnostics."
}

$producedInstaller = Join-Path $OutputDir ($outputBaseFilename + ".exe")
if (-not (Test-Path $producedInstaller)) {
    throw "ISCC reported success but installer not found at $producedInstaller"
}

$sizeMb = [math]::Round((Get-Item $producedInstaller).Length / 1MB, 1)
$sha256 = (Get-FileHash -Path $producedInstaller -Algorithm SHA256).Hash.ToLowerInvariant()
Write-Host "[installer] Installer ready: $producedInstaller ($sizeMb MB)" -ForegroundColor Green
Write-Host "[installer] SHA256:         $sha256" -ForegroundColor Green

Remove-Item -Path $tempIssDir -Recurse -Force -ErrorAction SilentlyContinue

[ordered]@{
    flavor = $Flavor
    display_label = $DisplayVersionLabel
    base_version = $Version
    installer_path = $producedInstaller
    size_bytes = (Get-Item $producedInstaller).Length
    sha256 = $sha256
} | ConvertTo-Json -Depth 4
