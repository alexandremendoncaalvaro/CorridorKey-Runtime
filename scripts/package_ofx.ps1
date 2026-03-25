param(
    [string]$BuildDir = "",
    [string]$OrtRoot = "",
    [string]$ModelsDir = "",
    [string]$OutputDir = "",
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
    # Copy essential TensorRT-RTX support libs
    Copy-OrtDll -Root $OrtRoot -Name "tensorrt_onnxparser_rtx_1_3.dll" -DestinationDir $win64Dir
    Copy-OrtDll -Root $OrtRoot -Name "tensorrt_rtx_1_3.dll" -DestinationDir $win64Dir
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

$targetModels = @(
    "corridorkey_fp16_512.onnx",
    "corridorkey_fp16_768.onnx",
    "corridorkey_fp16_1024.onnx",
    "corridorkey_fp16_1536.onnx",
    "corridorkey_int8_512.onnx",
    "corridorkey_int8_768.onnx",
    "corridorkey_int8_1024.onnx"
)
if (-not $Skip2048.IsPresent) {
    $targetModels += "corridorkey_fp16_2048.onnx"
}
foreach ($model in $targetModels) {
    $sourcePath = Join-Path $ModelsDir $model
    Assert-FileExists -Path $sourcePath -Message "Missing model: $sourcePath"
    Copy-Item $sourcePath $resourcesDir -Force
}

Write-Host "OpenFX bundle ready at: $OutputDir"
