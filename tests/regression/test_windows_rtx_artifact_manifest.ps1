Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
. (Join-Path $repoRoot "scripts\windows_runtime_helpers.ps1")

$tempRoot = Join-Path $env:TEMP ("corridorkey_rtx_manifest_test_" + [System.Guid]::NewGuid().ToString("N"))
$modelsDir = Join-Path $tempRoot "models"
$validationReportPath = Join-Path $tempRoot "windows_rtx_validation_report.json"

function Assert-Throws {
    param(
        [scriptblock]$ScriptBlock,
        [string]$Message
    )

    $threw = $false
    try {
        & $ScriptBlock
    } catch {
        $threw = $true
    }

    if (-not $threw) {
        throw $Message
    }
}

try {
    New-Item -ItemType Directory -Path $modelsDir -Force | Out-Null

    $files = @{
        "corridorkey_fp16_512.onnx" = "fp16-512"
        "corridorkey_fp16_512_ctx.onnx" = "fp16-512-ctx"
        "corridorkey_int8_512.onnx" = "int8-512"
    }

    foreach ($entry in $files.GetEnumerator()) {
        [System.IO.File]::WriteAllText((Join-Path $modelsDir $entry.Key), $entry.Value, [System.Text.Encoding]::ASCII)
    }

    Write-CorridorKeyJsonFile -Path $validationReportPath -Payload ([ordered]@{
            pipeline = "windows_rtx_mmdeploy_style"
            all_profiles_certified = $true
            certified_models = @("corridorkey_fp16_512.onnx")
        })

    $manifestPath = Write-CorridorKeyWindowsRtxArtifactManifest `
        -ModelsDir $modelsDir `
        -ValidationReportPath $validationReportPath

    $manifest = Assert-CorridorKeyWindowsRtxArtifactManifestHealthy `
        -ArtifactsDir $modelsDir `
        -ExpectedModels @("corridorkey_fp16_512.onnx", "corridorkey_int8_512.onnx") `
        -ExpectedCompiledContextModels @("corridorkey_fp16_512_ctx.onnx") `
        -ArtifactManifestPath $manifestPath `
        -Label "regression fixture"

    if (-not [bool]$manifest.all_profiles_certified) {
        throw "Expected the generated RTX manifest to preserve all_profiles_certified=true."
    }

    [System.IO.File]::WriteAllText((Join-Path $modelsDir "corridorkey_fp16_512.onnx"), "fp16-512-mutated", [System.Text.Encoding]::ASCII)

    Assert-Throws -Message "Expected the RTX manifest assertion to fail after the packaged model changed." -ScriptBlock {
        Assert-CorridorKeyWindowsRtxArtifactManifestHealthy `
            -ArtifactsDir $modelsDir `
            -ExpectedModels @("corridorkey_fp16_512.onnx", "corridorkey_int8_512.onnx") `
            -ExpectedCompiledContextModels @("corridorkey_fp16_512_ctx.onnx") `
            -ArtifactManifestPath $manifestPath `
            -Label "mutated fixture" | Out-Null
    }

    Write-Host "[PASS] Windows RTX artifact manifest regression checks passed." -ForegroundColor Green
} finally {
    Remove-Item $tempRoot -Recurse -Force -ErrorAction SilentlyContinue
}
