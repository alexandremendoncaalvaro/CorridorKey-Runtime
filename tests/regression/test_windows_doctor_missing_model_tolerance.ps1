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

Write-Host "[PASS] Windows doctor missing-model tolerance regression checks passed." -ForegroundColor Green
