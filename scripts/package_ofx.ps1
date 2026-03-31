param(
    [string]$BuildDir = "",
    [string]$OrtRoot = "",
    [string]$ModelsDir = "",
    [string]$OutputDir = "",
    [string]$ArtifactManifestPath = "",
    [ValidateSet("rtx-lite", "rtx-stable", "rtx-full", "windows-universal")]
    [string]$ModelProfile = "rtx-full",
    [switch]$AllowUncertifiedTensorRtContexts,
    [switch]$Skip2048
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot "windows_runtime_helpers.ps1")

if ([string]::IsNullOrWhiteSpace($BuildDir)) {
    $BuildDir = Join-Path $repoRoot "build\release"
}
$OrtRoot = Resolve-CorridorKeyWindowsOrtRoot -RepoRoot $repoRoot -ExplicitRoot $OrtRoot -PreferredTrack "rtx"
if ([string]::IsNullOrWhiteSpace($ModelsDir)) {
    $ModelsDir = Join-Path $repoRoot "models"
}
if ([string]::IsNullOrWhiteSpace($OutputDir)) {
    $OutputDir = Join-Path $repoRoot "dist\CorridorKey.ofx.bundle"
}

$pluginBinary = Join-Path $BuildDir "src\plugins\ofx\CorridorKey.ofx"
$runtimeServerBinary = Join-Path $BuildDir "src\cli\corridorkey.exe"
$win64Dir = Join-Path $OutputDir "Contents\Win64"
$resourcesDir = Join-Path $OutputDir "Contents\Resources\models"
$modelInventoryPath = Join-Path $OutputDir "model_inventory.json"
$artifactManifestOutputPath = Join-Path $OutputDir "artifact_manifest.json"

function Assert-FileExists {
    param([string]$Path, [string]$Message)
    if (-not (Test-Path $Path)) {
        throw $Message
    }
}

function Resolve-OrtDllPath {
    param([string]$Root, [string]$Name)
    $path1 = Join-Path $Root $Name
    $path2 = Join-Path (Join-Path $Root "bin") $Name
    $path3 = Join-Path (Join-Path $Root "lib") $Name
    foreach ($candidate in @($path1, $path2, $path3)) {
        if (Test-Path $candidate) {
            return $candidate
        }
    }
    return $null
}

function Copy-OrtDll {
    param([string]$Root, [string]$Name, [string]$DestinationDir)
    $resolved = Resolve-OrtDllPath -Root $Root -Name $Name
    if (-not $resolved) {
        throw "Required runtime DLL not found: $Name (searched under $Root)"
    }
    Copy-Item $resolved $DestinationDir -Force
}

function Copy-OrtDllIfPresent {
    param([string]$Root, [string]$Name, [string]$DestinationDir)
    $resolved = Resolve-OrtDllPath -Root $Root -Name $Name
    if (-not $resolved) {
        return $false
    }
    Copy-Item $resolved $DestinationDir -Force
    return $true
}

function Resolve-OrtDllByPattern {
    param([string]$Root, [string]$Pattern)

    $searchRoots = @(
        $Root,
        (Join-Path $Root "bin"),
        (Join-Path $Root "lib")
    )

    $matches = @()
    foreach ($searchRoot in $searchRoots) {
        if (-not (Test-Path $searchRoot)) {
            continue
        }
        $matches += Get-ChildItem -Path $searchRoot -Filter $Pattern -File -ErrorAction SilentlyContinue
    }

    return $matches |
        Sort-Object -Property Name -Descending |
        Select-Object -First 1
}

function Copy-OrtDllByPattern {
    param([string]$Root, [string]$Pattern, [string]$DestinationDir)

    $resolved = Resolve-OrtDllByPattern -Root $Root -Pattern $Pattern
    if ($null -eq $resolved) {
        throw "Required runtime DLL not found matching pattern '$Pattern' (searched under $Root)"
    }

    Copy-Item $resolved.FullName $DestinationDir -Force
    return $resolved.Name
}

function Get-RuntimeSupportedBackends {
    param([string]$RuntimeDir)

    $runtimeBinary = Join-Path $RuntimeDir "corridorkey.exe"
    if (-not (Test-Path $runtimeBinary)) {
        return @()
    }

    Push-Location $RuntimeDir
    try {
        $json = & $runtimeBinary info --json 2>$null
        if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($json)) {
            return @()
        }

        $parsed = $json | ConvertFrom-Json
        if ($null -eq $parsed.capabilities -or $null -eq $parsed.capabilities.supported_backends) {
            return @()
        }

        return @($parsed.capabilities.supported_backends)
    } catch {
        return @()
    } finally {
        Pop-Location
    }
}

function Test-RuntimeBackendSupport {
    param(
        [string]$RuntimeDir,
        [string]$RequiredBackend
    )

    $runtimeBinary = Join-Path $RuntimeDir "corridorkey.exe"
    if (-not (Test-Path $runtimeBinary)) {
        return $false
    }

    Push-Location $RuntimeDir
    try {
        $json = & $runtimeBinary info --json 2>$null
        if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($json)) {
            return $false
        }

        $parsed = $json | ConvertFrom-Json
        if ($null -eq $parsed.capabilities -or $null -eq $parsed.capabilities.supported_backends) {
            return $false
        }

        return @($parsed.capabilities.supported_backends) -contains $RequiredBackend
    } catch {
        return $false
    } finally {
        Pop-Location
    }
}

function Ensure-TensorRtCompiledContext {
    param(
        [string]$RuntimeDir,
        [string]$ModelPath,
        [string]$OutputPath
    )

    if (Test-Path $OutputPath) {
        return
    }

    $runtimeBinary = Join-Path $RuntimeDir "corridorkey.exe"
    if (-not (Test-Path $runtimeBinary)) {
        throw "Cannot compile TensorRT context because $runtimeBinary was not found."
    }

    $envPathOld = $env:PATH
    Push-Location $RuntimeDir
    try {
        $env:PATH = "$RuntimeDir;$envPathOld"
        $json = & $runtimeBinary `
            compile-context `
            --model $ModelPath `
            --output $OutputPath `
            --device tensorrt `
            --json 2>$null
        $exitCode = $LASTEXITCODE
        $parsed = $null
        if (-not [string]::IsNullOrWhiteSpace($json)) {
            try {
                $parsed = $json | ConvertFrom-Json
            } catch {
                $parsed = $null
            }
        }

        if ($exitCode -ne 0) {
            if ($null -ne $parsed -and $parsed.PSObject.Properties.Match("error").Count -gt 0) {
                throw [string]$parsed.error
            }
            throw "compile-context failed for $(Split-Path -Leaf $ModelPath)"
        }

        if ($null -eq $parsed) {
            throw "compile-context did not return valid JSON for $(Split-Path -Leaf $ModelPath)"
        }

        if (-not $parsed.success) {
            $errorMessage = if ($parsed.PSObject.Properties.Match("error").Count -gt 0) {
                [string]$parsed.error
            } else {
                "compile-context returned success=false"
            }
            throw $errorMessage
        }
    } finally {
        $env:PATH = $envPathOld
        Pop-Location
    }

    if (-not (Test-Path $OutputPath)) {
        throw "compile-context did not produce the expected output file: $OutputPath"
    }
}

if (Test-Path $OutputDir) {
    Remove-Item $OutputDir -Recurse -Force
}

New-Item -ItemType Directory -Path $win64Dir -Force | Out-Null
New-Item -ItemType Directory -Path $resourcesDir -Force | Out-Null

Assert-FileExists -Path $pluginBinary -Message "OpenFX plugin binary not found at $pluginBinary"
Assert-FileExists -Path $runtimeServerBinary -Message "Runtime server binary not found at $runtimeServerBinary"
Copy-Item $pluginBinary $win64Dir -Force
Copy-Item $runtimeServerBinary $win64Dir -Force

Write-Host "Staging ONNX Runtime core DLLs from $OrtRoot" -ForegroundColor Cyan
Copy-OrtDll -Root $OrtRoot -Name "onnxruntime.dll" -DestinationDir $win64Dir
Copy-OrtDll -Root $OrtRoot -Name "onnxruntime_providers_shared.dll" -DestinationDir $win64Dir
Copy-OrtDllIfPresent -Root $OrtRoot -Name "DirectML.dll" -DestinationDir $win64Dir | Out-Null

$isRtxRuntime = $OrtRoot -match "rtx"

$copiedOptionalGpuProvider = $false
foreach ($provider in @(
        "onnxruntime_providers_winml.dll",
        "onnxruntime_providers_openvino.dll"
    )) {
    if (Copy-OrtDllIfPresent -Root $OrtRoot -Name $provider -DestinationDir $win64Dir) {
        Write-Host "Copied optional runtime DLL: $provider"
        $copiedOptionalGpuProvider = $true
    }
}
if (-not $copiedOptionalGpuProvider) {
    # Check if DirectML is available even if no separate provider DLL exists
    # (since it can be built into onnxruntime.dll in some packages)
    $supportedBackends = Get-RuntimeSupportedBackends -RuntimeDir $win64Dir
    $hasOptionalGpuRuntime = $supportedBackends -contains "dml" -or
        $supportedBackends -contains "winml" -or
        $supportedBackends -contains "openvino"
    if (-not $hasOptionalGpuRuntime) {
        if (Test-Path (Join-Path $win64Dir "DirectML.dll")) {
             Write-Host "DirectML.dll is present, assuming DirectML support is built into onnxruntime.dll"
             $hasOptionalGpuRuntime = $true
        }
    }

    if (-not $hasOptionalGpuRuntime) {
        if ($isRtxRuntime) {
            Write-Host "RTX runtime intentionally omits DirectML/WinML/OpenVINO provider support." -ForegroundColor Cyan
        } else {
            Write-Host "No DirectML/WinML/OpenVINO runtime path was detected after staging $OrtRoot." -ForegroundColor Cyan
        }
    } else {
        Write-Host "Detected packaged optional GPU backend(s): $($supportedBackends -join ', ')"

        # Validate the staged bundle with the staged DLL set, not the developer machine.
        Write-Host "Validating packaged backends loadability..." -ForegroundColor Cyan
        $cliPath = Join-Path $win64Dir "corridorkey.exe"
        if (Test-Path $cliPath) {
            $envPathOld = $env:PATH
            try {
                # Temporarily add staging dir to PATH so it finds the DLLs
                $env:PATH = "$win64Dir;$envPathOld"
                $requiredBackend = if ($isRtxRuntime) { "tensorrt" } else { "dml" }

                if (Test-RuntimeBackendSupport -RuntimeDir $win64Dir -RequiredBackend $requiredBackend) {
                    Write-Host "[VERIFIED] $requiredBackend backend is functional in the package." -ForegroundColor Green
                } else {
                    throw "CRITICAL: Backend validation failed! 'corridorkey info --json' does not report $requiredBackend as supported."
                }
            } finally {
                $env:PATH = $envPathOld
            }
        }
    }
}

$tensorrtProvider = Resolve-OrtDllPath -Root $OrtRoot -Name "onnxruntime_providers_nv_tensorrt_rtx.dll"
if (-not $tensorrtProvider) {
    $tensorrtProvider = Resolve-OrtDllPath -Root $OrtRoot -Name "onnxruntime_providers_nvtensorrtrtx.dll"
}
$cudaProvider = Resolve-OrtDllPath -Root $OrtRoot -Name "onnxruntime_providers_cuda.dll"
if ($tensorrtProvider) {
    Copy-Item $tensorrtProvider $win64Dir -Force
    $onnxParserDll = Copy-OrtDllByPattern -Root $OrtRoot -Pattern "tensorrt_onnxparser_rtx_*.dll" -DestinationDir $win64Dir
    $runtimeDll = Copy-OrtDllByPattern -Root $OrtRoot -Pattern "tensorrt_rtx_*.dll" -DestinationDir $win64Dir
    Write-Host "Copied TensorRT-RTX support DLLs: $onnxParserDll, $runtimeDll"
}
if ($cudaProvider) {
    Copy-Item $cudaProvider $win64Dir -Force
}

$requiresCudaRuntime = ($null -ne $tensorrtProvider) -or ($null -ne $cudaProvider)
if ($requiresCudaRuntime) {
    $cudartCandidates = @()
    $rootBin = Join-Path $OrtRoot "bin"
    $rootLib = Join-Path $OrtRoot "lib"
    foreach ($candidateDir in @($OrtRoot, $rootBin, $rootLib)) {
        if (Test-Path $candidateDir) {
            $cudartCandidates += Get-ChildItem -Path $candidateDir -Filter "cudart64_*.dll" -File -ErrorAction SilentlyContinue
        }
    }
    if ($cudartCandidates.Count -eq 0) {
        throw "Required CUDA runtime DLL not found (cudart64_*.dll)."
    }
    foreach ($candidate in $cudartCandidates) {
        Copy-Item $candidate.FullName $win64Dir -Force
    }
} else {
    Write-Host "Skipping CUDA runtime staging because no CUDA/TensorRT provider was found."
}

if ($null -ne $tensorrtProvider) {
    Write-Host "Validating packaged TensorRT backend loadability..." -ForegroundColor Cyan
    $envPathOld = $env:PATH
    try {
        $env:PATH = "$win64Dir;$envPathOld"
        if (Test-RuntimeBackendSupport -RuntimeDir $win64Dir -RequiredBackend "tensorrt") {
            Write-Host "[VERIFIED] tensorrt backend is functional in the package." -ForegroundColor Green
        } else {
            throw "CRITICAL: TensorRT backend validation failed! 'corridorkey info --json' does not report tensorrt as supported."
        }
    } finally {
        $env:PATH = $envPathOld
    }
}

$targetModels = Get-CorridorKeyOfxBundleTargetModels -ModelProfile $ModelProfile
if ($Skip2048.IsPresent) {
    $targetModels = @($targetModels | Where-Object { $_ -ne "corridorkey_fp16_2048.onnx" })
}
$profileContract = Get-CorridorKeyModelProfileContract -ModelProfile $ModelProfile
$modelInventory = Get-CorridorKeyModelInventory -ModelsDir $ModelsDir -ExpectedModels $targetModels
$compiledContextModels = @()
$expectedCompiledContextModels = Get-CorridorKeyExpectedCompiledContextModels `
    -PresentModels $modelInventory.present_models `
    -ModelProfile $ModelProfile
$strictCertifiedRtxPackaging = $profileContract.expects_compiled_context_models -and
    (-not $AllowUncertifiedTensorRtContexts.IsPresent)
$artifactManifest = $null

if ($strictCertifiedRtxPackaging) {
    if ([string]::IsNullOrWhiteSpace($ArtifactManifestPath)) {
        $ArtifactManifestPath = Get-CorridorKeyWindowsRtxArtifactManifestPath -ModelsDir $ModelsDir
    }

    $artifactManifest = Assert-CorridorKeyWindowsRtxArtifactManifestHealthy `
        -ArtifactsDir $ModelsDir `
        -ExpectedModels $modelInventory.present_models `
        -ExpectedCompiledContextModels $expectedCompiledContextModels `
        -ArtifactManifestPath $ArtifactManifestPath `
        -Label "$ModelProfile package source"
}

foreach ($model in $modelInventory.present_models) {
    $sourcePath = Join-Path $ModelsDir $model
    Copy-Item $sourcePath $resourcesDir -Force

    $compiledContextName = ([System.IO.Path]::GetFileNameWithoutExtension($model)) + "_ctx.onnx"
    $compiledContextPath = Join-Path $ModelsDir $compiledContextName
    if (Test-Path $compiledContextPath) {
        Copy-Item $compiledContextPath $resourcesDir -Force
        $compiledContextModels += $compiledContextName
    }
}

if ($profileContract.expects_compiled_context_models -and -not $strictCertifiedRtxPackaging) {
    foreach ($compiledContextName in $expectedCompiledContextModels) {
        if ($compiledContextModels -contains $compiledContextName) {
            continue
        }

        $modelName = $compiledContextName -replace '_ctx\.onnx$', '.onnx'
        $stagedModelPath = Join-Path $resourcesDir $modelName
        $stagedCompiledContextPath = Join-Path $resourcesDir $compiledContextName
        Ensure-TensorRtCompiledContext `
            -RuntimeDir $win64Dir `
            -ModelPath $stagedModelPath `
            -OutputPath $stagedCompiledContextPath
        $compiledContextModels += $compiledContextName
    }
}

$missingCompiledContextModels = @(
    $expectedCompiledContextModels |
        Where-Object { $compiledContextModels -notcontains $_ }
)

$certificationContractIssues = @()
$certificationContractComplete = $false
if ($strictCertifiedRtxPackaging) {
    Copy-Item $ArtifactManifestPath $artifactManifestOutputPath -Force
    $stagedManifest = Assert-CorridorKeyWindowsRtxArtifactManifestHealthy `
        -ArtifactsDir $resourcesDir `
        -ExpectedModels $modelInventory.present_models `
        -ExpectedCompiledContextModels $expectedCompiledContextModels `
        -ArtifactManifestPath $artifactManifestOutputPath `
        -Label "$ModelProfile staged package"
    $artifactManifest = $stagedManifest
    $certificationContractComplete = $true
} elseif ($profileContract.expects_compiled_context_models) {
    $certificationContractIssues += "RTX packaging did not use a certified artifact manifest."
}

$inventoryPayload = [ordered]@{
    package_type = $profileContract.package_type
    model_profile = $profileContract.model_profile
    bundle_track = $profileContract.bundle_track
    release_label = $profileContract.release_label
    optimization_profile_id = $profileContract.optimization_profile_id
    optimization_profile_label = $profileContract.optimization_profile_label
    backend_intent = $profileContract.backend_intent
    fallback_policy = $profileContract.fallback_policy
    warmup_policy = $profileContract.warmup_policy
    certification_tier = $profileContract.certification_tier
    unrestricted_quality_attempt = $profileContract.unrestricted_quality_attempt
    models_dir = [System.IO.Path]::GetFullPath($ModelsDir)
    expected_models = @($modelInventory.expected_models)
    present_models = @($modelInventory.present_models)
    missing_models = @($modelInventory.missing_models)
    present_count = $modelInventory.present_count
    missing_count = $modelInventory.missing_count
    compiled_context_models = @($compiledContextModels)
    expected_compiled_context_models = @($expectedCompiledContextModels)
    missing_compiled_context_models = @($missingCompiledContextModels)
    compiled_context_complete = @($missingCompiledContextModels).Count -eq 0
    certification_manifest_present = $strictCertifiedRtxPackaging
    certification_contract_complete = $certificationContractComplete
    certification_contract_issues = @($certificationContractIssues)
}
Write-CorridorKeyJsonFile -Path $modelInventoryPath -Payload $inventoryPayload

if ($modelInventory.missing_count -gt 0) {
    Write-Host "[INFO] Packaging OFX bundle with partial model coverage: $($modelInventory.missing_models -join ', ')" -ForegroundColor Cyan
    Write-Host "[INFO] Wrote model inventory: $modelInventoryPath" -ForegroundColor Cyan
} else {
    Write-Host "[PASS] All targeted OFX models were packaged." -ForegroundColor Green
}

Write-Host "OpenFX bundle ready at: $OutputDir"
