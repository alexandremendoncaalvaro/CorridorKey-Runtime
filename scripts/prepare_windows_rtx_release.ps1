param(
    [string]$Version = "",
    [string]$BuildDir = "",
    [string]$OrtInstallDir = "",
    [string]$CorridorKeyRepo = "",
    [string]$Checkpoint = "",
    [string]$OrtSourceDir = "",
    [string]$OrtSourceRef = "v1.23.0",
    [string]$CudaHome = "",
    [string]$TensorRtRtxHome = "",
    [string]$VsDevCmd = "",
    [string]$PythonExe = "",
    [string]$Uv = "",
    [string]$BuildPreset = "release",
    [switch]$BootstrapOrtSource,
    [switch]$SkipModelPreparation,
    [switch]$SkipOrtBuild,
    [switch]$SkipRuntimeBuild,
    [switch]$SkipPackage,
    [switch]$SkipCompileContexts,
    [switch]$SkipBundleValidation,
    [switch]$ForceModelPreparation
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$modelsDir = Join-Path $repoRoot "models"
. (Join-Path $PSScriptRoot "windows_runtime_helpers.ps1")

$Version = Initialize-CorridorKeyVersion -RepoRoot $repoRoot -Version $Version
if ([string]::IsNullOrWhiteSpace($BuildDir)) {
    $BuildDir = Join-Path $repoRoot ("build\" + $BuildPreset)
}
$modelPackStatusPath = Join-Path $BuildDir "model_pack_status.json"
if ([string]::IsNullOrWhiteSpace($OrtInstallDir)) {
    $OrtInstallDir = Join-Path $repoRoot "vendor\onnxruntime-windows-rtx"
}

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
    foreach ($name in @("CorridorKey", "corridorkey", "CorridorKey-main")) {
        $candidates += Join-Path $parentRoot $name
    }

    if (Test-Path $parentRoot) {
        $candidates += Get-ChildItem -Path $parentRoot -Directory -ErrorAction SilentlyContinue |
            Where-Object { Test-Path (Join-Path $_.FullName "CorridorKeyModule") } |
            Select-Object -ExpandProperty FullName
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

    $checkpointsDir = Join-Path $SourceRepo "CorridorKeyModule\checkpoints"
    $candidates = @(
        (Join-Path $modelsDir "CorridorKey_v1.0.pth"),
        (Join-Path $modelsDir "CorridorKey.pth"),
        (Join-Path $checkpointsDir "CorridorKey_v1.0.pth"),
        (Join-Path $checkpointsDir "CorridorKey.pth")
    )

    foreach ($candidate in $candidates) {
        if (Test-Path $candidate) {
            return [System.IO.Path]::GetFullPath($candidate)
        }
    }

    $fallback = Get-ChildItem -Path $checkpointsDir -Filter "*.pth" -File -ErrorAction SilentlyContinue |
        Sort-Object Name | Select-Object -First 1
    if ($null -ne $fallback) {
        return $fallback.FullName
    }

    throw "No CorridorKey checkpoint was found under $checkpointsDir. Pass -Checkpoint explicitly."
}

function Resolve-OrtSourceDir {
    param(
        [string]$ExplicitPath,
        [switch]$Bootstrap,
        [string]$GitPath,
        [string]$OrtRef
    )

    $candidates = @()
    if (-not [string]::IsNullOrWhiteSpace($ExplicitPath)) {
        $candidates += $ExplicitPath
    }

    foreach ($candidate in @(
            (Join-Path $repoRoot "vendor\onnxruntime-src"),
            (Join-Path $repoRoot "vendor\onnxruntime-source")
        )) {
        $candidates += $candidate
    }

    foreach ($candidate in ($candidates | Select-Object -Unique)) {
        if ([string]::IsNullOrWhiteSpace($candidate)) {
            continue
        }
        $normalizedCandidate = [System.IO.Path]::GetFullPath($candidate)
        if (Test-Path (Join-Path $normalizedCandidate "build.bat")) {
            return $normalizedCandidate
        }
    }

    if (-not $Bootstrap.IsPresent) {
        throw "ONNX Runtime source checkout not found. Pass -OrtSourceDir or rerun with -BootstrapOrtSource."
    }

    $targetDir = Join-Path $repoRoot "vendor\onnxruntime-src"
    if (Test-Path $targetDir) {
        if (Test-Path (Join-Path $targetDir "build.bat")) {
            return [System.IO.Path]::GetFullPath($targetDir)
        }
        throw "Existing $targetDir is not an ONNX Runtime source checkout."
    }

    Write-Host "[bootstrap] Cloning ONNX Runtime $OrtRef into $targetDir ..."
    & $GitPath clone --branch $OrtRef --depth 1 https://github.com/microsoft/onnxruntime.git $targetDir
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to clone ONNX Runtime source."
    }

    return [System.IO.Path]::GetFullPath($targetDir)
}

function Invoke-ExternalCommand {
    param(
        [string]$FilePath,
        [string[]]$Arguments,
        [string]$WorkingDirectory = ""
    )

    $display = @($FilePath) + $Arguments
    Write-Host ("  > " + ($display -join " "))

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

function Remove-IntermediateModelsFromDestination {
    foreach ($model in (Get-CorridorKeyIntermediateModelList)) {
        $modelPath = Join-Path $modelsDir $model
        if (Test-Path $modelPath) {
            Remove-Item $modelPath -Force
        }
    }
}

function Get-PresentModelsInDirectory {
    param(
        [string]$Directory,
        [string[]]$ExpectedModels
    )

    return @($ExpectedModels | Where-Object { Test-Path (Join-Path $Directory $_) })
}

function Write-ModelPackStatus {
    param(
        [string]$Stage,
        [string]$Directory,
        [string[]]$ExpectedModels,
        [string[]]$ValidatedModels = @(),
        [string[]]$ParityModels = @(),
        [object[]]$GenerationFailures = @()
    )

    $inventory = Get-CorridorKeyModelInventory -ModelsDir $Directory -ExpectedModels $ExpectedModels
    $payload = [ordered]@{
        stage = $Stage
        models_dir = [System.IO.Path]::GetFullPath($Directory)
        expected_models = @($inventory.expected_models)
        present_models = @($inventory.present_models)
        missing_models = @($inventory.missing_models)
        present_count = $inventory.present_count
        missing_count = $inventory.missing_count
        missing_models_allowed = $true
        parity_validated_models = @($ParityModels)
        contract_validated_models = @($ValidatedModels)
        generation_failures = @($GenerationFailures)
    }
    Write-CorridorKeyJsonFile -Path $modelPackStatusPath -Payload $payload

    if ($inventory.missing_count -gt 0) {
        Write-Host "[INFO] Prepared model pack is partial: $($inventory.missing_models -join ', ')" -ForegroundColor Cyan
        Write-Host "[INFO] Wrote model pack status: $modelPackStatusPath" -ForegroundColor Cyan
    } else {
        Write-Host "[PASS] All prepared Windows RTX models are present." -ForegroundColor Green
    }
}

function Invoke-ModelPackValidation {
    param(
        [string]$UvPath,
        [string]$ToolDir,
        [string]$ModelsDirectory,
        [string[]]$Models
    )

    if ($Models.Count -eq 0) {
        Write-Host "[validate-models] No prepared runtime models are present. Skipping ONNX contract validation." -ForegroundColor Yellow
        return
    }

    $arguments = @(
        "run", "python", "validate_model_pack.py",
        "--dir", $ModelsDirectory,
        "--models"
    ) + $Models

    Write-Host "[validate-models] Verifying ONNX contracts and ORT loadability..."
    Invoke-ExternalCommand -FilePath $UvPath -WorkingDirectory $ToolDir -Arguments $arguments
}

function Invoke-ExportParityValidation {
    param(
        [string]$UvPath,
        [string]$ToolDir,
        [string]$CheckpointPath,
        [string]$SourceRepo,
        [string]$ExportDir,
        [string[]]$Models
    )

    if ($Models.Count -eq 0) {
        Write-Host "[validate-export] No runtime artifacts are present for parity validation. Skipping." -ForegroundColor Yellow
        return
    }

    $arguments = @(
        "run", "python", "validate_export_parity.py",
        "--ckpt", $CheckpointPath,
        "--dir", $ExportDir,
        "--repo-path", $SourceRepo,
        "--models"
    ) + $Models

    Write-Host "[validate-export] Verifying exported ONNX parity against the canonical PyTorch model..."
    Invoke-ExternalCommand -FilePath $UvPath -WorkingDirectory $ToolDir -Arguments $arguments
}

function Invoke-ModelPreparation {
    param(
        [string]$UvPath,
        [string]$SourceRepo,
        [string]$CheckpointPath,
        [switch]$Force
    )

    $toolDir = Join-Path $repoRoot "tools\model_exporter"
    $tempModelsDir = Join-Path $repoRoot "temp\windows-model-prep"
    $baseModelPath = Join-Path $tempModelsDir "corridorkey_fp32.onnx"
    $expectedPreparedModels = Get-CorridorKeyPreparedModelList
    $expectedIntermediateModels = Get-CorridorKeyIntermediateModelList

    $needsPreparation = $Force.IsPresent
    if (-not $needsPreparation) {
        foreach ($expected in $expectedPreparedModels) {
            if (-not (Test-Path (Join-Path $modelsDir $expected))) {
                $needsPreparation = $true
                break
            }
        }
    }

    if (-not $needsPreparation) {
        Write-Host "[1/6] Reusing prepared Windows RTX model pack from $modelsDir"
        Remove-IntermediateModelsFromDestination
        $presentPreparedModels = Get-PresentModelsInDirectory -Directory $modelsDir -ExpectedModels $expectedPreparedModels
        Write-Host "[2/6] Validating prepared runtime artifact parity..."
        Invoke-ExportParityValidation -UvPath $UvPath -ToolDir $toolDir `
            -CheckpointPath $CheckpointPath -SourceRepo $SourceRepo -ExportDir $modelsDir `
            -Models $presentPreparedModels
        Write-Host "[3/6] Verifying ONNX contracts and ORT loadability..."
        Invoke-ModelPackValidation -UvPath $UvPath -ToolDir $toolDir `
            -ModelsDirectory $modelsDir -Models $presentPreparedModels
        Write-ModelPackStatus -Stage "reuse" -Directory $modelsDir `
            -ExpectedModels $expectedPreparedModels -ValidatedModels $presentPreparedModels `
            -ParityModels $presentPreparedModels
        return
    }

    if (Test-Path $tempModelsDir) {
        Remove-Item $tempModelsDir -Recurse -Force
    }
    New-Item -ItemType Directory -Path $tempModelsDir | Out-Null

    foreach ($preparedModel in $expectedPreparedModels) {
        $preparedPath = Join-Path $modelsDir $preparedModel
        if (Test-Path $preparedPath) {
            Remove-Item $preparedPath -Force
        }
    }
    Remove-IntermediateModelsFromDestination

    $generationFailures = @()
    try {
        Write-Host "[1/6] Exporting CorridorKey ONNX models..." 
        $resolutions = @(512, 768, 1024, 1536, 2048)
        foreach ($resolution in $resolutions) {
            Write-Host "  -> Exporting $resolution px runtime artifact family..." -ForegroundColor Cyan
            try {
                Invoke-ExternalCommand -FilePath $UvPath -WorkingDirectory $toolDir -Arguments @(
                    "run", "python", "export_onnx.py",
                    "--ckpt", $CheckpointPath,
                    "--out", $baseModelPath,
                    "--resolutions", "$resolution",
                    "--static-resolutions", "1536", "2048",
                    "--repo-path", $SourceRepo
                )
            } catch {
                $generationFailures += [ordered]@{
                    stage = "export"
                    resolution = $resolution
                    error = $_.Exception.Message
                }
                Write-Host "[INFO] Export failed for ${resolution}px. Continuing with the remaining model set and recording the failure in model_pack_status.json." -ForegroundColor Cyan
            }
        }

        $presentIntermediateModels = Get-PresentModelsInDirectory -Directory $tempModelsDir `
            -ExpectedModels $expectedIntermediateModels

        Write-Host "[2/6] Validating exported FP32 ONNX parity..."
        Invoke-ExportParityValidation -UvPath $UvPath -ToolDir $toolDir `
            -CheckpointPath $CheckpointPath -SourceRepo $SourceRepo -ExportDir $tempModelsDir `
            -Models $presentIntermediateModels

        Write-Host "[3/6] Optimizing FP32 and generating FP16 variants..."
        Invoke-ExternalCommand -FilePath $UvPath -WorkingDirectory $toolDir -Arguments @(
            "run", "python", "optimize_model.py",
            "--dir", $tempModelsDir,
            "--target", "windows-rtx"
        )

        Write-Host "[4/6] Quantizing CPU fallback models..."
        Invoke-ExternalCommand -FilePath $UvPath -WorkingDirectory $toolDir -Arguments @(
            "run", "python", "quantize_model.py",
            "--dir", $tempModelsDir
        )

        Write-Host "[5/6] Validating prepared runtime artifact parity..."
        $presentPreparedModels = Get-PresentModelsInDirectory -Directory $tempModelsDir `
            -ExpectedModels $expectedPreparedModels
        Invoke-ExportParityValidation -UvPath $UvPath -ToolDir $toolDir `
            -CheckpointPath $CheckpointPath -SourceRepo $SourceRepo -ExportDir $tempModelsDir `
            -Models $presentPreparedModels

        Write-Host "[6/6] Validating prepared runtime models..."
        Invoke-ModelPackValidation -UvPath $UvPath -ToolDir $toolDir `
            -ModelsDirectory $tempModelsDir -Models $presentPreparedModels

        foreach ($preparedModel in $presentPreparedModels) {
            Copy-Item -Path (Join-Path $tempModelsDir $preparedModel) `
                -Destination (Join-Path $modelsDir $preparedModel) -Force
        }
    } finally {
        if (Test-Path $tempModelsDir) {
            Remove-Item $tempModelsDir -Recurse -Force
        }
    }

    $finalPresentModels = Get-PresentModelsInDirectory -Directory $modelsDir -ExpectedModels $expectedPreparedModels
    Write-ModelPackStatus -Stage "prepared" -Directory $modelsDir -ExpectedModels $expectedPreparedModels `
        -ValidatedModels $finalPresentModels -ParityModels $finalPresentModels `
        -GenerationFailures $generationFailures
}

$uvPath = ""
$sourceRepo = ""
$checkpointPath = ""
$gitPath = ""
$resolvedOrtSourceDir = ""

if (-not $SkipModelPreparation.IsPresent) {
    $uvPath = Resolve-CommandPath -ExplicitPath $Uv -CandidateNames @("uv.exe", "uv") `
        -ErrorMessage "uv was not found. Install uv or pass -Uv."
    $sourceRepo = Resolve-CorridorKeyRepo -ExplicitPath $CorridorKeyRepo
    $checkpointPath = Resolve-CheckpointPath -ExplicitPath $Checkpoint -SourceRepo $sourceRepo
    Invoke-ModelPreparation -UvPath $uvPath -SourceRepo $sourceRepo `
        -CheckpointPath $checkpointPath -Force:$ForceModelPreparation.IsPresent
} else {
    $uvPath = Resolve-CommandPath -ExplicitPath $Uv -CandidateNames @("uv.exe", "uv") `
        -ErrorMessage "uv was not found. Install uv or pass -Uv."
    $presentPreparedModels = Get-PresentModelsInDirectory -Directory $modelsDir `
        -ExpectedModels (Get-CorridorKeyPreparedModelList)
    Invoke-ModelPackValidation -UvPath $uvPath -ToolDir (Join-Path $repoRoot "tools\model_exporter") `
        -ModelsDirectory $modelsDir -Models $presentPreparedModels
    Write-ModelPackStatus -Stage "skip-model-preparation" -Directory $modelsDir `
        -ExpectedModels (Get-CorridorKeyPreparedModelList) -ValidatedModels $presentPreparedModels `
        -ParityModels @()
}

if (-not $SkipOrtBuild.IsPresent) {
    $gitPath = Resolve-CommandPath -ExplicitPath "" -CandidateNames @("git.exe", "git") `
        -ErrorMessage "git was not found. Install Git or skip ORT bootstrapping."
    $resolvedOrtSourceDir = Resolve-OrtSourceDir -ExplicitPath $OrtSourceDir `
        -Bootstrap:$BootstrapOrtSource.IsPresent -GitPath $gitPath -OrtRef $OrtSourceRef

    Write-Host "[4/5] Building curated ONNX Runtime for Windows RTX..."
    Invoke-ExternalCommand -FilePath (Join-Path $repoRoot "scripts\build_ort_windows_rtx.ps1") -Arguments @(
        "-OrtSourceDir", $resolvedOrtSourceDir,
        "-InstallDir", $ortInstallDir,
        "-CudaHome", $CudaHome,
        "-TensorRtRtxHome", $TensorRtRtxHome,
        "-VsDevCmd", $VsDevCmd,
        "-PythonExe", $PythonExe
    )
}

if (-not $SkipRuntimeBuild.IsPresent) {
    Write-Host "[5/5] Configuring and building CorridorKey with the curated runtime..."
    Invoke-ExternalCommand -FilePath "cmake" -Arguments @(
        "--preset", $BuildPreset,
        "-DCORRIDORKEY_WINDOWS_ORT_ROOT=$ortInstallDir"
    ) -WorkingDirectory $repoRoot
    Invoke-ExternalCommand -FilePath "cmake" -Arguments @(
        "--build", "--preset", $BuildPreset, "--parallel"
    ) -WorkingDirectory $repoRoot
}

if (-not $SkipPackage.IsPresent) {
    $forbiddenPaths = @(
        $repoRoot,
        $buildDir,
        $ortInstallDir,
        $sourceRepo,
        $(if (-not [string]::IsNullOrWhiteSpace($checkpointPath)) { Split-Path -Parent $checkpointPath }),
        $env:USERPROFILE,
        $env:LOCALAPPDATA,
        $CudaHome,
        $TensorRtRtxHome
    ) | Where-Object { -not [string]::IsNullOrWhiteSpace($_) } | Select-Object -Unique

    $packageArguments = @(
        "-Version", $Version,
        "-BuildDir", $buildDir,
        "-OrtRoot", $ortInstallDir
    )
    if (-not $SkipCompileContexts.IsPresent) {
        $packageArguments += "-CompileContexts"
    }
    foreach ($forbiddenPath in $forbiddenPaths) {
        $packageArguments += @("-ForbiddenPathPrefix", $forbiddenPath)
    }

    Write-Host "[package] Creating the portable Windows RTX bundle..."
    Invoke-ExternalCommand -FilePath (Join-Path $repoRoot "scripts\package_windows.ps1") `
        -Arguments $packageArguments

    if (-not $SkipBundleValidation.IsPresent) {
        $bundleRoot = Join-Path $repoRoot "dist\CorridorKey_Runtime_v${Version}_Windows"
        Write-Host "[validate] Running the packaged smoke test..."
        Invoke-ExternalCommand -FilePath (Join-Path $bundleRoot "smoke_test.bat") -WorkingDirectory $bundleRoot `
            -Arguments @()
    }
}

$summary = [ordered]@{
    version = $Version
    models_dir = [System.IO.Path]::GetFullPath($modelsDir)
    model_pack_status = [System.IO.Path]::GetFullPath($modelPackStatusPath)
    build_dir = [System.IO.Path]::GetFullPath($buildDir)
    ort_install_dir = [System.IO.Path]::GetFullPath($ortInstallDir)
    packaged_bundle_dir = [System.IO.Path]::GetFullPath((Join-Path $repoRoot "dist\CorridorKey_Runtime_v${Version}_Windows"))
    packaged_bundle_zip = [System.IO.Path]::GetFullPath((Join-Path $repoRoot "dist\CorridorKey_Runtime_v${Version}_Windows.zip"))
}
if (-not [string]::IsNullOrWhiteSpace($sourceRepo)) {
    $summary["corridorkey_repo"] = $sourceRepo
}
if (-not [string]::IsNullOrWhiteSpace($checkpointPath)) {
    $summary["checkpoint"] = $checkpointPath
}
if (-not [string]::IsNullOrWhiteSpace($resolvedOrtSourceDir)) {
    $summary["onnxruntime_source"] = $resolvedOrtSourceDir
}

$summary | ConvertTo-Json -Depth 3
