param(
    [string]$Version = "",
    [string]$CorridorKeyRepo = "",
    [string]$Checkpoint = "",
    [string]$BuildPreset = "release",
    [string]$Uv = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot "windows_runtime_helpers.ps1")

function Resolve-CommandPath {
    param(
        [string]$ExplicitPath,
        [string[]]$CandidateNames,
        [string]$ErrorMessage
    )

    if (-not [string]::IsNullOrWhiteSpace($ExplicitPath)) {
        return $ExplicitPath
    }

    foreach ($candidateName in $CandidateNames) {
        $command = Get-Command $candidateName -ErrorAction SilentlyContinue
        if ($null -ne $command) {
            return $command.Source
        }
    }

    throw $ErrorMessage
}

function Resolve-CorridorKeyRepo {
    param([string]$ExplicitPath)

    $candidates = @()
    if (-not [string]::IsNullOrWhiteSpace($ExplicitPath)) {
        $candidates += $ExplicitPath
    }
    if (-not [string]::IsNullOrWhiteSpace($env:CORRIDORKEY_SOURCE_REPO)) {
        $candidates += $env:CORRIDORKEY_SOURCE_REPO
    }

    $parentRoot = Split-Path -Parent $repoRoot
    foreach ($name in @("CorridorKey-Engine", "CorridorKey", "corridorkey", "CorridorKey-main")) {
        $candidates += Join-Path $parentRoot $name
    }

    foreach ($candidate in ($candidates | Where-Object { -not [string]::IsNullOrWhiteSpace($_) } |
            Select-Object -Unique)) {
        $normalizedCandidate = [System.IO.Path]::GetFullPath($candidate)
        if (Test-Path (Join-Path $normalizedCandidate "CorridorKeyModule")) {
            return $normalizedCandidate
        }
    }

    throw "CorridorKey source repository not found. Pass -CorridorKeyRepo or set CORRIDORKEY_SOURCE_REPO."
}

function Resolve-CheckpointPath {
    param(
        [string]$ExplicitPath,
        [string]$SourceRepo
    )

    if (-not [string]::IsNullOrWhiteSpace($ExplicitPath)) {
        if (-not (Test-Path $ExplicitPath)) {
            throw "Checkpoint not found: $ExplicitPath"
        }
        return [System.IO.Path]::GetFullPath($ExplicitPath)
    }

    $candidates = @(
        (Join-Path $repoRoot "models\\CorridorKey.pth"),
        (Join-Path $repoRoot "models\\CorridorKey_v1.0.pth"),
        (Join-Path $SourceRepo "CorridorKeyModule\\checkpoints\\CorridorKey.pth"),
        (Join-Path $SourceRepo "CorridorKeyModule\\checkpoints\\CorridorKey_v1.0.pth")
    )

    foreach ($candidate in $candidates) {
        if (Test-Path $candidate) {
            return [System.IO.Path]::GetFullPath($candidate)
        }
    }

    throw "No CorridorKey checkpoint was found. Pass -Checkpoint explicitly."
}

function Invoke-ExternalCommand {
    param(
        [string]$FilePath,
        [string[]]$Arguments,
        [string]$WorkingDirectory = ""
    )

    $display = @($FilePath) + $Arguments
    Write-Host ("  > " + ($display -join " ")) -ForegroundColor DarkGray

    if ([string]::IsNullOrWhiteSpace($WorkingDirectory)) {
        & $FilePath @Arguments
    } else {
        Push-Location $WorkingDirectory
        try {
            & $FilePath @Arguments
        } finally {
            Pop-Location
        }
    }

    if ($LASTEXITCODE -ne 0) {
        throw "Command failed: $FilePath"
    }
}

function Load-DeployProfiles {
    param([string]$Path)

    $json = Get-Content -Path $Path -Raw | ConvertFrom-Json
    if ($json.pipeline -ne "windows_rtx_mmdeploy_style") {
        throw "Unsupported deploy profile pipeline in $Path"
    }
    return @($json.profiles)
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
$uvPath = Resolve-CommandPath -ExplicitPath $Uv -CandidateNames @("uv.exe", "uv") `
    -ErrorMessage "uv was not found. Install uv or pass -Uv."
$sourceRepo = Resolve-CorridorKeyRepo -ExplicitPath $CorridorKeyRepo
$checkpointPath = Resolve-CheckpointPath -ExplicitPath $Checkpoint -SourceRepo $sourceRepo
$ortRoot = Resolve-CorridorKeyWindowsOrtRoot -RepoRoot $repoRoot -PreferredTrack "rtx"
$deployProfilesPath = Join-Path $repoRoot "tools\\model_exporter\\windows_rtx_deploy_profiles.json"
$deployProfiles = Load-DeployProfiles -Path $deployProfilesPath
$buildDir = Join-Path $repoRoot ("build\\" + $BuildPreset)
$deployRoot = Join-Path $buildDir "windows_rtx_mmdeploy"
$rawOnnxDir = Join-Path $deployRoot "raw_onnx"
$backendArtifactsDir = Join-Path $deployRoot "backend_artifacts"
$promotedModelsDir = Join-Path $deployRoot "promoted_models"
$validationReportPath = Join-Path $buildDir "windows_rtx_validation_report.json"
$toolDir = Join-Path $repoRoot "tools\\model_exporter"
$cliPath = Join-Path $buildDir "src\\cli\\corridorkey.exe"

Write-Host "[regen-rtx-release] Version: $Version" -ForegroundColor Cyan
Write-Host "[regen-rtx-release] Source repo: $sourceRepo" -ForegroundColor Cyan
Write-Host "[regen-rtx-release] Checkpoint: $checkpointPath" -ForegroundColor Cyan
Write-Host "[regen-rtx-release] Curated RTX runtime: $ortRoot" -ForegroundColor Cyan

foreach ($path in @($deployRoot, $rawOnnxDir, $backendArtifactsDir, $promotedModelsDir)) {
    if (Test-Path $path) {
        Remove-Item $path -Recurse -Force
    }
    New-Item -ItemType Directory -Path $path -Force | Out-Null
}

Write-Host "[1/9] Exporting raw ONNX deploy profiles..." -ForegroundColor Cyan
Invoke-ExternalCommand -FilePath $uvPath -WorkingDirectory $toolDir -Arguments @(
    "run", "python", "export_windows_rtx_onnx.py",
    "--profiles", $deployProfilesPath,
    "--ckpt", $checkpointPath,
    "--output-dir", $rawOnnxDir,
    "--repo-path", $sourceRepo
)

$rawModelNames = @($deployProfiles | ForEach-Object { $_.artifacts.raw_onnx })
Write-Host "[2/9] Validating raw ONNX parity against PyTorch..." -ForegroundColor Cyan
Invoke-ExternalCommand -FilePath $uvPath -WorkingDirectory $toolDir -Arguments @(
    "run", "python", "validate_export_parity.py",
    "--ckpt", $checkpointPath,
    "--dir", $rawOnnxDir,
    "--repo-path", $sourceRepo,
    "--models"
) + $rawModelNames

Write-Host "[3/9] Preparing backend-facing ONNX artifacts..." -ForegroundColor Cyan
Invoke-ExternalCommand -FilePath $uvPath -WorkingDirectory $toolDir -Arguments @(
    "run", "python", "prepare_windows_rtx_backend_artifacts.py",
    "--profiles", $deployProfilesPath,
    "--raw-dir", $rawOnnxDir,
    "--output-dir", $backendArtifactsDir
)

Write-Host "[4/9] Quantizing CPU fallback artifacts..." -ForegroundColor Cyan
Invoke-ExternalCommand -FilePath $uvPath -WorkingDirectory $toolDir -Arguments @(
    "run", "python", "quantize_model.py",
    "--dir", $backendArtifactsDir
)

$preparedModelNames = Get-CorridorKeyPreparedModelList
Write-Host "[5/9] Validating prepared runtime artifacts on CPU ORT..." -ForegroundColor Cyan
Invoke-ExternalCommand -FilePath $uvPath -WorkingDirectory $toolDir -Arguments @(
    "run", "python", "validate_export_parity.py",
    "--ckpt", $checkpointPath,
    "--dir", $backendArtifactsDir,
    "--repo-path", $sourceRepo,
    "--models"
) + $preparedModelNames

Write-Host "[6/9] Building the Windows RTX runtime and plugin..." -ForegroundColor Cyan
$env:CORRIDORKEY_WINDOWS_ORT_ROOT = $ortRoot
Invoke-ExternalCommand -FilePath (Join-Path $repoRoot "scripts\\build.ps1") `
    -Arguments @("-Preset", $BuildPreset)

if (-not (Test-Path $cliPath)) {
    throw "CLI binary not found after build: $cliPath"
}

$runtimeInfo = Invoke-CliJson -CliPath $cliPath -Arguments @("info", "--json")
$certificationDevice = @($runtimeInfo.devices | Where-Object {
        $_.backend -eq "tensorrt" -and [int]$_.memory_mb -ge 24000
    } | Select-Object -First 1)
if ($certificationDevice.Count -eq 0) {
    throw "regen-rtx-release requires a certified Windows RTX host with at least 24 GB VRAM for 1536/2048 validation."
}
$profileResults = @()
$certificationFailed = $false

Write-Host "[7/9] Certifying backend artifacts on the active RTX host..." -ForegroundColor Cyan
foreach ($profile in $deployProfiles) {
    $runtimeArtifactPath = Join-Path $backendArtifactsDir $profile.artifacts.runtime_onnx
    $compiledContextPath = [System.IO.Path]::ChangeExtension($runtimeArtifactPath, $null) + "_ctx.onnx"

    $result = [ordered]@{
        profile = $profile.name
        resolution = [int]$profile.resolution
        runtime_artifact = [System.IO.Path]::GetFullPath($runtimeArtifactPath)
        compiled_context_model = [System.IO.Path]::GetFullPath($compiledContextPath)
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
            "--resolution", [string]$profile.resolution,
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
    } catch {
        $certificationFailed = $true
        $result.error = $_.Exception.Message
    }

    $profileResults += [pscustomobject]$result
}

$validationReport = [ordered]@{
    pipeline = "windows_rtx_mmdeploy_style"
    version = $Version
    host_runtime = $runtimeInfo
    certification_device = $certificationDevice[0]
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
    throw "Windows RTX certification failed. See $validationReportPath"
}

Write-Host "[8/9] Promoting certified artifacts into models/ and staging pack..." -ForegroundColor Cyan
foreach ($preparedModel in $preparedModelNames) {
    $sourcePath = Join-Path $backendArtifactsDir $preparedModel
    if (-not (Test-Path $sourcePath)) {
        throw "Prepared model missing after certification: $sourcePath"
    }

    $repoModelPath = Join-Path $repoRoot "models\\$preparedModel"
    if (Test-Path $repoModelPath) {
        Remove-Item $repoModelPath -Force
    }
    Copy-Item -Path $sourcePath -Destination (Join-Path $promotedModelsDir $preparedModel) -Force
    Copy-Item -Path $sourcePath -Destination $repoModelPath -Force

    $compiledContextPath = Join-Path $backendArtifactsDir (([System.IO.Path]::GetFileNameWithoutExtension($preparedModel)) + "_ctx.onnx")
    if (Test-Path $compiledContextPath) {
        $repoCompiledContextPath = Join-Path $repoRoot "models\\$([System.IO.Path]::GetFileName($compiledContextPath))"
        if (Test-Path $repoCompiledContextPath) {
            Remove-Item $repoCompiledContextPath -Force
        }
        Copy-Item -Path $compiledContextPath `
            -Destination (Join-Path $promotedModelsDir ([System.IO.Path]::GetFileName($compiledContextPath))) -Force
        Copy-Item -Path $compiledContextPath `
            -Destination $repoCompiledContextPath -Force
    }
}

Write-Host "[9/9] Packaging the RTX OFX release tracks..." -ForegroundColor Cyan
foreach ($variant in Get-CorridorKeyWindowsOfxReleaseVariants -Track "rtx") {
    Invoke-ExternalCommand -FilePath (Join-Path $repoRoot "scripts\\package_ofx_installer_windows.ps1") `
        -Arguments @(
            "-Version", $Version,
            "-ReleaseSuffix", $variant.Suffix,
            "-ModelProfile", $variant.ModelProfile,
            "-OrtRoot", $ortRoot,
            "-ModelsDir", $promotedModelsDir
        )

    $bundleValidationPath = Join-Path $repoRoot "dist\\CorridorKey_Resolve_v${Version}_Windows_$($variant.Suffix)\\bundle_validation.json"
    if (-not (Test-Path $bundleValidationPath)) {
        throw "Bundle validation report missing after packaging $($variant.Suffix): $bundleValidationPath"
    }

    $bundleValidation = Get-Content -Path $bundleValidationPath -Raw | ConvertFrom-Json
    if (-not $bundleValidation.doctor.healthy) {
        throw "Doctor reported unhealthy status for packaged track $($variant.Suffix). See $bundleValidationPath"
    }
}

$summary = [ordered]@{
    version = $Version
    deploy_profiles = [System.IO.Path]::GetFullPath($deployProfilesPath)
    checkpoint = $checkpointPath
    source_repo = $sourceRepo
    raw_onnx_dir = [System.IO.Path]::GetFullPath($rawOnnxDir)
    backend_artifacts_dir = [System.IO.Path]::GetFullPath($backendArtifactsDir)
    promoted_models_dir = [System.IO.Path]::GetFullPath($promotedModelsDir)
    validation_report = [System.IO.Path]::GetFullPath($validationReportPath)
    installers = [ordered]@{
        rtx_lite = [System.IO.Path]::GetFullPath((Join-Path $repoRoot "dist\\CorridorKey_Resolve_v${Version}_Windows_RTX_Lite_Installer.exe"))
        rtx_full = [System.IO.Path]::GetFullPath((Join-Path $repoRoot "dist\\CorridorKey_Resolve_v${Version}_Windows_RTX_Full_Installer.exe"))
    }
    bundle_validation = [ordered]@{
        rtx_lite = [System.IO.Path]::GetFullPath((Join-Path $repoRoot "dist\\CorridorKey_Resolve_v${Version}_Windows_RTX_Lite\\bundle_validation.json"))
        rtx_full = [System.IO.Path]::GetFullPath((Join-Path $repoRoot "dist\\CorridorKey_Resolve_v${Version}_Windows_RTX_Full\\bundle_validation.json"))
    }
}

$summary | ConvertTo-Json -Depth 5
