param(
    [string]$Version = "",
    [string]$BuildPreset = "release",
    [string]$BuildDir = "",
    [string]$OrtRoot = "",
    [string]$SourceModelsDir = "",
    [string]$OutputModelsDir = "",
    [ValidateSet("windows-rtx")]
    [string]$ModelProfile = "windows-rtx",
    [string]$DisplayVersionLabel = "",
    [switch]$Skip2048
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot "windows_runtime_helpers.ps1")

function Invoke-ExternalCommand {
    param(
        [string]$FilePath,
        [string[]]$Arguments,
        [string]$WorkingDirectory = ""
    )

    $display = @($FilePath) + $Arguments
    Write-Host ("  > " + ($display -join " ")) -ForegroundColor DarkGray

    $invoke = {
        if ([System.StringComparer]::OrdinalIgnoreCase.Equals([System.IO.Path]::GetExtension($FilePath), ".ps1")) {
            & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $FilePath @Arguments
        } else {
            & $FilePath @Arguments
        }
    }

    if ([string]::IsNullOrWhiteSpace($WorkingDirectory)) {
        & $invoke
    } else {
        Push-Location $WorkingDirectory
        try {
            & $invoke
        } finally {
            Pop-Location
        }
    }

    if ($LASTEXITCODE -ne 0) {
        throw "Command failed: $FilePath"
    }
}

function Invoke-CliJson {
    param(
        [string]$CliPath,
        [string[]]$Arguments
    )

    $stdoutPath = Join-Path $env:TEMP ("corridorkey_cli_stdout_" + [System.Guid]::NewGuid().ToString("N") + ".json")
    $stderrPath = Join-Path $env:TEMP ("corridorkey_cli_stderr_" + [System.Guid]::NewGuid().ToString("N") + ".txt")

    try {
        & $CliPath @Arguments 1> $stdoutPath 2> $stderrPath
        $exitCode = $LASTEXITCODE
        $stdout = if (Test-Path $stdoutPath) { Get-Content -Path $stdoutPath -Raw } else { "" }
        $stderr = if (Test-Path $stderrPath) { Get-Content -Path $stderrPath -Raw } else { "" }
    } finally {
        Remove-Item $stdoutPath -Force -ErrorAction SilentlyContinue
        Remove-Item $stderrPath -Force -ErrorAction SilentlyContinue
    }

    if ($exitCode -ne 0) {
        $message = if ([string]::IsNullOrWhiteSpace($stderr)) { $stdout } else { $stderr }
        throw "CLI command failed: $message"
    }

    if ([string]::IsNullOrWhiteSpace($stdout)) {
        throw "CLI command produced no JSON output."
    }

    return $stdout | ConvertFrom-Json
}

$Version = Initialize-CorridorKeyVersion -RepoRoot $repoRoot -Version $Version -SyncGuiMetadata
if ([string]::IsNullOrWhiteSpace($BuildDir)) {
    $BuildDir = Join-Path $repoRoot ("build\" + $BuildPreset)
}
if ([string]::IsNullOrWhiteSpace($SourceModelsDir)) {
    $SourceModelsDir = Join-Path $repoRoot "models"
}
if ([string]::IsNullOrWhiteSpace($OutputModelsDir)) {
    $OutputModelsDir = Join-Path $BuildDir ("windows_rtx_certified\" + $ModelProfile)
}
$OrtRoot = Resolve-CorridorKeyWindowsOrtRoot -RepoRoot $repoRoot -ExplicitRoot $OrtRoot -PreferredTrack "rtx"
$cliPath = Join-Path $BuildDir "src\cli\corridorkey.exe"
$validationReportPath = Join-Path $OutputModelsDir "windows_rtx_validation_report.json"

if (-not (Test-Path $cliPath)) {
    Write-Host "[certify-rtx-artifacts] Runtime CLI not found. Building $BuildPreset first..." -ForegroundColor Cyan
    Invoke-ExternalCommand -FilePath (Join-Path $repoRoot "scripts\build.ps1") -Arguments @(
        "-Preset", $BuildPreset,
        "-DisplayVersionLabel", $DisplayVersionLabel
    )
}

if (-not (Test-Path $cliPath)) {
    throw "CLI binary not found after build: $cliPath"
}

$targetModels = Get-CorridorKeyOfxBundleTargetModels -ModelProfile $ModelProfile
if ($Skip2048.IsPresent) {
    $targetModels = @($targetModels | Where-Object { $_ -ne "corridorkey_fp16_2048.onnx" })
}

if (Test-Path $OutputModelsDir) {
    Remove-Item $OutputModelsDir -Recurse -Force
}
New-Item -ItemType Directory -Path $OutputModelsDir -Force | Out-Null

Write-Host "[certify-rtx-artifacts] Source models: $SourceModelsDir" -ForegroundColor Cyan
Write-Host "[certify-rtx-artifacts] Output models: $OutputModelsDir" -ForegroundColor Cyan

$sourceInventory = Get-CorridorKeyModelInventory -ModelsDir $SourceModelsDir -ExpectedModels $targetModels
if ($sourceInventory.missing_count -gt 0) {
    throw "Source model set is incomplete for $ModelProfile. Missing: $($sourceInventory.missing_models -join ', ')"
}

foreach ($model in $sourceInventory.present_models) {
    Copy-Item -Path (Join-Path $SourceModelsDir $model) -Destination (Join-Path $OutputModelsDir $model) -Force
}

$runtimeInfo = Invoke-CliJson -CliPath $cliPath -Arguments @("info", "--json")
$activeTensorRtDevice = @($runtimeInfo.devices | Where-Object { $_.backend -eq "tensorrt" } | Select-Object -First 1)
if ($activeTensorRtDevice.Count -eq 0) {
    throw "No active TensorRT device is available on this host."
}

$fp16TargetModels = @($sourceInventory.present_models | Where-Object { $_ -match '^corridorkey_fp16_[0-9]+\.onnx$' })
$profileResults = @()
$certificationFailed = $false

Write-Host "[certify-rtx-artifacts] Certifying packaged FP16 ladder on the active RTX host..." -ForegroundColor Cyan
foreach ($model in $fp16TargetModels) {
    $resolution = [int](([regex]::Match($model, '_(\d+)\.onnx$')).Groups[1].Value)
    $runtimeArtifactPath = Join-Path $OutputModelsDir $model
    $compiledContextPath = Join-Path $OutputModelsDir (([System.IO.Path]::GetFileNameWithoutExtension($model)) + "_ctx.onnx")

    $result = [ordered]@{
        profile = $model
        resolution = $resolution
        runtime_artifact = [System.IO.Path]::GetFullPath($runtimeArtifactPath)
        compiled_context_model = [System.IO.Path]::GetFullPath($compiledContextPath)
        runtime_artifact_sha256 = ""
        compiled_context_sha256 = ""
        session_create_ok = $false
        frame_execute_ok = $false
        repeated_execute_ok = $false
        certified = $false
        backend = ""
        device = ""
        error = ""
    }

    try {
        $compileJson = Invoke-CliJson -CliPath $cliPath -Arguments @(
            "compile-context",
            "--model", $runtimeArtifactPath,
            "--output", $compiledContextPath,
            "--device", "tensorrt",
            "--json"
        )
        if (-not $compileJson.success) {
            throw "compile-context returned success=false"
        }
        $result.session_create_ok = $true

        $benchmarkJson = Invoke-CliJson -CliPath $cliPath -Arguments @(
            "benchmark",
            "--model", $runtimeArtifactPath,
            "--device", "tensorrt",
            "--resolution", [string]$resolution,
            "--json"
        )

        if ($benchmarkJson.PSObject.Properties.Match("error").Count -gt 0 -and
            -not [string]::IsNullOrWhiteSpace($benchmarkJson.error)) {
            throw $benchmarkJson.error
        }

        if ($benchmarkJson.backend -ne "tensorrt") {
            throw "Expected tensorrt backend but benchmark used '$($benchmarkJson.backend)'."
        }

        $result.backend = $benchmarkJson.backend
        $result.device = $benchmarkJson.device
        $result.frame_execute_ok = $true
        $result.repeated_execute_ok = $true
        $result.certified = $true
        $result.runtime_artifact_sha256 = Get-CorridorKeyFileSha256 -Path $runtimeArtifactPath
        if (Test-Path $compiledContextPath) {
            $result.compiled_context_sha256 = Get-CorridorKeyFileSha256 -Path $compiledContextPath
        }
    } catch {
        $certificationFailed = $true
        $result.error = $_.Exception.Message
    }

    $profileResults += [pscustomobject]$result
}

$validationReport = [ordered]@{
    pipeline = "windows_rtx_existing_artifact_certification"
    version = $Version
    model_profile = $ModelProfile
    source_models_dir = [System.IO.Path]::GetFullPath($SourceModelsDir)
    certified_models_dir = [System.IO.Path]::GetFullPath($OutputModelsDir)
    host_runtime = $runtimeInfo
    certification_device = $activeTensorRtDevice[0]
    profiles = @($profileResults)
    certified_models = @($profileResults | Where-Object { $_.certified } | ForEach-Object {
            Split-Path -Leaf $_.runtime_artifact
        })
    failed_models = @($profileResults | Where-Object { -not $_.certified } | ForEach-Object {
            Split-Path -Leaf $_.runtime_artifact
        })
    all_profiles_certified = (-not $certificationFailed)
}
Write-CorridorKeyJsonFile -Path $validationReportPath -Payload $validationReport

if ($certificationFailed) {
    throw "Windows RTX artifact certification failed. See $validationReportPath"
}

$manifestPath = Write-CorridorKeyWindowsRtxArtifactManifest `
    -ModelsDir $OutputModelsDir `
    -ValidationReportPath $validationReportPath

$summary = [ordered]@{
    version = $Version
    model_profile = $ModelProfile
    source_models_dir = [System.IO.Path]::GetFullPath($SourceModelsDir)
    certified_models_dir = [System.IO.Path]::GetFullPath($OutputModelsDir)
    validation_report = [System.IO.Path]::GetFullPath($validationReportPath)
    artifact_manifest = $manifestPath
    certified_models = @($validationReport.certified_models)
}

$summary | ConvertTo-Json -Depth 5
