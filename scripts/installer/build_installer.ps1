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
    downloads every selected pack from Hugging Face during install,
    with Green + Blue selected by default. Offline flavor produces a
    self-contained installer (~7 GB) with every manifest pack
    pre-bundled and fixed for install.

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
    Base CMakeLists version (X.Y.Z). Numeric Windows version resource
    fields are set to this; the displayed wizard label uses the longer
    DisplayVersionLabel (typically derived from `git describe`).

.PARAMETER DisplayVersionLabel
    Long-form build identifier shown to the operator (in the wizard
    title bar, in the OFX panel after install, in the "About" dialog).
    Falls back to -Version when empty.

.PARAMETER PluginPayloadDir
    Path to the pre-staged host plugin payload layout, typically the
    output of the host-specific package script (Contents\Win64\* with
    all DLLs already laid out).

.PARAMETER InstallerSurface
    Host integration surface for installer metadata and target path.
    "ofx" installs to the OpenFX common bundle location. "adobe"
    installs to Adobe's shared Common Plug-ins MediaCore location.

.PARAMETER OutputBaseFilename
    Optional final installer basename without extension. When omitted,
    the builder uses the selected surface's online/offline naming convention.

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
    Path to the .ico file or source image used for the installer setup
    icon. Defaults to assets\ck-microchip.png when present.

.PARAMETER InstallerWizardImage
    Path to the source image used as the installer wizard's full-height
    side image. Defaults to assets\ck-install-banner.png.

.PARAMETER ISCCPath
    Path to ISCC.exe. Auto-detected from common install paths when
    omitted, including user-scope winget installs under
    `%LOCALAPPDATA%\Programs\Inno Setup 6\`.

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

    [ValidateSet("ofx", "adobe")]
    [string]$InstallerSurface = "ofx",

    [string]$OutputBaseFilename = "",

    [string]$ModelPayloadDir = "",

    [string]$OutputDir = "",

    [string]$ManifestPath = "",

    [string]$InstallerIcon = "",

    [string]$InstallerWizardImage = "",

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
    $candidate = Join-Path $repoRoot "assets\ck-microchip.png"
    if (Test-Path $candidate) {
        $InstallerIcon = $candidate
    }
}
if ([string]::IsNullOrWhiteSpace($InstallerIcon)) {
    $candidate = Join-Path $repoRoot "src\plugins\ofx\resources\corridorkey.ico"
    if (Test-Path $candidate) {
        $InstallerIcon = $candidate
    }
}
if ([string]::IsNullOrWhiteSpace($InstallerWizardImage)) {
    $candidate = Join-Path $repoRoot "assets\ck-install-banner.png"
    if (Test-Path $candidate) {
        $InstallerWizardImage = $candidate
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
if ([string]::IsNullOrWhiteSpace($InstallerWizardImage) -or -not (Test-Path $InstallerWizardImage)) {
    throw "Installer wizard image not found: $InstallerWizardImage"
}
if ([string]::IsNullOrWhiteSpace($InstallerIcon) -or -not (Test-Path $InstallerIcon)) {
    throw "Installer icon source not found: $InstallerIcon"
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

function New-IconDibFrameBytes {
    param(
        [object]$SourceImage,
        [int]$Size
    )

    $bitmap = [System.Drawing.Bitmap]::new(
        $Size,
        $Size,
        [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    try {
        $bitmap.SetResolution(96, 96)
        $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
        try {
            $graphics.Clear([System.Drawing.Color]::Transparent)
            $graphics.CompositingQuality = [System.Drawing.Drawing2D.CompositingQuality]::HighQuality
            $graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
            $graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality
            $graphics.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality

            $scale = [Math]::Min(
                [double]$Size / [double]$SourceImage.Width,
                [double]$Size / [double]$SourceImage.Height)
            $drawWidth = [int][Math]::Round([double]$SourceImage.Width * $scale)
            $drawHeight = [int][Math]::Round([double]$SourceImage.Height * $scale)
            $x = [int][Math]::Round(([double]$Size - [double]$drawWidth) / 2.0)
            $y = [int][Math]::Round(([double]$Size - [double]$drawHeight) / 2.0)

            $graphics.DrawImage($SourceImage, $x, $y, $drawWidth, $drawHeight)
        } finally {
            $graphics.Dispose()
        }

        $memory = [System.IO.MemoryStream]::new()
        try {
            $writer = [System.IO.BinaryWriter]::new($memory)
            try {
                $xorStride = $Size * 4
                $andStride = [int]([Math]::Ceiling($Size / 32.0) * 4)
                $xorBytes = $xorStride * $Size
                $andBytes = $andStride * $Size

                $writer.Write([UInt32]40)
                $writer.Write([Int32]$Size)
                $writer.Write([Int32]($Size * 2))
                $writer.Write([UInt16]1)
                $writer.Write([UInt16]32)
                $writer.Write([UInt32]0)
                $writer.Write([UInt32]$xorBytes)
                $writer.Write([Int32]0)
                $writer.Write([Int32]0)
                $writer.Write([UInt32]0)
                $writer.Write([UInt32]0)

                for ($y = $Size - 1; $y -ge 0; $y--) {
                    for ($x = 0; $x -lt $Size; $x++) {
                        $pixel = $bitmap.GetPixel($x, $y)
                        $writer.Write([byte]$pixel.B)
                        $writer.Write([byte]$pixel.G)
                        $writer.Write([byte]$pixel.R)
                        $writer.Write([byte]$pixel.A)
                    }
                }

                $writer.Write([byte[]]::new($andBytes))
                return $memory.ToArray()
            } finally {
                $writer.Dispose()
            }
        } finally {
            $memory.Dispose()
        }
    } finally {
        $bitmap.Dispose()
    }
}

function New-InstallerSetupIcon {
    param(
        [string]$SourcePath,
        [string]$OutputDir
    )

    $resolvedSource = (Resolve-Path -LiteralPath $SourcePath).ProviderPath
    if ([System.IO.Path]::GetExtension($resolvedSource).ToLowerInvariant() -eq ".ico") {
        return [ordered]@{
            source = $resolvedSource
            path = $resolvedSource
        }
    }

    Add-Type -AssemblyName System.Drawing

    $sourceImage = [System.Drawing.Image]::FromFile($resolvedSource)
    try {
        $iconPath = Join-Path $OutputDir "ck_microchip_setup.ico"
        $frames = @()
        foreach ($size in @(16, 32, 48, 64, 256)) {
            $frames += , [ordered]@{
                size = $size
                bytes = New-IconDibFrameBytes -SourceImage $sourceImage -Size $size
            }
        }

        $fileStream = [System.IO.File]::Open(
            $iconPath,
            [System.IO.FileMode]::Create,
            [System.IO.FileAccess]::Write)
        try {
            $writer = [System.IO.BinaryWriter]::new($fileStream)
            try {
                $writer.Write([UInt16]0)
                $writer.Write([UInt16]1)
                $writer.Write([UInt16]$frames.Count)

                $offset = 6 + (16 * $frames.Count)
                foreach ($frame in $frames) {
                    $dimension = if ($frame.size -eq 256) { 0 } else { $frame.size }
                    $writer.Write([byte]$dimension)
                    $writer.Write([byte]$dimension)
                    $writer.Write([byte]0)
                    $writer.Write([byte]0)
                    $writer.Write([UInt16]1)
                    $writer.Write([UInt16]32)
                    $writer.Write([UInt32]$frame.bytes.Length)
                    $writer.Write([UInt32]$offset)
                    $offset += $frame.bytes.Length
                }

                foreach ($frame in $frames) {
                    $writer.Write([byte[]]$frame.bytes)
                }
            } finally {
                $writer.Dispose()
            }
        } finally {
            $fileStream.Dispose()
        }

        [ordered]@{
            source = $resolvedSource
            path = $iconPath
        }
    } finally {
        $sourceImage.Dispose()
    }
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

function Format-InstallerSizeLabel {
    param([Int64]$Bytes)
    if ($Bytes -ge 1000000000) {
        return ("{0:0.0} GB" -f ($Bytes / 1000000000.0))
    }
    return ("{0:0} MB" -f ($Bytes / 1000000.0))
}

function Get-InstallerSurfaceConfig {
    param(
        [ValidateSet("ofx", "adobe")]
        [string]$Surface
    )

    switch ($Surface) {
        "adobe" {
            $helperCode = @'
function StripTrailingBackslash(Value: String): String;
begin
  Result := Value;
  while (Length(Result) > 3) and (Copy(Result, Length(Result), 1) = '\') do
    Delete(Result, Length(Result), 1);
end;

function GetAdobeMediaCoreDir(Param: String): String;
var
  CommonPath: String;
begin
  if RegQueryStringValue(HKLM, 'SOFTWARE\Adobe\After Effects\26.0', 'CommonPluginInstallPath', CommonPath) then
    Result := StripTrailingBackslash(CommonPath)
  else if RegQueryStringValue(HKLM, 'SOFTWARE\Adobe\After Effects\25.0', 'CommonPluginInstallPath', CommonPath) then
    Result := StripTrailingBackslash(CommonPath)
  else
    Result := ExpandConstant('{autopf}\Adobe\Common\Plug-ins\7.0\MediaCore');
end;
'@
            return [ordered]@{
                app_id = "{{9F11F5D7-62CA-4350-B92A-C4B2C2B59A4D}}"
                app_name = "CorridorKey Adobe"
                version_info_description = "CorridorKey Adobe plugin installer"
                default_dir_name = "{code:GetAdobeMediaCoreDir}\CorridorKey"
                plugin_base_description = "Base Adobe plugin (AEX + CLI + ONNX runtime)"
                host_stop_process_lines = @(
                    "  StopProcessByImageName('AfterFX.exe');",
                    "  StopProcessByImageName('Adobe Premiere Pro.exe');",
                    "  StopProcessByImageName('Adobe Media Encoder.exe');"
                ) -join "`r`n"
                host_cache_clear_lines = ""
                surface_helper_code = $helperCode.TrimEnd()
            }
        }
        default {
            return [ordered]@{
                app_id = "{{8B8C9CF7-9C20-4D8E-9E36-C3A4F0F1E0B0}}"
                app_name = "CorridorKey"
                version_info_description = "CorridorKey OFX plugin installer"
                default_dir_name = "{commoncf64}\OFX\Plugins\{#MyAppName}.ofx.bundle"
                plugin_base_description = "Base OFX plugin (CLI + ONNX runtime)"
                host_stop_process_lines = @(
                    "  StopProcessByImageName('Resolve.exe');",
                    "  StopProcessByImageName('Nuke*.exe');"
                ) -join "`r`n"
                host_cache_clear_lines = @(
                    "  DeleteFile(ExpandConstant('{userappdata}\Blackmagic Design\DaVinci Resolve\Support\OFXPluginCacheV2.xml'));",
                    "  DelTree(ExpandConstant('{localappdata}\Temp\nuke\ofxplugincache'), True, True, True);"
                ) -join "`r`n"
                surface_helper_code = ""
            }
        }
    }
}

function Get-PackDownloadSizeBytes {
    param([object]$PackMeta)
    $sum = [Int64]0
    foreach ($file in $PackMeta.files) {
        if ($file.status -ne 'ready' -or $null -eq $file.size_bytes) { continue }
        $sum += [Int64]$file.size_bytes
    }
    return $sum
}

function Get-PackInstalledSizeBytes {
    param([object]$PackMeta)
    $explicit = $PackMeta.PSObject.Properties.Match('installed_size_bytes')
    if ($explicit.Count -gt 0 -and $null -ne $PackMeta.installed_size_bytes) {
        return [Int64]$PackMeta.installed_size_bytes
    }
    return Get-PackDownloadSizeBytes -PackMeta $PackMeta
}

function Get-ComponentSizeLabel {
    param([object]$Manifest, [string]$Component)
    $downloadBytes = [Int64]0
    $installedBytes = [Int64]0
    foreach ($pack in $Manifest.packs.PSObject.Properties) {
        $packMeta = $pack.Value
        if ($packMeta.component -ne $Component) { continue }
        $downloadBytes += Get-PackDownloadSizeBytes -PackMeta $packMeta
        $installedBytes += Get-PackInstalledSizeBytes -PackMeta $packMeta
    }
    $installedLabel = Format-InstallerSizeLabel -Bytes $installedBytes
    $downloadLabel = Format-InstallerSizeLabel -Bytes $downloadBytes
    if ($installedBytes -eq $downloadBytes) {
        return $installedLabel
    }
    return "$installedLabel installed, $downloadLabel download"
}

function Get-PackByName {
    param([object]$Manifest, [string]$PackName)
    $match = $Manifest.packs.PSObject.Properties.Match($PackName)
    if ($match.Count -eq 0) {
        throw "Distribution manifest missing pack: $PackName"
    }
    return $match[0].Value
}

function Test-PackExtractsArchive {
    param([object]$PackMeta)
    return ($PackMeta.PSObject.Properties.Match('is_archive').Count -gt 0 -and $PackMeta.is_archive) `
        -and ($PackMeta.PSObject.Properties.Match('extract').Count -gt 0 -and $PackMeta.extract)
}

function Get-PackAggregateSha256 {
    # Aggregate hash for a pack's "ready" files. Algorithm: take each
    # ready file's sha256 (lowercased hex), sort by filename, join with
    # \n, then SHA256 the resulting UTF-8 byte sequence. The marker file
    # written at install-time embeds this exact string; the Pascal
    # `CorridorKeyPackCacheValid` helper compares against it. A manifest
    # edit (new file, hash change, file removed) naturally produces a
    # different aggregate and invalidates the cache.
    param([object]$PackMeta)
    $readyFiles = @($PackMeta.files | Where-Object { $_.status -eq 'ready' } | Sort-Object filename)
    if ($readyFiles.Count -eq 0) {
        return ""
    }
    $sb = [System.Text.StringBuilder]::new()
    foreach ($file in $readyFiles) {
        [void]$sb.AppendLine($file.sha256.ToLowerInvariant())
    }
    $sha = [System.Security.Cryptography.SHA256]::Create()
    try {
        $bytes = [System.Text.Encoding]::UTF8.GetBytes($sb.ToString())
        $hash = $sha.ComputeHash($bytes)
        return -join ($hash | ForEach-Object { $_.ToString("x2") })
    } finally {
        $sha.Dispose()
    }
}

function Get-PackCacheCheck {
    # Render the `not CorridorKeyPackCacheValid(...)` Pascal expression
    # used both in [Files] Check: parameters and in the body of the
    # generated `CorridorKeyEnqueueDownloads` / `CorridorKeyPrepareSelectedPackCaches`
    # procedures. Pack name is the manifest key; dest_subdir is the
    # install-time location relative to {app}\Contents\Resources.
    param(
        [string]$DestSubdir,
        [string]$PackName,
        [string]$AggregateSha256
    )
    $subdir = ConvertTo-IssEscapedString -Value ($DestSubdir -replace '/', '\')
    $name = ConvertTo-IssEscapedString -Value $PackName
    $hash = ConvertTo-IssEscapedString -Value $AggregateSha256
    return "CorridorKeyPackCacheValid('$subdir', '$name', '$hash')"
}

function Build-OnlineExternalFilesBlock {
    param([object]$Manifest)
    $sb = [System.Text.StringBuilder]::new()
    [void]$sb.AppendLine('[Files]')
    foreach ($pack in $Manifest.packs.PSObject.Properties) {
        $packMeta = $pack.Value
        $component = $packMeta.component
        $destSubdir = $packMeta.dest_subdir -replace '/', '\'
        $isExtractArchive = Test-PackExtractsArchive -PackMeta $packMeta
        $packAggregateSha256 = Get-PackAggregateSha256 -PackMeta $packMeta
        foreach ($file in $packMeta.files) {
            if ($file.status -ne 'ready') { continue }
            $line = "Source: `"{tmp}\$($file.filename)`"; DestDir: `"{app}\Contents\Resources\$destSubdir`"; Components: $component; ExternalSize: $($file.size_bytes); Flags: external ignoreversion"
            if ($isExtractArchive) {
                $line += " extractarchive recursesubdirs"
                if ($pack.Name -eq "blue-runtime") {
                    $line += "; Check: not CorridorKeyBlueRuntimeCacheValid"
                }
            } else {
                $line += "; Check: not $(Get-PackCacheCheck -DestSubdir $destSubdir -PackName $pack.Name -AggregateSha256 $packAggregateSha256)"
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
        $isExtractArchive = Test-PackExtractsArchive -PackMeta $packMeta
        $packAggregateSha256 = Get-PackAggregateSha256 -PackMeta $packMeta
        if ($isExtractArchive) {
            if (-not (Test-Path $packDir)) {
                throw "Offline payload missing pre-extracted archive dir: $packDir. Re-run scripts/installer/stage_offline_payload.ps1."
            }
            $hasContent = @(Get-ChildItem -Path $packDir -File -ErrorAction SilentlyContinue).Count -gt 0
            if (-not $hasContent) {
                throw "Offline payload pre-extraction dir is empty: $packDir. The archive download may have failed; re-run staging."
            }
            $sourceForIss = ((Join-Path $packDir '*') -replace '/', '\') -replace '\\\\', '\'
            $line = "Source: `"$sourceForIss`"; DestDir: `"{app}\Contents\Resources\$destSubdir`"; Components: $component; Flags: ignoreversion recursesubdirs createallsubdirs"
            if ($pack.Name -eq "blue-runtime") {
                $line += "; Check: not CorridorKeyBlueRuntimeCacheValid"
            }
            [void]$sb.AppendLine($line)
            continue
        }
        foreach ($file in $packMeta.files) {
            if ($file.status -ne 'ready') { continue }
            $sourcePath = Join-Path $packDir $file.filename
            if (-not (Test-Path $sourcePath)) {
                throw "Offline payload missing file: $sourcePath. Pre-populate before invoking with -Flavor offline."
            }
            $sourceForIss = ($sourcePath -replace '/', '\') -replace '\\\\', '\'
            [void]$sb.AppendLine("Source: `"$sourceForIss`"; DestDir: `"{app}\Contents\Resources\$destSubdir`"; Components: $component; Flags: ignoreversion; Check: not $(Get-PackCacheCheck -DestSubdir $destSubdir -PackName $pack.Name -AggregateSha256 $packAggregateSha256)")
        }
    }
    return $sb.ToString().TrimEnd()
}

function Build-PackCachePrepareProcedure {
    # Runs from PrepareToInstall (main thread, after wpSelectTasks). Two
    # decision points per pack:
    #   1. Component deselected → wipe the pack's files + marker so a
    #      re-selection on a future install does a clean download. Use
    #      per-file DeleteFile rather than DelTree because two non-archive
    #      packs share `models\`; nuking the directory would clobber the
    #      sibling pack.
    #   2. Component selected → if WizardIsTaskSelected('cleaninstall')
    #      OR the pack cache marker is stale/missing, delete the files
    #      and the marker so the [Files] Check and the download queue
    #      both fall through to the install path. The blue-runtime
    #      archive pack keeps its existing CorridorKeyBlueRuntimeCacheValid
    #      gate (also bypassed by the cleaninstall task).
    param([object]$Manifest)
    $sb = [System.Text.StringBuilder]::new()
    [void]$sb.AppendLine('procedure CorridorKeyPrepareSelectedPackCaches;')
    [void]$sb.AppendLine('begin')
    foreach ($component in @('green', 'blue')) {
        $componentPacks = @($Manifest.packs.PSObject.Properties | Where-Object { $_.Value.component -eq $component })
        if ($componentPacks.Count -eq 0) { continue }
        [void]$sb.AppendLine("  if WizardIsComponentSelected('$component') then begin")
        foreach ($pack in $componentPacks) {
            $packMeta = $pack.Value
            $destSubdir = ConvertTo-IssEscapedString -Value ($packMeta.dest_subdir -replace '/', '\')
            $packNameEscaped = ConvertTo-IssEscapedString -Value $pack.Name
            if (Test-PackExtractsArchive -PackMeta $packMeta) {
                if ($pack.Name -eq "blue-runtime") {
                    [void]$sb.AppendLine("    if WizardIsTaskSelected('cleaninstall') or not CorridorKeyBlueRuntimeCacheValid then begin")
                    [void]$sb.AppendLine('      DelTree(CorridorKeyBlueRuntimeBinPath, True, True, True);')
                    [void]$sb.AppendLine('    end;')
                }
                continue
            }
            $aggregate = Get-PackAggregateSha256 -PackMeta $packMeta
            $aggregateEscaped = ConvertTo-IssEscapedString -Value $aggregate
            [void]$sb.AppendLine("    if WizardIsTaskSelected('cleaninstall') or not CorridorKeyPackCacheValid('$destSubdir', '$packNameEscaped', '$aggregateEscaped') then begin")
            foreach ($file in $packMeta.files) {
                if ($file.status -ne 'ready') { continue }
                $name = ConvertTo-IssEscapedString -Value $file.filename
                [void]$sb.AppendLine("      CorridorKeyDeleteResourceFile('$destSubdir', '$name');")
            }
            [void]$sb.AppendLine("      CorridorKeyDeletePackMarker('$destSubdir', '$packNameEscaped');")
            [void]$sb.AppendLine('    end;')
        }
        [void]$sb.AppendLine('  end else begin')
        foreach ($pack in $componentPacks) {
            $packMeta = $pack.Value
            $destSubdir = ConvertTo-IssEscapedString -Value ($packMeta.dest_subdir -replace '/', '\')
            $packNameEscaped = ConvertTo-IssEscapedString -Value $pack.Name
            if (Test-PackExtractsArchive -PackMeta $packMeta) {
                if ($pack.Name -eq "blue-runtime") {
                    [void]$sb.AppendLine("    DelTree(ExpandConstant('{app}\Contents\Resources\torchtrt-runtime'), True, True, True);")
                }
                continue
            }
            foreach ($file in $packMeta.files) {
                if ($file.status -ne 'ready') { continue }
                $name = ConvertTo-IssEscapedString -Value $file.filename
                [void]$sb.AppendLine("    CorridorKeyDeleteResourceFile('$destSubdir', '$name');")
            }
            [void]$sb.AppendLine("    CorridorKeyDeletePackMarker('$destSubdir', '$packNameEscaped');")
        }
        [void]$sb.AppendLine('  end;')
    }
    [void]$sb.AppendLine('end;')
    return $sb.ToString().TrimEnd()
}

function Build-PackMigrationProcedure {
    # Runs from NextButtonClick(wpReady), after {app} is initialized and
    # before the download queue is planned. For each non-archive pack, if
    # the marker file is absent but every manifest file is already on disk
    # with the expected size, write the marker now so the subsequent
    # CorridorKeyPackCacheValid check short-circuits to "cache valid" and
    # the install skips downloads. Conservative: any missing file or size
    # mismatch leaves the marker absent and the normal download flow fires.
    #
    # Size-only validation here mirrors CorridorKeyBlueRuntimeFilesMatch
    # (corridorkey.iss.template), which has been in production for the
    # multi-gigabyte blue-runtime pack since the cache marker pattern
    # landed. The "Clean install" task remains the explicit recovery
    # path for any rare same-size corruption that slips through.
    param([object]$Manifest)
    $sb = [System.Text.StringBuilder]::new()
    [void]$sb.AppendLine('procedure CorridorKeyMigrateLegacyPackMarkers;')
    [void]$sb.AppendLine('var')
    [void]$sb.AppendLine('  AllPresent: Boolean;')
    [void]$sb.AppendLine('  Size: Int64;')
    [void]$sb.AppendLine('begin')
    foreach ($pack in $Manifest.packs.PSObject.Properties) {
        $packMeta = $pack.Value
        if (Test-PackExtractsArchive -PackMeta $packMeta) { continue }
        $readyFiles = @($packMeta.files | Where-Object { $_.status -eq 'ready' })
        if ($readyFiles.Count -eq 0) { continue }
        $destSubdir = ConvertTo-IssEscapedString -Value ($packMeta.dest_subdir -replace '/', '\')
        $packNameEscaped = ConvertTo-IssEscapedString -Value $pack.Name
        $aggregate = Get-PackAggregateSha256 -PackMeta $packMeta
        $aggregateEscaped = ConvertTo-IssEscapedString -Value $aggregate
        [void]$sb.AppendLine("  if not FileExists(CorridorKeyPackMarkerPath('$destSubdir', '$packNameEscaped')) then begin")
        [void]$sb.AppendLine('    AllPresent := True;')
        foreach ($file in $readyFiles) {
            $name = ConvertTo-IssEscapedString -Value $file.filename
            $expectedSize = [Int64]$file.size_bytes
            [void]$sb.AppendLine('    if AllPresent then begin')
            [void]$sb.AppendLine("      AllPresent := FileSize64(CorridorKeyResourceFilePath('$destSubdir', '$name'), Size) and (Size = Int64($expectedSize));")
            [void]$sb.AppendLine('    end;')
        }
        [void]$sb.AppendLine('    if AllPresent then begin')
        [void]$sb.AppendLine("      Log('CorridorKey: legacy cache detected for pack ""$packNameEscaped""; back-filling marker.');")
        [void]$sb.AppendLine("      CorridorKeyWritePackMarker('$destSubdir', '$packNameEscaped', '$aggregateEscaped');")
        [void]$sb.AppendLine('    end;')
        [void]$sb.AppendLine('  end;')
    }
    [void]$sb.AppendLine('end;')
    return $sb.ToString().TrimEnd()
}

function Build-PackMarkerWriteProcedure {
    # Runs from CurStepChanged(ssPostInstall). For every selected
    # component's non-archive packs, write the aggregate-hash marker so
    # the next install short-circuits the entire pack via
    # CorridorKeyPackCacheValid. The blue-runtime marker is handled
    # separately by CorridorKeyWriteBlueRuntimeCacheMarker.
    param([object]$Manifest)
    $sb = [System.Text.StringBuilder]::new()
    [void]$sb.AppendLine('procedure CorridorKeyWriteSelectedPackMarkers;')
    [void]$sb.AppendLine('begin')
    foreach ($component in @('green', 'blue')) {
        $componentPacks = @($Manifest.packs.PSObject.Properties | Where-Object { $_.Value.component -eq $component })
        $writable = @($componentPacks | Where-Object { -not (Test-PackExtractsArchive -PackMeta $_.Value) })
        if ($writable.Count -eq 0) { continue }
        [void]$sb.AppendLine("  if WizardIsComponentSelected('$component') then begin")
        foreach ($pack in $writable) {
            $packMeta = $pack.Value
            $destSubdir = ConvertTo-IssEscapedString -Value ($packMeta.dest_subdir -replace '/', '\')
            $packNameEscaped = ConvertTo-IssEscapedString -Value $pack.Name
            $aggregate = Get-PackAggregateSha256 -PackMeta $packMeta
            $aggregateEscaped = ConvertTo-IssEscapedString -Value $aggregate
            [void]$sb.AppendLine("    CorridorKeyWritePackMarker('$destSubdir', '$packNameEscaped', '$aggregateEscaped');")
        }
        [void]$sb.AppendLine('  end;')
    }
    [void]$sb.AppendLine('end;')
    return $sb.ToString().TrimEnd()
}

function Build-OnlineDownloadQueueProcedure {
    # Runs from NextButtonClick(wpReady) on the wizard UI thread. Pack-
    # level cache predicate (one tiny marker read per pack) replaces the
    # legacy per-file GetSHA256OfFile sweep that previously hashed every
    # locally cached model on the UI thread and produced a multi-second
    # freeze after the user clicked Install. Per-file SHA256 integrity
    # is still enforced via DownloadPage.Add's third argument (Inno
    # verifies after download, before files leave {tmp}).
    #
    # Clean install gate: `WizardIsTaskSelected('cleaninstall')` short-
    # circuits the per-pack cache predicate so every selected pack
    # enqueues unconditionally. Without this gate the wipe in
    # CorridorKeyPrepareSelectedPackCaches (which runs in
    # PrepareToInstall, after wpReady) would delete the staged files
    # but the download queue would already have skipped them — the
    # subsequent [Files] install step would then find an empty {tmp}
    # entry and fail.
    param([object]$Manifest)
    $sb = [System.Text.StringBuilder]::new()
    [void]$sb.AppendLine('procedure CorridorKeyEnqueueDownloads(const DownloadPage: TDownloadWizardPage);')
    [void]$sb.AppendLine('begin')
    foreach ($pack in $Manifest.packs.PSObject.Properties) {
        $packMeta = $pack.Value
        $component = $packMeta.component
        $readyFiles = @($packMeta.files | Where-Object { $_.status -eq 'ready' })
        if ($readyFiles.Count -eq 0) { continue }
        $destSubdir = ConvertTo-IssEscapedString -Value ($packMeta.dest_subdir -replace '/', '\')
        $packNameEscaped = ConvertTo-IssEscapedString -Value $pack.Name
        [void]$sb.AppendLine("  if WizardIsComponentSelected('$component') then begin")
        if ($pack.Name -eq "blue-runtime") {
            [void]$sb.AppendLine("    if WizardIsTaskSelected('cleaninstall') or not CorridorKeyBlueRuntimeCacheValid then begin")
            foreach ($file in $readyFiles) {
                $url = ConvertTo-IssEscapedString -Value $file.url
                $name = ConvertTo-IssEscapedString -Value $file.filename
                $hash = $file.sha256
                [void]$sb.AppendLine("      DownloadPage.Add('$url', '$name', '$hash');")
                [void]$sb.AppendLine("      CorridorKeyTrackPlannedDownload($($file.size_bytes));")
            }
            [void]$sb.AppendLine('    end;')
        } else {
            $aggregate = Get-PackAggregateSha256 -PackMeta $packMeta
            $aggregateEscaped = ConvertTo-IssEscapedString -Value $aggregate
            [void]$sb.AppendLine("    if WizardIsTaskSelected('cleaninstall') or not CorridorKeyPackCacheValid('$destSubdir', '$packNameEscaped', '$aggregateEscaped') then begin")
            foreach ($file in $readyFiles) {
                $url = ConvertTo-IssEscapedString -Value $file.url
                $name = ConvertTo-IssEscapedString -Value $file.filename
                $hash = $file.sha256
                [void]$sb.AppendLine("      DownloadPage.Add('$url', '$name', '$hash');")
                [void]$sb.AppendLine("      CorridorKeyTrackPlannedDownload($($file.size_bytes));")
            }
            [void]$sb.AppendLine('    end;')
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
$surfaceConfig = Get-InstallerSurfaceConfig -Surface $InstallerSurface
$greenComponentSizeLabel = Get-ComponentSizeLabel -Manifest $manifest -Component "green"
$blueComponentSizeLabel = Get-ComponentSizeLabel -Manifest $manifest -Component "blue"
$blueRuntimePack = Get-PackByName -Manifest $manifest -PackName "blue-runtime"
$blueRuntimeFile = @($blueRuntimePack.files | Where-Object { $_.status -eq 'ready' })[0]
$blueRuntimeSha256 = $blueRuntimeFile.sha256
$blueRuntimeInstalledSizeBytes = [Int64]$blueRuntimePack.installed_size_bytes
$blueRuntimeInstalledFileCount = [Int64]$blueRuntimePack.installed_file_count

$onlineFilesBlock = ""
$offlineFilesBlock = ""
$downloadQueueProcedure = ""
$packCachePrepareProcedure = Build-PackCachePrepareProcedure -Manifest $manifest
$packMarkerWriteProcedure = Build-PackMarkerWriteProcedure -Manifest $manifest
$packMigrationProcedure = Build-PackMigrationProcedure -Manifest $manifest
if ($Flavor -eq "online") {
    $onlineFilesBlock = Build-OnlineExternalFilesBlock -Manifest $manifest
    $downloadQueueProcedure = Build-OnlineDownloadQueueProcedure -Manifest $manifest
} else {
    $offlineFilesBlock = Build-OfflineFilesBlock -Manifest $manifest -PayloadRoot $ModelPayloadDir
}

$flavorLower = $Flavor.ToLowerInvariant()
if ([string]::IsNullOrWhiteSpace($OutputBaseFilename)) {
    $OutputBaseFilename = if ($InstallerSurface -eq "adobe") {
        "CorridorKey_Adobe_v${DisplayVersionLabel}_Windows_${flavorLower}_Setup"
    } else {
        "CorridorKey_v${DisplayVersionLabel}_Windows_${flavorLower}_Setup"
    }
}
$appVersionField = if ($InstallerSurface -eq "adobe") { $DisplayVersionLabel } else { $Version }

$tempIssDir = Join-Path $env:TEMP ("corridorkey_iss_" + [System.Guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $tempIssDir -Force | Out-Null
$setupIcon = New-InstallerSetupIcon -SourcePath $InstallerIcon -OutputDir $tempIssDir
$wizardImagePath = (Resolve-Path -LiteralPath $InstallerWizardImage).ProviderPath

$template = Get-Content -Raw -Path $templatePath
$rendered = $template `
    -replace '@@APP_ID@@', $surfaceConfig.app_id `
    -replace '@@APP_NAME@@', $surfaceConfig.app_name `
    -replace '@@VERSION_INFO_DESCRIPTION@@', $surfaceConfig.version_info_description `
    -replace '@@DEFAULT_DIR_NAME@@', $surfaceConfig.default_dir_name `
    -replace '@@PLUGIN_BASE_DESCRIPTION@@', $surfaceConfig.plugin_base_description `
    -replace '@@DISPLAY_LABEL@@', $DisplayVersionLabel `
    -replace '@@BASE_VERSION@@', $Version `
    -replace '@@APP_VERSION@@', $appVersionField `
    -replace '@@PLUGIN_PAYLOAD_DIR@@', ($PluginPayloadDir -replace '/', '\') `
    -replace '@@MODEL_PAYLOAD_DIR@@', (($ModelPayloadDir -replace '/', '\')) `
    -replace '@@OUTPUT_DIR@@', ($OutputDir -replace '/', '\') `
    -replace '@@OUTPUT_BASE_FILENAME@@', $OutputBaseFilename `
    -replace '@@INSTALLER_ICON@@', ($setupIcon.path -replace '/', '\') `
    -replace '@@WIZARD_IMAGE@@', ($wizardImagePath -replace '/', '\') `
    -replace '@@MANIFEST_PATH@@', ($ManifestPath -replace '/', '\') `
    -replace '@@INSTALLER_SURFACE@@', $InstallerSurface `
    -replace '@@FLAVOR@@', $flavorLower `
    -replace '@@GREEN_COMPONENT_SIZE_LABEL@@', $greenComponentSizeLabel `
    -replace '@@BLUE_COMPONENT_SIZE_LABEL@@', $blueComponentSizeLabel `
    -replace '@@BLUE_RUNTIME_SHA256@@', $blueRuntimeSha256 `
    -replace '@@BLUE_RUNTIME_INSTALLED_SIZE_BYTES@@', $blueRuntimeInstalledSizeBytes `
    -replace '@@BLUE_RUNTIME_INSTALLED_FILE_COUNT@@', $blueRuntimeInstalledFileCount

# Inject generated Pascal/.iss blocks AFTER simple token replacement.
# These blocks may contain regex metacharacters (`$`, `{`), so use
# String.Replace (literal, no interpretation) instead of -replace.
$rendered = $rendered.Replace('@@OFFLINE_FILES_BLOCK@@', $offlineFilesBlock)
$rendered = $rendered.Replace('@@ONLINE_EXTERNAL_FILES_BLOCK@@', $onlineFilesBlock)
$rendered = $rendered.Replace('@@ONLINE_DOWNLOAD_QUEUE_PROCEDURE@@', $downloadQueueProcedure)
$rendered = $rendered.Replace('@@PACK_CACHE_PREPARE_PROCEDURE@@', $packCachePrepareProcedure)
$rendered = $rendered.Replace('@@PACK_MARKER_WRITE_PROCEDURE@@', $packMarkerWriteProcedure)
$rendered = $rendered.Replace('@@PACK_MIGRATION_PROCEDURE@@', $packMigrationProcedure)
$rendered = $rendered.Replace('@@SURFACE_HELPER_CODE@@', $surfaceConfig.surface_helper_code)
$rendered = $rendered.Replace('@@HOST_STOP_PROCESS_LINES@@', $surfaceConfig.host_stop_process_lines)
$rendered = $rendered.Replace('@@HOST_CACHE_CLEAR_LINES@@', $surfaceConfig.host_cache_clear_lines)

$tempIssPath = Join-Path $tempIssDir "corridorkey_setup.iss"
Set-Content -Path $tempIssPath -Value $rendered -Encoding UTF8

Write-Host "[installer] Flavor:        $Flavor" -ForegroundColor Cyan
Write-Host "[installer] Surface:       $InstallerSurface" -ForegroundColor Cyan
Write-Host "[installer] Display label: $DisplayVersionLabel" -ForegroundColor Cyan
Write-Host "[installer] Plugin dir:    $PluginPayloadDir" -ForegroundColor Cyan
Write-Host "[installer] Setup icon:    $($setupIcon.source)" -ForegroundColor Cyan
Write-Host "[installer] Wizard image:  $wizardImagePath" -ForegroundColor Cyan
if ($Flavor -eq "offline") {
    Write-Host "[installer] Model dir:     $ModelPayloadDir" -ForegroundColor Cyan
}
Write-Host "[installer] Output:        $OutputDir\$OutputBaseFilename.exe" -ForegroundColor Cyan
Write-Host "[installer] ISCC:          $iscc" -ForegroundColor Cyan
Write-Host "[installer] Generated iss: $tempIssPath" -ForegroundColor DarkGray

if (-not (Test-Path $OutputDir)) {
    New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null
}

& $iscc /Q $tempIssPath
if ($LASTEXITCODE -ne 0) {
    throw "ISCC failed with exit code $LASTEXITCODE. Inspect $tempIssPath for diagnostics."
}

$producedInstaller = Join-Path $OutputDir ($OutputBaseFilename + ".exe")
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
    surface = $InstallerSurface
    display_label = $DisplayVersionLabel
    base_version = $Version
    installer_path = $producedInstaller
    size_bytes = (Get-Item $producedInstaller).Length
    sha256 = $sha256
} | ConvertTo-Json -Depth 4
