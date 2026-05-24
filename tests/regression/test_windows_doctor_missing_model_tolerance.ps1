Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
. (Join-Path $repoRoot "scripts\windows_runtime_helpers.ps1")

$doctorMissingOnly = [pscustomobject]@{
    windows_universal = [pscustomobject]@{
        execution_probes = @(
            [pscustomobject]@{
                model = "corridorkey_fp16_2048.onnx"
                model_found = $false
                error = "Model not found: corridorkey_fp16_2048.onnx"
                session_create_ok = $false
                frame_execute_ok = $false
            },
            [pscustomobject]@{
                model = "corridorkey_fp16_1536.onnx"
                model_found = $true
                error = ""
                session_create_ok = $true
                frame_execute_ok = $true
            }
        )
    }
}

if (-not (Test-CorridorKeyDoctorMissingModelProbeFailuresOnly `
        -Doctor $doctorMissingOnly `
        -MissingModels @("corridorkey_fp16_2048.onnx"))) {
    throw "Expected missing-model-only execution probe failures to be tolerated."
}

$doctorRealFailure = [pscustomobject]@{
    windows_universal = [pscustomobject]@{
        execution_probes = @(
            [pscustomobject]@{
                model = "corridorkey_fp16_1536.onnx"
                model_found = $true
                error = "Model output contains non-finite values"
                session_create_ok = $true
                frame_execute_ok = $false
            }
        )
    }
}

if (Test-CorridorKeyDoctorMissingModelProbeFailuresOnly `
        -Doctor $doctorRealFailure `
        -MissingModels @("corridorkey_fp16_2048.onnx")) {
    throw "Did not expect real execution failures to be tolerated as missing-model-only issues."
}

$doctorBundleMissingOnly = [pscustomobject]@{
    bundle = [pscustomobject]@{
        healthy = $false
        packaged_layout_detected = $true
        runtime_backend_bundle_ready = $true
        packaged_models = @(
            [pscustomobject]@{
                filename = "corridorkey_fp16_2048.onnx"
                found = $false
                usable = $false
                artifact_status = "missing"
                error = "Model not found"
            },
            [pscustomobject]@{
                filename = "corridorkey_fp16_1536.onnx"
                found = $true
                usable = $true
                artifact_status = "usable"
                error = ""
            }
        )
    }
}

if (-not (Test-CorridorKeyDoctorMissingModelBundleFailuresOnly `
        -Doctor $doctorBundleMissingOnly `
        -MissingModels @("corridorkey_fp16_2048.onnx"))) {
    throw "Expected missing-model-only bundle failures to be tolerated."
}

$doctorBundleInvalidPresent = [pscustomobject]@{
    bundle = [pscustomobject]@{
        healthy = $false
        packaged_layout_detected = $true
        runtime_backend_bundle_ready = $true
        packaged_models = @(
            [pscustomobject]@{
                filename = "corridorkey_fp16_1536.onnx"
                found = $true
                usable = $false
                artifact_status = "invalid"
                error = "Model output contains non-finite values"
            }
        )
    }
}

if (Test-CorridorKeyDoctorMissingModelBundleFailuresOnly `
        -Doctor $doctorBundleInvalidPresent `
        -MissingModels @("corridorkey_fp16_2048.onnx")) {
    throw "Did not expect invalid present bundle models to be tolerated as missing-model-only issues."
}

Write-Host "[PASS] Windows doctor missing-model tolerance regression checks passed." -ForegroundColor Green
