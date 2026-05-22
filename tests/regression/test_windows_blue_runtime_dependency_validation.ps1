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
$loader = Get-Content -Path (Join-Path $repoRoot "src\core\torch_trt_loader.cpp") -Raw
$ofxCmake = Get-Content -Path (Join-Path $repoRoot "src\plugins\ofx\CMakeLists.txt") -Raw
$testsCmake = Get-Content -Path (Join-Path $repoRoot "tests\CMakeLists.txt") -Raw
$validator = Get-Content -Path (Join-Path $repoRoot "scripts\validate_ofx_win.ps1") -Raw

foreach ($nppDll in @("nppc64_12.dll", "nppif64_12.dll", "nppig64_12.dll")) {
    Assert-Contains `
        -Content $packageScript `
        -Needle $nppDll `
        -Message "scripts/package_ofx.ps1 must stage $nppDll for CUDA/NPP imports used by the Blue TorchTRT wrapper."
}

foreach ($nppTarget in @("nppc64_12", "nppif64_12", "nppig64_12")) {
    Assert-Contains `
        -Content $ofxCmake `
        -Needle $nppTarget `
        -Message "src/plugins/ofx/CMakeLists.txt must stage $nppTarget.dll into the build bundle."
}

foreach ($nppTarget in @("nppc64_12", "nppif64_12", "nppig64_12")) {
    Assert-Contains `
        -Content $testsCmake `
        -Needle $nppTarget `
        -Message "tests/CMakeLists.txt must stage $nppTarget.dll beside Windows test binaries."
}

Assert-Contains `
    -Content $validator `
    -Needle '$torchTrtWrapperPath' `
    -Message "scripts/validate_ofx_win.ps1 must scan corridorkey_torchtrt.dll imports."

Assert-Contains `
    -Content $validator `
    -Needle 'additional_dirs = @($win64Dir)' `
    -Message "scripts/validate_ofx_win.ps1 must model the Blue wrapper search path that includes Contents/Win64."

Assert-Contains `
    -Content $loader `
    -Needle "resolve_ofx_pack_win64_dir" `
    -Message "src/core/torch_trt_loader.cpp must derive Contents/Win64 from the OFX Blue runtime layout."

Assert-Contains `
    -Content $loader `
    -Needle "AddDllDirectory(pack_win64_dir" `
    -Message "src/core/torch_trt_loader.cpp must add Contents/Win64 before loading corridorkey_torchtrt.dll."

Write-Host "[PASS] Windows Blue runtime dependency validation regression checks passed." -ForegroundColor Green
