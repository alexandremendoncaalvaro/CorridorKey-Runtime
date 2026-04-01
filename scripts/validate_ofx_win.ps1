param(
    [string]$BundlePath = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot "windows_runtime_helpers.ps1")
if ([string]::IsNullOrWhiteSpace($BundlePath)) {
    $BundlePath = Join-Path $repoRoot "dist\CorridorKey.ofx.bundle"
}

function Test-CorridorKeyJsonProperty {
    param(
        [object]$Object,
        [string]$Name
    )

    if ($null -eq $Object) {
        return $false
    }

    return $Object.PSObject.Properties.Match($Name).Count -gt 0
}

function Get-CorridorKeyInventoryContractIssues {
    param(
        [object]$Inventory,
        [object]$ExpectedContract
    )

    $issues = @()

    $stringFields = @(
        "package_type",
        "model_profile",
        "bundle_track",
        "release_label",
        "optimization_profile_id",
        "optimization_profile_label",
        "backend_intent",
        "fallback_policy",
        "warmup_policy",
        "certification_tier"
    )

    foreach ($field in $stringFields) {
        if (-not (Test-CorridorKeyJsonProperty -Object $Inventory -Name $field)) {
            $issues += "Missing inventory field '$field'."
            continue
        }

        $actualValue = [string]$Inventory.$field
        $expectedValue = [string]$ExpectedContract.$field
        if ($actualValue -ne $expectedValue) {
            $issues += "Inventory field '$field' expected '$expectedValue' but found '$actualValue'."
        }
    }

    foreach ($field in @("expected_compiled_context_models", "compiled_context_models", "missing_compiled_context_models")) {
        if (-not (Test-CorridorKeyJsonProperty -Object $Inventory -Name $field)) {
            $issues += "Missing inventory field '$field'."
            continue
        }

        if ($null -eq $Inventory.$field -or $Inventory.$field -isnot [System.Array]) {
            $issues += "Inventory field '$field' must be an array."
        }
    }

    foreach ($field in @("compiled_context_complete", "unrestricted_quality_attempt")) {
        if (-not (Test-CorridorKeyJsonProperty -Object $Inventory -Name $field)) {
            $issues += "Missing inventory field '$field'."
        }
    }

    if ($ExpectedContract.expects_compiled_context_models) {
        $compiledContextComplete = $false
        if (Test-CorridorKeyJsonProperty -Object $Inventory -Name "compiled_context_complete") {
            $compiledContextComplete = [bool]$Inventory.compiled_context_complete
        }
        if (-not $compiledContextComplete) {
            $issues += "Inventory requires precompiled TensorRT context models, but the packaged set is incomplete."
        }
    }

    return @($issues)
}

Write-Host "Validating OFX bundle: $BundlePath" -ForegroundColor Cyan
Write-Host ""

$bundleDescriptor = [System.IO.Path]::GetFullPath($BundlePath)
$bundleRoot = Split-Path -Parent $bundleDescriptor
$win64Dir = Join-Path $bundleDescriptor "Contents\Win64"
$resourcesDir = Join-Path $bundleDescriptor "Contents\Resources\models"
$modelInventoryPath = Join-Path $bundleDescriptor "model_inventory.json"
$artifactManifestPath = Join-Path $bundleDescriptor "artifact_manifest.json"
$bundleValidationPath = Join-Path $bundleRoot "bundle_validation.json"
$defaultModelProfile = Get-CorridorKeyOfxModelProfileFromReleaseSuffix -ReleaseSuffix (Split-Path $bundleRoot -Leaf)
$expectsUniversalGpuPath = $bundleDescriptor -match 'Universal'
$expectsDirectMlPath = $bundleDescriptor -match 'DirectML'

# Check bundle structure
if (-not (Test-Path $BundlePath)) {
    throw "Bundle directory not found: $BundlePath"
}

if (-not (Test-Path $win64Dir)) {
    throw "Missing Contents\Win64 directory"
}

if (-not (Test-Path $resourcesDir)) {
    throw "Missing Contents\Resources\models directory"
}

Write-Host "[PASS] Bundle directory structure exists" -ForegroundColor Green

# CRITICAL: Check for correct ONNX Runtime DLL name
$onnxDll = Join-Path $win64Dir "onnxruntime.dll"
if (-not (Test-Path $onnxDll)) {
    Write-Host "[FAIL] onnxruntime.dll not found!" -ForegroundColor Red
    throw "ERROR: onnxruntime.dll not found in Win64 directory"
}

Write-Host "[PASS] onnxruntime.dll exists" -ForegroundColor Green

# Check all required DLLs
$requiredDlls = @(
    "onnxruntime.dll",
    "onnxruntime_providers_shared.dll"
)

foreach ($dll in $requiredDlls) {
    $path = Join-Path $win64Dir $dll
    if (-not (Test-Path $path)) {
        Write-Host "[FAIL] Missing required DLL: $dll" -ForegroundColor Red
        throw "Missing required DLL: $dll"
    }
    Write-Host "[PASS] Found $dll" -ForegroundColor Green
}

# Check plugin binary
$plugin = Join-Path $win64Dir "CorridorKey.ofx"
if (-not (Test-Path $plugin)) {
    Write-Host "[FAIL] Plugin binary not found" -ForegroundColor Red
    throw "Plugin binary not found: CorridorKey.ofx"
}

$pluginSize = (Get-Item $plugin).Length
Write-Host "[PASS] Found plugin binary ($([math]::Round($pluginSize / 1MB, 2)) MB)" -ForegroundColor Green

$cliBinary = Join-Path $win64Dir "corridorkey.exe"
if (-not (Test-Path $cliBinary)) {
    Write-Host "[FAIL] CLI binary not found" -ForegroundColor Red
    throw "CLI binary not found: corridorkey.exe"
}

$runtimeServer = Join-Path $win64Dir "corridorkey_ofx_runtime_server.exe"
if (-not (Test-Path $runtimeServer)) {
    Write-Host "[FAIL] Runtime server binary not found" -ForegroundColor Red
    throw "Runtime server binary not found: corridorkey_ofx_runtime_server.exe"
}

$directmlDll = Join-Path $win64Dir "DirectML.dll"
if (Test-Path $directmlDll) {
    Write-Host "[PASS] Found DirectML.dll" -ForegroundColor Green
} elseif ($expectsDirectMlPath) {
    Write-Host "[FAIL] DirectML.dll not found in DirectML bundle" -ForegroundColor Red
    throw "DirectML.dll is required for the DirectML bundle."
} else {
    Write-Host "[INFO] DirectML.dll not found (RTX bundle)" -ForegroundColor Cyan
}

$cliBinarySize = (Get-Item $cliBinary).Length
Write-Host "[PASS] Found CLI binary ($([math]::Round($cliBinarySize / 1MB, 2)) MB)" -ForegroundColor Green

$runtimeServerSize = (Get-Item $runtimeServer).Length
Write-Host "[PASS] Found runtime server binary ($([math]::Round($runtimeServerSize / 1MB, 2)) MB)" -ForegroundColor Green

$supportedBackends = @()
Push-Location $win64Dir
try {
    $runtimeInfoJson = & ".\corridorkey.exe" info --json 2>$null
    if ($LASTEXITCODE -eq 0 -and -not [string]::IsNullOrWhiteSpace($runtimeInfoJson)) {
        $runtimeInfo = $runtimeInfoJson | ConvertFrom-Json
        if ($null -ne $runtimeInfo.capabilities -and
            $null -ne $runtimeInfo.capabilities.supported_backends) {
            $supportedBackends = @($runtimeInfo.capabilities.supported_backends)
            Write-Host "[PASS] Runtime probe succeeded: $($supportedBackends -join ', ')" -ForegroundColor Green
        } else {
            Write-Host "[WARN] Runtime probe returned no supported_backends payload" -ForegroundColor Yellow
        }
    } else {
        Write-Host "[WARN] Runtime probe failed; falling back to DLL inspection" -ForegroundColor Yellow
    }
} catch {
    Write-Host "[WARN] Runtime probe failed; falling back to DLL inspection" -ForegroundColor Yellow
} finally {
    Pop-Location
}

# Check CUDA runtime (optional but should be present for NVIDIA systems)
$cudartFiles = @(Get-ChildItem -Path $win64Dir -Filter "cudart64_*.dll" -File -ErrorAction SilentlyContinue)
if ($cudartFiles.Count -eq 0) {
    if ($expectsDirectMlPath) {
        Write-Host "[INFO] No CUDA runtime DLL found (expected for DirectML bundle)" -ForegroundColor Cyan
    } else {
        Write-Host "[WARN] No CUDA runtime DLL found (cudart64_*.dll)" -ForegroundColor Yellow
    }
} else {
    foreach ($cudart in $cudartFiles) {
        Write-Host "[PASS] Found $($cudart.Name)" -ForegroundColor Green
    }
}

# Check TensorRT provider (optional)
$tensorrtProvider = @(Get-ChildItem -Path $win64Dir -Filter "onnxruntime_providers_*tensorrt*.dll" -File -ErrorAction SilentlyContinue)
if ($tensorrtProvider.Count -eq 0) {
    Write-Host "[INFO] No TensorRT provider found (DirectML will be used)" -ForegroundColor Cyan
} else {
    foreach ($provider in $tensorrtProvider) {
        Write-Host "[PASS] Found $($provider.Name)" -ForegroundColor Green
    }
}

# Check Windows universal GPU providers
$universalProviderDlls = @(
    "onnxruntime_providers_dml.dll",
    "onnxruntime_providers_winml.dll",
    "onnxruntime_providers_openvino.dll"
)
$foundUniversalProviders = @()
foreach ($provider in $universalProviderDlls) {
    $path = Join-Path $win64Dir $provider
    if (Test-Path $path) {
        $foundUniversalProviders += $provider
        Write-Host "[PASS] Found $provider" -ForegroundColor Green
    }
}
if ($foundUniversalProviders.Count -eq 0) {
    $message = "No Windows universal GPU provider DLL found; AMD/Intel systems will fall back to CPU."
    $hasUniversalGpuBackend = $supportedBackends -contains "dml" -or
        $supportedBackends -contains "winml" -or
        $supportedBackends -contains "openvino"
    if ($expectsUniversalGpuPath -and -not $hasUniversalGpuBackend) {
        Write-Host "[FAIL] $message" -ForegroundColor Red
        throw $message
    }
    if (-not $hasUniversalGpuBackend) {
        if ($expectsDirectMlPath) {
            Write-Host "[FAIL] $message" -ForegroundColor Red
            throw $message
        }
        Write-Host "[INFO] $message" -ForegroundColor Cyan
    }
}

if ($expectsDirectMlPath -and ($supportedBackends -notcontains "dml")) {
    Write-Host "[FAIL] DirectML bundle did not report DML support in runtime probe." -ForegroundColor Red
    throw "DirectML bundle missing DML runtime support."
}

# Check models
$bundleModelInventory = if (Test-Path $modelInventoryPath) {
    Get-Content -Path $modelInventoryPath -Raw | ConvertFrom-Json
} else {
    $expectedModels = Get-CorridorKeyOfxBundleTargetModels -ModelProfile $defaultModelProfile
    $fallbackInventory = [ordered]@{}
    foreach ($property in (Get-CorridorKeyModelInventory -ModelsDir $resourcesDir -ExpectedModels $expectedModels).GetEnumerator()) {
        $fallbackInventory[$property.Key] = $property.Value
    }
    $fallbackInventory["model_profile"] = $defaultModelProfile
    [pscustomobject]$fallbackInventory
}

$presentModels = @($bundleModelInventory.present_models)
$missingModels = @($bundleModelInventory.missing_models)
$expectedModels = @($bundleModelInventory.expected_models)
$modelProfile = if (Test-CorridorKeyJsonProperty -Object $bundleModelInventory -Name "model_profile") {
    $bundleModelInventory.model_profile
} else {
    $defaultModelProfile
}
$expectedProfileContract = Get-CorridorKeyModelProfileContract -ModelProfile $modelProfile
$compiledContextComplete = if (Test-CorridorKeyJsonProperty -Object $bundleModelInventory -Name "compiled_context_complete") {
    [bool]$bundleModelInventory.compiled_context_complete
} else {
    $false
}
$expectedCompiledContextModels = if (Test-CorridorKeyJsonProperty -Object $bundleModelInventory -Name "expected_compiled_context_models") {
    @($bundleModelInventory.expected_compiled_context_models)
} else {
    @()
}
$missingCompiledContextModels = if (Test-CorridorKeyJsonProperty -Object $bundleModelInventory -Name "missing_compiled_context_models") {
    @($bundleModelInventory.missing_compiled_context_models)
} else {
    @()
}
$inventoryContractIssues = Get-CorridorKeyInventoryContractIssues `
    -Inventory $bundleModelInventory `
    -ExpectedContract $expectedProfileContract
$inventoryContractComplete = @($inventoryContractIssues).Count -eq 0
if (-not $inventoryContractComplete) {
    throw "Bundle model inventory contract is invalid. Issues: $($inventoryContractIssues -join ' | ')"
}

$certificationContractIssues = @()
$certificationContractComplete = $false
$certificationManifestPresent = Test-Path $artifactManifestPath
if ($expectedProfileContract.expects_compiled_context_models) {
    if (-not $certificationManifestPresent) {
        $certificationContractIssues += "Packaged RTX bundle is missing artifact_manifest.json."
    } else {
        $certificationManifest = Read-CorridorKeyWindowsRtxArtifactManifest -ArtifactManifestPath $artifactManifestPath
        $certificationContractIssues = Get-CorridorKeyWindowsRtxArtifactManifestIssues `
            -Manifest $certificationManifest `
            -ArtifactsDir $resourcesDir `
            -ExpectedModels $presentModels `
            -ExpectedCompiledContextModels $expectedCompiledContextModels
        $certificationContractComplete = @($certificationContractIssues).Count -eq 0
        if (-not $certificationContractComplete) {
            throw "Bundle RTX certification contract is invalid. Issues: $($certificationContractIssues -join ' | ')"
        }
    }
}

foreach ($model in $presentModels) {
    $path = Join-Path $resourcesDir $model
    $modelSize = (Get-Item $path).Length
    Write-Host "[PASS] Found $model ($([math]::Round($modelSize / 1MB, 2)) MB)" -ForegroundColor Green
}
foreach ($model in $missingModels) {
    Write-Host "[INFO] Packaged bundle omits model: $model" -ForegroundColor Cyan
}

$doctorReportPath = Join-Path $bundleRoot "doctor_report.json"
$previousModelsDir = if (Test-Path Env:CORRIDORKEY_MODELS_DIR) {
    $env:CORRIDORKEY_MODELS_DIR
} else {
    $null
}
$doctorSucceeded = $false
$doctorHealthy = $false
$doctorModelContractsAvailable = $false
$doctorModelContractsHealthy = $null
$doctorFailureTolerated = $false
$doctorFailureReason = ""
$doctorModelContractIssues = @()

Write-Host "[DOCTOR] Running packaged runtime doctor..." -ForegroundColor Cyan
Push-Location $win64Dir
try {
    $doctorStdoutPath = Join-Path $env:TEMP ("corridorkey_validate_stdout_" + [System.Guid]::NewGuid().ToString("N") + ".txt")
    $doctorStderrPath = Join-Path $env:TEMP ("corridorkey_validate_stderr_" + [System.Guid]::NewGuid().ToString("N") + ".txt")
    try {
        $doctorCommand = 'set "CORRIDORKEY_MODELS_DIR={0}" && cd /d "{1}" && corridorkey.exe doctor --json > "{2}" 2> "{3}"' -f `
            $resourcesDir, $win64Dir, $doctorStdoutPath, $doctorStderrPath
        & $env:ComSpec /v:on /d /c $doctorCommand | Out-Null
        $doctorExitCode = $LASTEXITCODE
        $doctorJson = if (Test-Path $doctorStdoutPath) {
            Get-Content -Path $doctorStdoutPath -Raw -ErrorAction SilentlyContinue
        } else {
            ""
        }
        $doctorStderr = if (Test-Path $doctorStderrPath) {
            Get-Content -Path $doctorStderrPath -Raw -ErrorAction SilentlyContinue
        } else {
            ""
        }
    } finally {
        Remove-Item $doctorStdoutPath -Force -ErrorAction SilentlyContinue
        Remove-Item $doctorStderrPath -Force -ErrorAction SilentlyContinue
    }

    if ($doctorExitCode -ne 0 -or [string]::IsNullOrWhiteSpace($doctorJson)) {
        if (-not [string]::IsNullOrWhiteSpace($doctorStderr)) {
            Write-Host "[INFO] Packaged runtime doctor stderr:" -ForegroundColor Cyan
            Write-Host $doctorStderr -ForegroundColor Cyan
        }
        if ($missingModels.Count -gt 0) {
            $doctorFailureTolerated = $true
            $doctorFailureReason = "Packaged runtime doctor failed while the bundle is missing model(s)."
            Write-Host "[INFO] $doctorFailureReason" -ForegroundColor Cyan
        } else {
            throw "Packaged runtime doctor failed."
        }
    }

    $doctor = $null
    if (-not [string]::IsNullOrWhiteSpace($doctorJson)) {
        $doctorJson | Set-Content -Path $doctorReportPath -Encoding UTF8
        $doctor = $doctorJson | ConvertFrom-Json
        if (-not (Test-CorridorKeyJsonProperty -Object $doctor -Name "summary") -or $null -eq $doctor.summary) {
            throw "Packaged runtime doctor report is missing the summary payload."
        }

        $doctorSucceeded = $true
        $doctorHealthy = [bool]$doctor.summary.healthy
        $doctorModelContractsAvailable = Test-CorridorKeyJsonProperty -Object $doctor -Name "model_contracts"
        if (Test-CorridorKeyJsonProperty -Object $doctor.summary -Name "model_contracts_healthy") {
            $doctorModelContractsHealthy = [bool]$doctor.summary.model_contracts_healthy
            $doctorModelContractsAvailable = $true
        } elseif (Test-CorridorKeyJsonProperty -Object $doctor.summary -Name "bundle_inventory_contract_healthy") {
            $doctorModelContractsHealthy = [bool]$doctor.summary.bundle_inventory_contract_healthy
            $doctorModelContractsAvailable = $true
        }

        Write-Host "[PASS] Wrote doctor report: $doctorReportPath" -ForegroundColor Green
        $summaryModelContractsHealthy = if ($doctorModelContractsAvailable -and $null -ne $doctorModelContractsHealthy) {
            $doctorModelContractsHealthy
        } else {
            "n/a"
        }
        Write-Host "[INFO] Doctor summary healthy=$($doctor.summary.healthy) model_contracts_healthy=$summaryModelContractsHealthy windows_universal_healthy=$($doctor.summary.windows_universal_healthy)" -ForegroundColor Cyan
        if ((Test-CorridorKeyJsonProperty -Object $doctor -Name "model_contracts") -and
            $null -ne $doctor.model_contracts) {
            $contractGroups = @($doctor.model_contracts.groups)
            foreach ($group in $contractGroups) {
                Write-Host "[INFO] Model contract group '$($group.group)': healthy=$($group.healthy) loadable=$($group.all_models_loadable) consistent=$($group.contract_consistent) baseline=$($group.baseline_model)" -ForegroundColor Cyan
            }
            $unhealthyContractGroups = @($contractGroups | Where-Object { -not $_.healthy })
            foreach ($group in $unhealthyContractGroups) {
                $firstIssue = $group.models | Where-Object {
                    (-not $_.load_ok) -or (-not $_.contract_match_baseline)
                } | Select-Object -First 1
                if ($null -ne $firstIssue) {
                    $doctorModelContractIssues += @($group.models | Where-Object {
                        (-not $_.load_ok) -or (-not $_.contract_match_baseline)
                    })
                    $reason = if ([string]::IsNullOrWhiteSpace($firstIssue.error)) {
                        "Contract mismatch relative to baseline."
                    } else {
                        $firstIssue.error
                    }
                    Write-Host "[INFO] Model contract group '$($group.group)' first issue: $($firstIssue.filename) -> $reason" -ForegroundColor Cyan
                }
            }
        } else {
            Write-Host "[INFO] Doctor schema does not expose model contract groups; skipping that validation layer." -ForegroundColor Cyan
        }
    }
} finally {
    if ($null -ne $previousModelsDir) {
        $env:CORRIDORKEY_MODELS_DIR = $previousModelsDir
    } else {
        Remove-Item Env:CORRIDORKEY_MODELS_DIR -ErrorAction SilentlyContinue
    }
    Pop-Location
}

if ($doctorSucceeded -and $doctorModelContractsAvailable -and -not $doctorModelContractsHealthy) {
    $nonMissingIssues = @($doctorModelContractIssues | Where-Object {
        ($missingModels -notcontains $_.filename) -or $_.error -ne "Model not found"
    })
    if ($nonMissingIssues.Count -eq 0 -and $missingModels.Count -gt 0) {
        $doctorFailureTolerated = $true
        if ([string]::IsNullOrWhiteSpace($doctorFailureReason)) {
            $doctorFailureReason = "Packaged runtime doctor reported unhealthy model contracts only because model(s) are absent from this bundle."
        }
        Write-Host "[INFO] Packaged runtime doctor reported unhealthy model contracts only because model(s) are absent from this bundle." -ForegroundColor Cyan
    } else {
        throw "Packaged runtime doctor reported unhealthy model contracts. See $doctorReportPath."
    }
}

if ($doctorSucceeded -and -not $doctorHealthy -and -not $doctorFailureTolerated) {
    $missingModelProbeFailuresOnly = Test-CorridorKeyDoctorMissingModelProbeFailuresOnly `
        -Doctor $doctor `
        -MissingModels $missingModels
    if ($missingModelProbeFailuresOnly) {
        $doctorFailureTolerated = $true
        if ([string]::IsNullOrWhiteSpace($doctorFailureReason)) {
            $doctorFailureReason = "Packaged runtime doctor reported unhealthy execution probes only because model(s) are absent from this bundle."
        }
        Write-Host "[INFO] Packaged runtime doctor reported unhealthy execution probes only because model(s) are absent from this bundle." -ForegroundColor Cyan
    } else {
        throw "Packaged runtime doctor reported unhealthy status. See $doctorReportPath."
    }
}

$validationPayload = [ordered]@{
    bundle_path = $bundleDescriptor
    validation_passed = $true
    runtime_probe = [ordered]@{
        supported_backends = @($supportedBackends)
    }
    models = [ordered]@{
        model_profile = $modelProfile
        expected_models = @($expectedModels)
        present_models = @($presentModels)
        missing_models = @($missingModels)
        present_count = @($presentModels).Count
        missing_count = @($missingModels).Count
        inventory_contract = [ordered]@{
            complete = $inventoryContractComplete
            issues = @($inventoryContractIssues)
            expected_contract = [ordered]@{
                package_type = $expectedProfileContract.package_type
                model_profile = $expectedProfileContract.model_profile
                bundle_track = $expectedProfileContract.bundle_track
                release_label = $expectedProfileContract.release_label
                optimization_profile_id = $expectedProfileContract.optimization_profile_id
                optimization_profile_label = $expectedProfileContract.optimization_profile_label
                backend_intent = $expectedProfileContract.backend_intent
                fallback_policy = $expectedProfileContract.fallback_policy
                warmup_policy = $expectedProfileContract.warmup_policy
                certification_tier = $expectedProfileContract.certification_tier
                unrestricted_quality_attempt = $expectedProfileContract.unrestricted_quality_attempt
            }
            compiled_context_complete = $compiledContextComplete
            expected_compiled_context_models = @($expectedCompiledContextModels)
            missing_compiled_context_models = @($missingCompiledContextModels)
        }
        certification_contract = [ordered]@{
            complete = $certificationContractComplete
            issues = @($certificationContractIssues)
            manifest_present = $certificationManifestPresent
        }
    }
    doctor = [ordered]@{
        attempted = $true
        succeeded = $doctorSucceeded
        healthy = $doctorHealthy
        model_contracts_available = $doctorModelContractsAvailable
        model_contracts_healthy = $doctorModelContractsHealthy
        failure_tolerated = $doctorFailureTolerated
        failure_reason = $doctorFailureReason
        report_path = if ($doctorSucceeded) { $doctorReportPath } else { "" }
    }
}
Write-CorridorKeyJsonFile -Path $bundleValidationPath -Payload $validationPayload
Write-Host "[PASS] Wrote bundle validation report: $bundleValidationPath" -ForegroundColor Green

Write-Host ""
Write-Host "================================" -ForegroundColor Green
Write-Host "Bundle validation PASSED" -ForegroundColor Green
Write-Host "================================" -ForegroundColor Green
Write-Host ""
if ($missingModels.Count -gt 0) {
    Write-Host "Bundle is ready for installation with partial model coverage. Missing models are listed in bundle_validation.json and model_inventory.json." -ForegroundColor Cyan
} else {
    Write-Host "Bundle is ready for installation and should work with DaVinci Resolve."
}
