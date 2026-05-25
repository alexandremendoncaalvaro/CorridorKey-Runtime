Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$fixtureScriptPath = Join-Path $repoRoot "scripts\compare_adobe_jordan_fixture_win.ps1"

if (-not (Test-Path -LiteralPath $fixtureScriptPath)) {
    throw "Expected Adobe Jordan fixture script not found: $fixtureScriptPath"
}

$fixtureScript = Get-Content -LiteralPath $fixtureScriptPath -Raw

foreach ($requiredToken in @(
        'Jordan4k.mp4',
        'Jordan4k_alphahint.mp4',
        'FusionCompositionPath',
        'ResolveReferencePath',
        'RequireResolveReference',
        'Get-FusionCompositionSettings',
        'AfterFX.com',
        'aerender.exe',
        'Alpha Hint Layer',
        'Input Color Space',
        'Output Mode',
        'Quality',
        'Quality Fallback',
        'Coarse Resolution Override',
        'Upscale Method',
        'actual_resized_to_reference',
        'lanczos4',
        'bilinear',
        'adobe_bilinear_vs_lanczos4',
        'adobe_lanczos4_vs_resolve',
        'adobe_bilinear_vs_resolve',
        'dark_pixel_fraction',
        'large_delta_fraction',
        'scripting_api_available',
        'WindowStyle Hidden',
        'ConvertTo-Json'
    )) {
    if ($fixtureScript -notmatch [regex]::Escape($requiredToken)) {
        throw "scripts/compare_adobe_jordan_fixture_win.ps1 must provide '$requiredToken'."
    }
}

foreach ($forbiddenToken in @(
        'Stop-Process -Name Resolve',
        'Stop-Process -Name AfterFX',
        'Remove-Item -LiteralPath $env:PROGRAMDATA',
        'Remove-Item -LiteralPath $env:APPDATA'
    )) {
    if ($fixtureScript -match [regex]::Escape($forbiddenToken)) {
        throw "Adobe Jordan fixture script must not use broad/destructive host operations: $forbiddenToken"
    }
}

$tokens = $null
$errors = $null
[System.Management.Automation.Language.Parser]::ParseInput($fixtureScript, [ref]$tokens, [ref]$errors) | Out-Null
if ($errors.Count -gt 0) {
    $summary = ($errors | ForEach-Object { $_.Message }) -join "; "
    throw "Adobe Jordan fixture script has parse errors: $summary"
}

Write-Host "[PASS] Adobe Jordan fixture scaffold checks passed." -ForegroundColor Green
