param(
    [string]$PackagePath = "",
    [string]$ExpectedDisplayVersionLabel = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot "windows_runtime_helpers.ps1")

if ([string]::IsNullOrWhiteSpace($PackagePath)) {
    $PackagePath = Join-Path $repoRoot "dist\CorridorKey_Adobe"
}

function Test-JsonProperty {
    param([object]$Object, [string]$Name)
    return $null -ne $Object -and $Object.PSObject.Properties.Match($Name).Count -gt 0
}

function Get-AdobeCommonPluginInstallPath {
    foreach ($registryRoot in @("HKLM:\SOFTWARE\Adobe\After Effects", "HKLM:\SOFTWARE\WOW6432Node\Adobe\After Effects")) {
        if (-not (Test-Path $registryRoot)) {
            continue
        }

        $keys = @(Get-ChildItem -Path $registryRoot -ErrorAction SilentlyContinue)
        foreach ($key in ($keys | Sort-Object -Property PSChildName -Descending)) {
            try {
                $value = $key.GetValue("CommonPluginInstallPath", $null, "DoNotExpandEnvironmentNames")
                if ($null -ne $value -and -not [string]::IsNullOrWhiteSpace([string]$value)) {
                    return ([string]$value).Trim()
                }
            } catch {
            }
        }
    }

    return Join-Path $env:ProgramFiles "Adobe\Common\Plug-ins\7.0\MediaCore"
}

function Assert-PathExists {
    param([string]$Path, [string]$Message)
    if (-not (Test-Path $Path)) {
        throw $Message
    }
}

$packageRoot = [System.IO.Path]::GetFullPath($PackagePath)
$payloadDir = Join-Path $packageRoot "Adobe\Common\Plug-ins\7.0\MediaCore\CorridorKey"
$win64Dir = Join-Path $payloadDir "Contents\Win64"
$modelsDir = Join-Path $payloadDir "Contents\Resources\models"
$greenPluginBinary = Join-Path $win64Dir "corridorkey_adobe_green.aex"
$bluePluginBinary = Join-Path $win64Dir "corridorkey_adobe_blue.aex"
$cliBinary = Join-Path $win64Dir "corridorkey.exe"
$runtimeServer = Join-Path $win64Dir "corridorkey_host_plugin_runtime_server.exe"
$modelInventoryPath = Join-Path $payloadDir "model_inventory.json"
$packageInventoryPath = Join-Path $packageRoot "adobe_package_inventory.json"
$validationPath = Join-Path $packageRoot "adobe_package_validation.json"
$doctorReportPath = Join-Path $packageRoot "doctor_report.json"
$commonPluginInstallPath = Get-AdobeCommonPluginInstallPath

Write-Host "Validating Adobe package: $packageRoot" -ForegroundColor Cyan
Assert-PathExists -Path $payloadDir -Message "Missing Adobe MediaCore payload directory."
Assert-PathExists -Path $win64Dir -Message "Missing Adobe Contents\Win64 payload directory."
Assert-PathExists -Path $greenPluginBinary -Message "Missing corridorkey_adobe_green.aex."
Assert-PathExists -Path $bluePluginBinary -Message "Missing corridorkey_adobe_blue.aex."
Assert-PathExists -Path $cliBinary -Message "Missing corridorkey.exe diagnostic binary."
Assert-PathExists -Path $runtimeServer -Message "Missing corridorkey_host_plugin_runtime_server.exe."
Assert-PathExists -Path $modelsDir -Message "Missing models directory."
Assert-PathExists -Path $modelInventoryPath -Message "Missing model_inventory.json."
Assert-PathExists -Path $packageInventoryPath -Message "Missing adobe_package_inventory.json."

foreach ($requiredDll in @("onnxruntime.dll", "onnxruntime_providers_shared.dll")) {
    Assert-PathExists -Path (Join-Path $win64Dir $requiredDll) -Message "Missing required runtime DLL: $requiredDll."
}

foreach ($requiredRuntime in @("VCRUNTIME140.dll", "VCRUNTIME140_1.dll", "MSVCP140.dll")) {
    Assert-PathExists -Path (Join-Path $win64Dir $requiredRuntime) -Message "Missing app-local Visual C++ runtime DLL: $requiredRuntime."
}

foreach ($forbiddenBaseRuntime in @("torch_cuda.dll", "torch_cpu.dll", "torchtrt.dll", "c10.dll", "c10_cuda.dll")) {
    if (Test-Path -LiteralPath (Join-Path $win64Dir $forbiddenBaseRuntime)) {
        throw "Adobe base payload must not bundle Blue runtime DLL '$forbiddenBaseRuntime'; it belongs in the downloadable blue-runtime pack."
    }
}

$packageInventory = Get-Content -Path $packageInventoryPath -Raw | ConvertFrom-Json
if (-not (Test-JsonProperty -Object $packageInventory -Name "effects") -or $null -eq $packageInventory.effects) {
    throw "Adobe package inventory is missing effect identities."
}
$packageEffects = @($packageInventory.effects)
if ($packageEffects.Count -ne 2) {
    throw "Adobe package inventory must report the Green and Blue effects."
}
foreach ($expectedMatchName in @("com.corridorkey.effect", "com.corridorkey.effect.blue")) {
    $matchingEffects = @($packageEffects | Where-Object { [string]$_.match_name -eq $expectedMatchName })
    if ($matchingEffects.Count -eq 0) {
        throw "Adobe package inventory is missing effect match name '$expectedMatchName'."
    }
    $effect = $matchingEffects[0]
    $piplCapabilities = @($effect.pipl_capabilities)
    if ($piplCapabilities -notcontains "PF_OutFlag2_SUPPORTS_SMART_RENDER") {
        throw "Adobe package inventory must report PF_OutFlag2_SUPPORTS_SMART_RENDER for '$expectedMatchName'."
    }
}

$modelInventory = Get-Content -Path $modelInventoryPath -Raw | ConvertFrom-Json
$missingModels = @($modelInventory.missing_models)
$presentModels = @($modelInventory.present_models)
$expectedModels = @($modelInventory.expected_models)

foreach ($model in $presentModels) {
    Assert-PathExists -Path (Join-Path $modelsDir $model) -Message "Inventory lists missing staged model: $model."
}

$supportedBackends = @()
$runtimeInfoSucceeded = $false
$cliDisplayVersion = ""
$envPathOld = $env:PATH
Push-Location $win64Dir
try {
    $env:PATH = "$win64Dir;$envPathOld"
    $versionLines = & ".\corridorkey.exe" --version 2>&1
    $versionExitCode = $LASTEXITCODE
    $cliVersionOutput = ($versionLines | Out-String).Trim()
    if ($versionExitCode -ne 0 -or [string]::IsNullOrWhiteSpace($cliVersionOutput)) {
        throw "Packaged CLI version probe failed."
    }

    $versionMatch = [regex]::Match($cliVersionOutput, '^CorridorKey Runtime v(?<label>\S+)$')
    if (-not $versionMatch.Success) {
        throw "Packaged CLI version output has unexpected format: $cliVersionOutput"
    }

    $cliDisplayVersion = $versionMatch.Groups["label"].Value
    if (-not [string]::IsNullOrWhiteSpace($ExpectedDisplayVersionLabel) -and
        $cliDisplayVersion -ne $ExpectedDisplayVersionLabel) {
        Write-Host "[FAIL] Packaged CLI display label mismatch. Expected $ExpectedDisplayVersionLabel, got $cliDisplayVersion" -ForegroundColor Red
        throw "Packaged CLI display label mismatch. Rebuild with the same -DisplayVersionLabel before packaging."
    }

    Write-Host "[PASS] Packaged CLI display label: $cliDisplayVersion" -ForegroundColor Green

    $runtimeInfoJson = & ".\corridorkey.exe" info --json 2>$null
    if ($LASTEXITCODE -eq 0 -and -not [string]::IsNullOrWhiteSpace($runtimeInfoJson)) {
        $runtimeInfo = $runtimeInfoJson | ConvertFrom-Json
        if ($null -ne $runtimeInfo.capabilities -and $null -ne $runtimeInfo.capabilities.supported_backends) {
            $supportedBackends = @($runtimeInfo.capabilities.supported_backends)
            $runtimeInfoSucceeded = $true
        }
    }
} finally {
    $env:PATH = $envPathOld
    Pop-Location
}

if (-not $runtimeInfoSucceeded) {
    throw "Packaged Adobe runtime probe failed."
}

$previousModelsDir = if (Test-Path Env:CORRIDORKEY_MODELS_DIR) { $env:CORRIDORKEY_MODELS_DIR } else { $null }
$doctorEnvPathOld = $env:PATH
$doctorSucceeded = $false
$doctorHealthy = $false
$doctorFailureTolerated = $false
$doctorFailureReason = ""
$doctor = $null
$doctorLayoutKind = ""
$doctorBundleHealthy = $false
Push-Location $win64Dir
try {
    $env:PATH = "$win64Dir;$env:PATH"
    $env:CORRIDORKEY_MODELS_DIR = $modelsDir
    $doctorJson = & ".\corridorkey.exe" doctor --json 2>$null
    if ($LASTEXITCODE -eq 0 -and -not [string]::IsNullOrWhiteSpace($doctorJson)) {
        $doctorSucceeded = $true
        $doctorJson | Set-Content -Path $doctorReportPath -Encoding UTF8
        $doctor = $doctorJson | ConvertFrom-Json
        if (-not (Test-JsonProperty -Object $doctor -Name "summary") -or $null -eq $doctor.summary) {
            throw "Packaged runtime doctor report is missing summary."
        }
        if (-not (Test-JsonProperty -Object $doctor -Name "bundle") -or $null -eq $doctor.bundle) {
            throw "Packaged runtime doctor report is missing bundle diagnostics."
        }
        $doctorLayoutKind = [string]$doctor.bundle.layout_kind
        $doctorBundleHealthy = [bool]$doctor.bundle.healthy
        if ($doctorLayoutKind -ne "windows_adobe") {
            throw "Packaged runtime doctor must identify the payload as windows_adobe; found '$doctorLayoutKind'."
        }
        $doctorHealthy = [bool]$doctor.summary.healthy
    } else {
        throw "Packaged runtime doctor failed before producing a JSON report."
    }
} finally {
    if ($null -ne $previousModelsDir) {
        $env:CORRIDORKEY_MODELS_DIR = $previousModelsDir
    } else {
        Remove-Item Env:CORRIDORKEY_MODELS_DIR -ErrorAction SilentlyContinue
    }
    $env:PATH = $doctorEnvPathOld
    Pop-Location
}

if ($doctorSucceeded -and ((-not $doctorHealthy) -or (-not $doctorBundleHealthy))) {
    $missingModelOnlyFailures =
        (Test-CorridorKeyDoctorMissingModelProbeFailuresOnly -Doctor $doctor -MissingModels $missingModels) -or
        (Test-CorridorKeyDoctorMissingModelBundleFailuresOnly -Doctor $doctor -MissingModels $missingModels)
    if ($missingModelOnlyFailures) {
        $doctorFailureTolerated = $true
        $doctorFailureReason = "Packaged runtime doctor reported failures only for missing model(s)."
    } elseif (-not $doctorBundleHealthy) {
        throw "Packaged runtime doctor reported an unhealthy Adobe bundle layout. See $doctorReportPath."
    } else {
        throw "Packaged runtime doctor reported unhealthy status. See $doctorReportPath."
    }
}

$greenEffectDisplayName = "CorridorKey Green"
$blueEffectDisplayName = "CorridorKey Blue"
if (-not [string]::IsNullOrWhiteSpace($ExpectedDisplayVersionLabel)) {
    $greenEffectDisplayName = "CorridorKey Green v$ExpectedDisplayVersionLabel"
    $blueEffectDisplayName = "CorridorKey Blue v$ExpectedDisplayVersionLabel"
}

$validationPayload = [ordered]@{
    package_path = $packageRoot
    validation_passed = $true
    install = [ordered]@{
        scope = "adobe_common_mediacore"
        CommonPluginInstallPath = $commonPluginInstallPath
        payload_relative_path = "Adobe\Common\Plug-ins\7.0\MediaCore\CorridorKey"
        target_path = Join-Path $commonPluginInstallPath "CorridorKey"
    }
    effects = @(
        [ordered]@{
            plugin_binary = $greenPluginBinary
            name = $greenEffectDisplayName
            match_name = "com.corridorkey.effect"
            category = "CorridorKey"
            component = "green"
            pipl_capabilities = @("PF_OutFlag2_SUPPORTS_SMART_RENDER", "PF_OutFlag2_FLOAT_COLOR_AWARE")
        },
        [ordered]@{
            plugin_binary = $bluePluginBinary
            name = $blueEffectDisplayName
            match_name = "com.corridorkey.effect.blue"
            category = "CorridorKey"
            component = "blue"
            pipl_capabilities = @("PF_OutFlag2_SUPPORTS_SMART_RENDER", "PF_OutFlag2_FLOAT_COLOR_AWARE")
        }
    )
    runtime = [ordered]@{
        cli_binary = $cliBinary
        host_plugin_runtime_server = $runtimeServer
        supported_backends = @($supportedBackends)
        layout = "Contents\Win64"
        expected_display_version = $ExpectedDisplayVersionLabel
        cli_display_version = $cliDisplayVersion
    }
    models = [ordered]@{
        expected_models = @($expectedModels)
        present_models = @($presentModels)
        missing_models = @($missingModels)
        present_count = @($presentModels).Count
        missing_count = @($missingModels).Count
    }
    doctor = [ordered]@{
        attempted = $true
        succeeded = $doctorSucceeded
        healthy = $doctorHealthy
        failure_tolerated = $doctorFailureTolerated
        failure_reason = $doctorFailureReason
        report_path = if ($doctorSucceeded) { $doctorReportPath } else { "" }
        layout_kind = $doctorLayoutKind
        bundle_healthy = $doctorBundleHealthy
    }
}

Write-CorridorKeyJsonFile -Path $validationPath -Payload $validationPayload
Write-Host "[PASS] Wrote Adobe package validation report: $validationPath" -ForegroundColor Green
if ($missingModels.Count -gt 0) {
    Write-Host "Adobe package is ready with partial model coverage. Missing models are listed in adobe_package_validation.json and model_inventory.json." -ForegroundColor Cyan
} else {
    Write-Host "Adobe package is ready for installation." -ForegroundColor Green
}
