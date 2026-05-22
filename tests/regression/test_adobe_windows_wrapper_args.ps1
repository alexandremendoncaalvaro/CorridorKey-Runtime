Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$windowsWrapper = Get-Content -Path (Join-Path $repoRoot "scripts\windows.ps1") -Raw
$buildScript = Get-Content -Path (Join-Path $repoRoot "scripts\build.ps1") -Raw

foreach ($scriptCase in @(
    @{ Name = "scripts/windows.ps1"; Text = $windowsWrapper },
    @{ Name = "scripts/build.ps1"; Text = $buildScript }
)) {
    foreach ($requiredParameter in @("EnableAdobePlugin", "AdobeSdkRoot")) {
        if ($scriptCase.Text -notmatch "\`$$requiredParameter\b") {
            throw "$($scriptCase.Name) must define or use $requiredParameter."
        }
    }
}

foreach ($requiredForward in @("-EnableAdobePlugin", "-AdobeSdkRoot")) {
    if ($windowsWrapper -notmatch [regex]::Escape($requiredForward)) {
        throw "scripts/windows.ps1 must forward $requiredForward to scripts/build.ps1."
    }
}

if ($windowsWrapper -notmatch '\$Task\s+-ne\s+"build"') {
    throw "scripts/windows.ps1 must reject Adobe arguments for non-build tasks."
}
if ($windowsWrapper -notmatch [regex]::Escape("-EnableAdobePlugin and -AdobeSdkRoot are supported only with -Task build.")) {
    throw "scripts/windows.ps1 must explain that Adobe arguments are build-only."
}

foreach ($requiredDefine in @(
    "CORRIDORKEY_ENABLE_ADOBE_PLUGIN=ON",
    "CORRIDORKEY_ENABLE_ADOBE_PLUGIN=OFF",
    "CORRIDORKEY_ADOBE_SDK_ROOT="
)) {
    if ($buildScript -notmatch [regex]::Escape($requiredDefine)) {
        throw "scripts/build.ps1 must pass $requiredDefine to CMake."
    }
}

Write-Host "[PASS] Adobe Windows wrapper argument checks passed." -ForegroundColor Green
