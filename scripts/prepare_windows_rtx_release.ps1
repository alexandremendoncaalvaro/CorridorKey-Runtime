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

function Get-ProjectVersion {
    param([string]$RepoRoot)

    $cmakePath = Join-Path $RepoRoot "CMakeLists.txt"
    if (-not (Test-Path $cmakePath)) {
        throw "Could not determine project version because CMakeLists.txt was not found at $cmakePath"
    }

    $versionLine = Select-String -Path $cmakePath -Pattern '^\s*VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)\s*$'
    if ($null -ne $versionLine) {
        return $versionLine.Matches[0].Groups[1].Value
    }

    throw "Could not determine project version from $cmakePath"
}

if ([string]::IsNullOrWhiteSpace($Version)) {
    $Version = Get-ProjectVersion -RepoRoot $repoRoot
}
if ([string]::IsNullOrWhiteSpace($BuildDir)) {
    $BuildDir = Join-Path $repoRoot ("build\" + $BuildPreset)
}
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

function Assert-RequiredModels {
    $requiredModels = @(
        "corridorkey_fp16_768.onnx",
        "corridorkey_fp16_1024.onnx",
        "corridorkey_int8_512.onnx"
    )

    foreach ($model in $requiredModels) {
        $modelPath = Join-Path $modelsDir $model
        if (-not (Test-Path $modelPath)) {
            throw "Missing required prepared model: $modelPath"
        }
    }
}

function Invoke-ModelPreparation {
    param(
        [string]$UvPath,
        [string]$SourceRepo,
        [string]$CheckpointPath,
        [switch]$Force
    )

    $toolDir = Join-Path $repoRoot "tools\model_exporter"
    $baseModelPath = Join-Path $modelsDir "corridorkey_fp32.onnx"

    $needsPreparation = $Force.IsPresent
    if (-not $needsPreparation) {
        foreach ($expected in @("corridorkey_fp16_768.onnx", "corridorkey_fp16_1024.onnx", "corridorkey_int8_512.onnx")) {
            if (-not (Test-Path (Join-Path $modelsDir $expected))) {
                $needsPreparation = $true
                break
            }
        }
    }

    if (-not $needsPreparation) {
        Write-Host "[1/5] Reusing prepared Windows RTX model pack from $modelsDir"
        return
    }

    Write-Host "[1/5] Exporting CorridorKey ONNX models..."
    Invoke-ExternalCommand -FilePath $UvPath -WorkingDirectory $toolDir -Arguments @(
        "run", "python", "export_onnx.py",
        "--ckpt", $CheckpointPath,
        "--out", $baseModelPath,
        "--repo-path", $SourceRepo
    )

    Write-Host "[2/5] Optimizing FP32 and generating FP16 variants..."
    Invoke-ExternalCommand -FilePath $UvPath -WorkingDirectory $toolDir -Arguments @(
        "run", "python", "optimize_model.py",
        "--dir", $modelsDir,
        "--target", "windows-rtx"
    )

    Write-Host "[3/5] Quantizing CPU fallback models..."
    Invoke-ExternalCommand -FilePath $UvPath -WorkingDirectory $toolDir -Arguments @(
        "run", "python", "quantize_model.py",
        "--dir", $modelsDir
    )

    Assert-RequiredModels
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
    Assert-RequiredModels
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
        $bundleRoot = Join-Path $repoRoot "dist\CorridorKey_Windows_v$Version"
        Write-Host "[validate] Running the packaged smoke test..."
        Invoke-ExternalCommand -FilePath (Join-Path $bundleRoot "smoke_test.bat") -WorkingDirectory $bundleRoot `
            -Arguments @()
    }
}

$summary = [ordered]@{
    version = $Version
    models_dir = [System.IO.Path]::GetFullPath($modelsDir)
    build_dir = [System.IO.Path]::GetFullPath($buildDir)
    ort_install_dir = [System.IO.Path]::GetFullPath($ortInstallDir)
    packaged_bundle_dir = [System.IO.Path]::GetFullPath((Join-Path $repoRoot "dist\CorridorKey_Windows_v$Version"))
    packaged_bundle_zip = [System.IO.Path]::GetFullPath((Join-Path $repoRoot "dist\CorridorKey_Windows_v$Version.zip"))
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
