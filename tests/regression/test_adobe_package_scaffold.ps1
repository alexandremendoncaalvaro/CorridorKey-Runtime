Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$windowsWrapperPath = Join-Path $repoRoot "scripts\windows.ps1"
$packageScriptPath = Join-Path $repoRoot "scripts\package_adobe_plugins_windows.ps1"
$validateScriptPath = Join-Path $repoRoot "scripts\validate_adobe_package_win.ps1"
$installSmokeScriptPath = Join-Path $repoRoot "scripts\smoke_adobe_install_win.ps1"
$installerBuilderPath = Join-Path $repoRoot "scripts\installer\build_installer.ps1"
$installerTemplatePath = Join-Path $repoRoot "scripts\installer\corridorkey.iss.template"
$agentsPath = Join-Path $repoRoot "AGENTS.md"
$claudePath = Join-Path $repoRoot "CLAUDE.md"

$windowsWrapper = Get-Content -Path $windowsWrapperPath -Raw
$agentsDoc = Get-Content -Path $agentsPath -Raw
$claudeDoc = Get-Content -Path $claudePath -Raw

if ($windowsWrapper -notmatch '"package-adobe"') {
    throw "scripts/windows.ps1 must expose package-adobe as the canonical Adobe packaging task."
}
if ($windowsWrapper -notmatch [regex]::Escape("package_adobe_plugins_windows.ps1")) {
    throw "scripts/windows.ps1 must delegate package-adobe through scripts/package_adobe_plugins_windows.ps1."
}
if ($windowsWrapper -notmatch "Assert-CorridorKeyAdobePackageValidationHealthy") {
    throw "scripts/windows.ps1 must assert the generated Adobe package validation report."
}
if ($agentsDoc -notmatch "package-adobe" -or $claudeDoc -notmatch "package-adobe") {
    throw "AGENTS.md and CLAUDE.md must list package-adobe as a canonical Windows task."
}

foreach ($scriptPath in @($packageScriptPath, $validateScriptPath, $installSmokeScriptPath)) {
    if (-not (Test-Path $scriptPath)) {
        throw "Expected Adobe package script not found: $scriptPath"
    }
}

$packageScript = Get-Content -Path $packageScriptPath -Raw
$validateScript = Get-Content -Path $validateScriptPath -Raw
$installerBuilder = Get-Content -Path $installerBuilderPath -Raw
$installerTemplate = Get-Content -Path $installerTemplatePath -Raw

foreach ($requiredToken in @(
    "Adobe\Common\Plug-ins\7.0\MediaCore\CorridorKey",
    "Contents\Win64",
    "Contents\Resources\models",
    "corridorkey_adobe_green.aex",
    "corridorkey_adobe_blue.aex",
    "corridorkey_host_plugin_runtime_server.exe",
    "corridorkey.exe",
    "model_inventory.json",
    "adobe_package_inventory.json",
    "validate_adobe_package_win.ps1",
    "smoke_adobe_install_win.ps1",
    "adobe_install_smoke_clean.json",
    "adobe_install_smoke_upgrade.json",
    "_Setup",
    "[ValidateSet(""online"", ""offline"")]",
    '$Flavor',
    "installer\build_installer.ps1",
    "stage_offline_payload.ps1",
    "-InstallerSurface",
    "adobe",
    "-OutputBaseFilename",
    "-ModelPayloadDir",
    "function Assert-SafeAdobePackageOutputPath",
    "function Assert-SafeAdobeInstallSmokeRoot",
    '$OutputDir = Assert-SafeAdobePackageOutputPath',
    '[System.IO.Path]::GetFullPath($Path)',
    '[switch]$RequireRtxProviders',
    'Required RTX ONNX Runtime TensorRT provider DLL not found',
    '-RequireRtxProviders:($ModelProfile -eq "windows-rtx")'
)) {
    if ($packageScript -notmatch [regex]::Escape($requiredToken)) {
        throw "scripts/package_adobe_plugins_windows.ps1 must stage or emit '$requiredToken'."
    }
}

if ($packageScript -match "Adobe Media Encoder") {
    throw "Adobe package README must not claim Adobe Media Encoder support without validation coverage."
}

foreach ($forbiddenToken in @(
    "function Write-AdobeInnoInstaller",
    "function Resolve-InnoCompiler"
)) {
    if ($packageScript -match [regex]::Escape($forbiddenToken)) {
        throw "scripts/package_adobe_plugins_windows.ps1 must use the shared modern installer builder instead of '$forbiddenToken'."
    }
}

foreach ($requiredToken in @(
    '$arguments += @("-Flavor", $Flavor)',
    "package_adobe_plugins_windows.ps1"
)) {
    if ($windowsWrapper -notmatch [regex]::Escape($requiredToken)) {
        throw "scripts/windows.ps1 must forward Adobe installer flavor through '$requiredToken'."
    }
}

foreach ($requiredToken in @(
    '[ValidateSet("ofx", "adobe")]',
    '$InstallerSurface',
    '$appVersionField = if ($InstallerSurface -eq "adobe") { $DisplayVersionLabel } else { $Version }',
    "@@APP_ID@@",
    "@@APP_NAME@@",
    "@@APP_VERSION@@",
    "@@DEFAULT_DIR_NAME@@",
    "@@INSTALLER_SURFACE@@",
    "@@PLUGIN_BASE_DESCRIPTION@@",
    "@@HOST_STOP_PROCESS_LINES@@",
    "@@HOST_CACHE_CLEAR_LINES@@"
)) {
    if ($installerBuilder -notmatch [regex]::Escape($requiredToken) -and
        $installerTemplate -notmatch [regex]::Escape($requiredToken)) {
        throw "shared Inno installer must expose Adobe surface token '$requiredToken'."
    }
}

foreach ($requiredToken in @(
    "onnxruntime_providers_nv_tensorrt_rtx.dll",
    "onnxruntime_providers_nvtensorrtrtx.dll",
    "tensorrt_onnxparser_rtx_*.dll",
    "tensorrt_rtx_*.dll",
    "cudart64_*.dll",
    "nppc64_12.dll",
    "nppial64_12.dll",
    "nppidei64_12.dll",
    "nppif64_12.dll",
    "nppig64_12.dll",
    "function Copy-VisualCppRedistributableDependencies",
    '$expectedVcRedistNames',
    'CorridorKey.ofx.bundle\Contents\Win64',
    'Remove-Item -LiteralPath $OutputDir -Recurse -Force',
    'Remove-Item -LiteralPath $installerPath -Force',
    'Remove-Item -LiteralPath $installSmokeRoot -Recurse -Force',
    "function Assert-SafeAdobeInstallDestination",
    'Remove-Item -LiteralPath $destination -Recurse -Force',
    'Copy-Item -LiteralPath $source -Destination $destination -Recurse -Force',
    "The payload is staged for Adobe's shared Common Plug-ins MediaCore location."
)) {
    if ($packageScript -notmatch [regex]::Escape($requiredToken)) {
        throw "scripts/package_adobe_plugins_windows.ps1 must require safe cleanup/runtime sidecar token '$requiredToken'."
    }
}

foreach ($forbiddenToken in @(
    'Remove-Item $OutputDir -Recurse -Force',
    'Remove-Item $installerPath -Force',
    'Remove-Item -Path $destination -Recurse -Force',
    'Get-ChildItem -Path $runtimeServerDir -Filter "*.dll"',
    "can be discovered by After Effects and Premiere Pro"
)) {
    if ($packageScript -match [regex]::Escape($forbiddenToken)) {
        throw "scripts/package_adobe_plugins_windows.ps1 must use LiteralPath cleanup instead of '$forbiddenToken'."
    }
}

foreach ($requiredToken in @(
    'WizardImageFile={#InstallerWizardImage}',
    'SetupIconFile={#InstallerIcon}',
    'AppVersion={#MyAppVersion}',
    'VersionInfoTextVersion={#MyDisplayLabel}',
    'CreateDownloadPage(',
    'Name: "greenonly"; Description: "Green only"',
    'Name: "blueonly"; Description: "Blue only"',
    'Types: greenonly blueonly recommended custom; Flags: fixed',
    'Types: greenonly recommended custom',
    'Types: blueonly recommended custom',
    'Excludes: "corridorkey_adobe_green.aex,corridorkey_adobe_blue.aex"',
    'Components: green',
    'Components: blue',
    'corridorkey_adobe.aex',
    'CorridorKeyGreenNotSelected',
    'CorridorKeyBlueNotSelected'
)) {
    if ($installerTemplate -notmatch [regex]::Escape($requiredToken)) {
        throw "scripts/installer/corridorkey.iss.template must provide '$requiredToken'."
    }
}

foreach ($requiredToken in @(
    "adobe_package_validation.json",
    "CommonPluginInstallPath",
    "corridorkey_adobe_green.aex",
    "corridorkey_adobe_blue.aex",
    "com.corridorkey.effect.blue",
    "windows_adobe",
    "PF_OutFlag2_SUPPORTS_SMART_RENDER",
    "doctor_report.json",
    'corridorkey.exe" --version',
    "Packaged CLI display label mismatch",
    "cli_display_version",
    "Packaged runtime doctor failed before producing a JSON report.",
    "Adobe base payload must not bundle Blue runtime DLL",
    "blue-runtime pack",
    "Test-CorridorKeyDoctorMissingModelProbeFailuresOnly",
    "Test-CorridorKeyDoctorMissingModelBundleFailuresOnly",
    "Packaged runtime doctor reported failures only for missing model(s).",
    'if ($doctorSucceeded -and ((-not $doctorHealthy) -or (-not $doctorBundleHealthy)))'
)) {
    if ($validateScript -notmatch [regex]::Escape($requiredToken)) {
        throw "scripts/validate_adobe_package_win.ps1 must validate or report '$requiredToken'."
    }
}

if ($validateScript -match [regex]::Escape('} elseif ($missingModels.Count -gt 0) {')) {
    throw "scripts/validate_adobe_package_win.ps1 must not tolerate a total doctor command failure solely because models are missing."
}
if ($validateScript -match [regex]::Escape('if (-not $doctorBundleHealthy) {
            throw "Packaged runtime doctor reported an unhealthy Adobe bundle layout."
        }')) {
    throw "scripts/validate_adobe_package_win.ps1 must evaluate unhealthy Adobe bundle reports through the missing-model-only tolerance path."
}

Write-Host "[PASS] Adobe package scaffold checks passed." -ForegroundColor Green
