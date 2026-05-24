Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$smokeScriptPath = Join-Path $repoRoot "scripts\smoke_adobe_install_win.ps1"

if (-not (Test-Path -LiteralPath $smokeScriptPath)) {
    throw "Expected Adobe install smoke script not found: $smokeScriptPath"
}

function New-PlaceholderFile {
    param([string]$Path)

    $parent = Split-Path -Parent $Path
    [System.IO.Directory]::CreateDirectory($parent) | Out-Null
    [System.IO.File]::WriteAllText($Path, "placeholder", [System.Text.Encoding]::ASCII)
}

$tempRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("corridorkey-adobe-install-smoke-[literal]-" + [System.Guid]::NewGuid().ToString("N"))

try {
    $packageRoot = Join-Path $tempRoot "package"
    $payloadDir = Join-Path $packageRoot "Adobe\Common\Plug-ins\7.0\MediaCore\CorridorKey"
    $win64Dir = Join-Path $payloadDir "Contents\Win64"
    $commonPluginRoot = Join-Path $tempRoot "Adobe\Common\Plug-ins\7.0\MediaCore"
    $targetWin64Dir = Join-Path $commonPluginRoot "CorridorKey\Contents\Win64"
    $cleanReportPath = Join-Path $tempRoot "clean_report.json"
    $upgradeReportPath = Join-Path $tempRoot "upgrade_report.json"

    foreach ($fileName in @(
            "corridorkey_adobe_green.aex",
            "corridorkey_adobe_blue.aex",
            "corridorkey.exe",
            "corridorkey_host_plugin_runtime_server.exe"
        )) {
        New-PlaceholderFile -Path (Join-Path $win64Dir $fileName)
    }

    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $smokeScriptPath `
        -PackagePath $packageRoot `
        -CommonPluginInstallPath $commonPluginRoot `
        -Mode clean `
        -ReportPath $cleanReportPath
    if ($LASTEXITCODE -ne 0) {
        throw "Clean Adobe install smoke failed."
    }

    foreach ($fileName in @(
            "corridorkey_adobe_green.aex",
            "corridorkey_adobe_blue.aex",
            "corridorkey.exe",
            "corridorkey_host_plugin_runtime_server.exe"
        )) {
        if (-not (Test-Path -LiteralPath (Join-Path $targetWin64Dir $fileName))) {
            throw "Clean install smoke did not stage discoverable file: $fileName"
        }
    }

    $cleanReport = Get-Content -LiteralPath $cleanReportPath -Raw | ConvertFrom-Json
    if ([string]$cleanReport.mode -ne "clean") {
        throw "Clean install smoke report must identify clean mode."
    }
    if ([string]$cleanReport.host_discovery.after_effects -ne "adobe_common_mediacore_payload_present" -or
        [string]$cleanReport.host_discovery.premiere -ne "adobe_common_mediacore_payload_present") {
        throw "Clean install smoke report must identify Adobe host discovery through Common Plug-ins MediaCore."
    }

    New-PlaceholderFile -Path (Join-Path $targetWin64Dir "corridorkey_adobe.aex")

    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $smokeScriptPath `
        -PackagePath $packageRoot `
        -CommonPluginInstallPath $commonPluginRoot `
        -Mode upgrade `
        -ReportPath $upgradeReportPath
    if ($LASTEXITCODE -ne 0) {
        throw "Upgrade Adobe install smoke failed."
    }

    if (Test-Path -LiteralPath (Join-Path $targetWin64Dir "corridorkey_adobe.aex")) {
        throw "Upgrade install smoke must remove the retired single Adobe effect binary."
    }

    foreach ($fileName in @("corridorkey_adobe_green.aex", "corridorkey_adobe_blue.aex")) {
        if (-not (Test-Path -LiteralPath (Join-Path $targetWin64Dir $fileName))) {
            throw "Upgrade install smoke did not leave discoverable effect: $fileName"
        }
    }

    $upgradeReport = Get-Content -LiteralPath $upgradeReportPath -Raw | ConvertFrom-Json
    if ([string]$upgradeReport.mode -ne "upgrade") {
        throw "Upgrade install smoke report must identify upgrade mode."
    }
    if (-not [bool]$upgradeReport.retired_effect_removed) {
        throw "Upgrade install smoke report must prove the retired single Adobe effect was removed."
    }
} finally {
    if (Test-Path -LiteralPath $tempRoot) {
        Remove-Item -LiteralPath $tempRoot -Recurse -Force
    }
}

Write-Host "[PASS] Adobe install smoke checks passed." -ForegroundColor Green
