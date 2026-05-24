Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$windowsWrapperPath = Join-Path $repoRoot "scripts\windows.ps1"
$hostSmokeScriptPath = Join-Path $repoRoot "scripts\smoke_adobe_after_effects_host_win.ps1"

if (-not (Test-Path -LiteralPath $windowsWrapperPath)) {
    throw "Expected Windows wrapper not found: $windowsWrapperPath"
}
if (-not (Test-Path -LiteralPath $hostSmokeScriptPath)) {
    throw "Expected Adobe host smoke script not found: $hostSmokeScriptPath"
}

$windowsWrapper = Get-Content -LiteralPath $windowsWrapperPath -Raw
$hostSmokeScript = Get-Content -LiteralPath $hostSmokeScriptPath -Raw

foreach ($requiredToken in @(
        '"smoke-adobe-host"',
        'smoke_adobe_after_effects_host_win.ps1',
        'Assert-CorridorKeyAdobeHostSmokeHealthy'
    )) {
    if ($windowsWrapper -notmatch [regex]::Escape($requiredToken)) {
        throw "scripts/windows.ps1 must expose Adobe host smoke through '$requiredToken'."
    }
}

foreach ($requiredToken in @(
        'AfterFX.com',
        'aerender.exe',
        'Plugin Loading.log',
        'PF_Interrupt_CANCEL',
        'Alpha Hint Layer',
        'Output Mode',
        'CorridorKey_E2E',
        'corridorkey_adobe_green.aex',
        'corridorkey_adobe_blue.aex',
        'UseInstalledPayload',
        'AllowSystemPluginPath',
        'Start-Process',
        'WindowStyle Hidden',
        'app.project.renderQueue.items.add',
        'output_luma_min',
        'output_luma_max',
        'validation_passed'
    )) {
    if ($hostSmokeScript -notmatch [regex]::Escape($requiredToken)) {
        throw "scripts/smoke_adobe_after_effects_host_win.ps1 must provide '$requiredToken'."
    }
}

foreach ($forbiddenToken in @(
        'Remove-Item -LiteralPath $commonPluginRoot -Recurse -Force',
        'Remove-Item -Path $commonPluginRoot -Recurse -Force',
        'Stop-Process -Name AfterFX'
    )) {
    if ($hostSmokeScript -match [regex]::Escape($forbiddenToken)) {
        throw "Adobe host smoke must not use broad/destructive host operations: $forbiddenToken"
    }
}

Write-Host "[PASS] Adobe host smoke scaffold checks passed." -ForegroundColor Green
