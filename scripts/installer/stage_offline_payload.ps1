<#
.SYNOPSIS
    Stage every distribution-manifest pack into a flat payload tree
    suitable for the Inno Setup OFFLINE flavor.

.DESCRIPTION
    The offline installer (`scripts/installer/build_installer.ps1
    -Flavor offline`) bundles every selected pack inside the .exe
    so end users never need network access at install time. To do
    that, the Inno Setup `[Files]` block needs every file laid out
    on disk in the pack's `dest_subdir` shape (see
    distribution_manifest.json).

    This helper downloads every "ready" file from Hugging Face into
    the payload tree, verifying SHA256 against the manifest. The
    blue runtime archive (~2 GB) is downloaded as-is; the Inno Setup
    `extractarchive` flag handles extraction at install time, so we
    do NOT pre-extract here.

    The payload tree layout under -OutputDir matches what
    build_installer.ps1 -Flavor offline expects:

        <OutputDir>/
          models/
            corridorkey_fp16_512.onnx
            ...
          torchtrt-runtime/bin/
            corridorkey_blue_torchtrt_runtime.7z

.PARAMETER OutputDir
    Where to stage the payload. Defaults to dist/_offline_payload/.
    Existing files matching the manifest sha256 are kept (idempotent
    re-runs do not re-download multi-GB packs).

.PARAMETER ManifestPath
    Distribution manifest. Defaults to
    scripts/installer/distribution_manifest.json.

.PARAMETER Force
    Re-download every file even if the local sha256 matches the
    manifest. Use when you suspect cache poisoning or after a
    manifest regeneration that changed an artifact in place (which
    SHOULD never happen per docs immutability rules, but the flag
    is here for diagnostic use).

.EXAMPLE
    pwsh scripts/installer/stage_offline_payload.ps1
#>

[CmdletBinding()]
param(
    [string]$OutputDir = "",
    [string]$ManifestPath = "",
    [switch]$Force
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
if ([string]::IsNullOrWhiteSpace($OutputDir)) {
    $OutputDir = Join-Path $repoRoot "dist\_offline_payload"
}
if ([string]::IsNullOrWhiteSpace($ManifestPath)) {
    $ManifestPath = Join-Path $PSScriptRoot "distribution_manifest.json"
}

if (-not (Test-Path $ManifestPath)) {
    throw "Distribution manifest not found: $ManifestPath. Run build_distribution_manifest.py first."
}

$manifest = Get-Content -Raw -Path $ManifestPath | ConvertFrom-Json
if (-not (Test-Path $OutputDir)) {
    New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null
}

function Test-Sha256Match {
    param(
        [string]$Path,
        [string]$Expected
    )
    if (-not (Test-Path $Path)) { return $false }
    $actual = (Get-FileHash -Path $Path -Algorithm SHA256).Hash.ToLowerInvariant()
    return ($actual -eq $Expected.ToLowerInvariant())
}

function Get-FileFromHuggingFace {
    param(
        [string]$Url,
        [string]$DestPath,
        [string]$ExpectedSha256
    )
    $destDir = Split-Path -Parent $DestPath
    if (-not (Test-Path $destDir)) {
        New-Item -ItemType Directory -Path $destDir -Force | Out-Null
    }

    if ((-not $Force.IsPresent) -and (Test-Sha256Match -Path $DestPath -Expected $ExpectedSha256)) {
        Write-Host "  [skip] $(Split-Path -Leaf $DestPath) (sha256 matches manifest)" -ForegroundColor DarkGray
        return
    }

    Write-Host "  [download] $(Split-Path -Leaf $DestPath)" -ForegroundColor Cyan
    # Stream to a .partial file then rename atomically so a Ctrl+C
    # mid-download never leaves a half-written file shadowing the
    # sha256 cache check next run.
    $partial = "$DestPath.partial"
    if (Test-Path $partial) { Remove-Item $partial -Force }
    Invoke-WebRequest -Uri $Url -OutFile $partial -UseBasicParsing

    $actual = (Get-FileHash -Path $partial -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actual -ne $ExpectedSha256.ToLowerInvariant()) {
        Remove-Item $partial -Force
        throw "SHA256 mismatch for $Url: expected $ExpectedSha256, got $actual"
    }
    if (Test-Path $DestPath) { Remove-Item $DestPath -Force }
    Move-Item -Path $partial -Destination $DestPath
}

$totalReady = 0
$totalSkipped = 0
foreach ($pack in $manifest.packs.PSObject.Properties) {
    $packMeta = $pack.Value
    Write-Host "[$($pack.Name)] -> $($packMeta.dest_subdir)" -ForegroundColor Yellow
    $destSubdir = $packMeta.dest_subdir -replace '/', '\'
    foreach ($file in $packMeta.files) {
        if ($file.status -ne "ready") {
            Write-Host "  [pending] $($file.filename) (status=$($file.status); skip)" -ForegroundColor Yellow
            $totalSkipped++
            continue
        }
        $destPath = Join-Path (Join-Path $OutputDir $destSubdir) $file.filename
        Get-FileFromHuggingFace -Url $file.url -DestPath $destPath -ExpectedSha256 $file.sha256
        $totalReady++
    }
}

Write-Host ""
Write-Host "[done] Staged $totalReady file(s) under $OutputDir; $totalSkipped pending entries skipped." -ForegroundColor Green
Write-Host "[done] Pass this dir as -ModelPayloadDir to build_installer.ps1 -Flavor offline." -ForegroundColor Green
