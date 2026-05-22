Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)

function Assert-Contains {
    param(
        [string]$Content,
        [string]$Needle,
        [string]$Message
    )
    if (-not $Content.Contains($Needle)) {
        throw $Message
    }
}

$packageScript = Get-Content -Path (Join-Path $repoRoot "scripts\package_ofx.ps1") -Raw
$ofxCmake = Get-Content -Path (Join-Path $repoRoot "src\plugins\ofx\CMakeLists.txt") -Raw
$testsCmake = Get-Content -Path (Join-Path $repoRoot "tests\CMakeLists.txt") -Raw
$validator = Get-Content -Path (Join-Path $repoRoot "scripts\validate_ofx_win.ps1") -Raw

Assert-Contains `
    -Content $packageScript `
    -Needle "nppif64_12.dll" `
    -Message "scripts/package_ofx.ps1 must stage nppif64_12.dll for the Blue TorchTRT wrapper."

Assert-Contains `
    -Content $ofxCmake `
    -Needle "nppif64_12" `
    -Message "src/plugins/ofx/CMakeLists.txt must stage nppif64_12.dll into the build bundle."

Assert-Contains `
    -Content $testsCmake `
    -Needle "nppif64_12" `
    -Message "tests/CMakeLists.txt must stage nppif64_12.dll beside Windows test binaries."

Assert-Contains `
    -Content $validator `
    -Needle '$torchTrtWrapperPath' `
    -Message "scripts/validate_ofx_win.ps1 must scan corridorkey_torchtrt.dll imports."

Assert-Contains `
    -Content $validator `
    -Needle "AllowedExternalDlls" `
    -Message "scripts/validate_ofx_win.ps1 must distinguish Blue runtime pack imports from missing base-bundle imports."

Write-Host "[PASS] Windows Blue runtime dependency validation regression checks passed." -ForegroundColor Green
