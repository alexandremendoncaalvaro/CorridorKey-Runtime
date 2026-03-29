param(
    [string]$SourceRepoName = "",
    [string]$RuntimeRepoName = "CorridorKey-Runtime",
    [switch]$Json
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Add-CheckResult {
    param(
        [System.Collections.Generic.List[object]]$Results,
        [string]$Name,
        [string]$Status,
        [string]$Detail
    )

    $Results.Add([pscustomobject]@{
            name = $Name
            status = $Status
            detail = $Detail
        }) | Out-Null
}

function Test-UsableCheckpointFile {
    param([string]$Path)

    if ([string]::IsNullOrWhiteSpace($Path) -or -not (Test-Path $Path)) {
        return $false
    }

    $fileInfo = Get-Item -Path $Path -ErrorAction Stop
    if ($fileInfo.Length -le 512) {
        $pointerHead = Get-Content -Path $Path -TotalCount 3 -ErrorAction SilentlyContinue
        if ($pointerHead -and (($pointerHead | Out-String) -match "https://git-lfs.github.com/spec/v1")) {
            return $false
        }
    }

    return $true
}

function Test-SourceRepoPath {
    param([string]$Path)

    return Test-Path (Join-Path $Path "CorridorKeyModule")
}

function Resolve-SourceRepoRoot {
    param(
        [string]$ParentRoot,
        [string]$ExplicitName
    )

    $candidateNames = @()
    if (-not [string]::IsNullOrWhiteSpace($ExplicitName)) {
        $candidateNames += $ExplicitName
    } else {
        $candidateNames += @("CorridorKey", "CorridorKey-Engine")
    }

    foreach ($candidateName in $candidateNames) {
        $candidateRoot = Join-Path $ParentRoot $candidateName
        if (Test-SourceRepoPath -Path $candidateRoot) {
            return [System.IO.Path]::GetFullPath($candidateRoot)
        }
    }

    return ""
}

function Resolve-RuntimeRepoRoot {
    param(
        [string]$ParentRoot,
        [string]$RepoName
    )

    $candidate = Join-Path $ParentRoot $RepoName
    if (Test-Path (Join-Path $candidate ".git")) {
        return [System.IO.Path]::GetFullPath($candidate)
    }

    return ""
}

function Resolve-CheckpointPath {
    param(
        [string]$SourceRepoRoot,
        [string]$RuntimeRepoRoot
    )

    $candidates = @()
    if (-not [string]::IsNullOrWhiteSpace($SourceRepoRoot)) {
        $candidates += @(
            (Join-Path $SourceRepoRoot "CorridorKeyModule\checkpoints\CorridorKey.pth"),
            (Join-Path $SourceRepoRoot "CorridorKeyModule\checkpoints\CorridorKey_v1.0.pth")
        )
    }
    if (-not [string]::IsNullOrWhiteSpace($RuntimeRepoRoot)) {
        $candidates += @(
            (Join-Path $RuntimeRepoRoot "models\CorridorKey.pth"),
            (Join-Path $RuntimeRepoRoot "models\CorridorKey_v1.0.pth")
        )
    }

    foreach ($candidate in ($candidates | Select-Object -Unique)) {
        if (Test-UsableCheckpointFile -Path $candidate) {
            return [System.IO.Path]::GetFullPath($candidate)
        }
    }

    return ""
}

function Test-CudaToolkitRoot {
    param([string]$CandidatePath)

    return (Test-Path (Join-Path $CandidatePath "bin\nvcc.exe")) -and
           (Test-Path (Join-Path $CandidatePath "include\cuda_runtime.h"))
}

function Resolve-CudaRoot {
    if (-not [string]::IsNullOrWhiteSpace($env:CUDA_PATH) -and
        (Test-CudaToolkitRoot -CandidatePath $env:CUDA_PATH)) {
        return [System.IO.Path]::GetFullPath($env:CUDA_PATH)
    }

    $cudaRoot = Join-Path ${env:ProgramFiles} "NVIDIA GPU Computing Toolkit\CUDA"
    if (Test-Path $cudaRoot) {
        $candidate = Get-ChildItem -Path $cudaRoot -Directory -Filter "v*" -ErrorAction SilentlyContinue |
            Sort-Object Name -Descending | Select-Object -First 1
        if ($null -ne $candidate -and (Test-CudaToolkitRoot -CandidatePath $candidate.FullName)) {
            return $candidate.FullName
        }
    }

    return ""
}

function Test-TensorRtRtxRoot {
    param([string]$CandidatePath)

    return (Test-Path (Join-Path $CandidatePath "include\NvInfer.h")) -and
           ((Get-ChildItem -Path (Join-Path $CandidatePath "bin") -Filter "tensorrt_rtx*.dll" -File -ErrorAction SilentlyContinue | Measure-Object).Count -gt 0)
}

function Resolve-TensorRtRtxRoot {
    param([string]$ParentRoot)

    if (-not [string]::IsNullOrWhiteSpace($env:TENSORRT_RTX_HOME) -and
        (Test-TensorRtRtxRoot -CandidatePath $env:TENSORRT_RTX_HOME)) {
        return [System.IO.Path]::GetFullPath($env:TENSORRT_RTX_HOME)
    }

    $vendorRoot = Join-Path $ParentRoot "CorridorKey-Runtime\vendor"
    if (Test-Path $vendorRoot) {
        foreach ($candidate in (Get-ChildItem -Path $vendorRoot -Directory -ErrorAction SilentlyContinue |
                Where-Object { $_.Name -match '^(TensorRT-RTX|tensorrt-rtx)' } |
                Sort-Object Name -Descending)) {
            if (Test-TensorRtRtxRoot -CandidatePath $candidate.FullName) {
                return $candidate.FullName
            }
        }
    }

    return ""
}

function Resolve-UvPath {
    $command = Get-Command "uv.exe" -ErrorAction SilentlyContinue
    if ($null -ne $command) {
        return $command.Source
    }

    $command = Get-Command "uv" -ErrorAction SilentlyContinue
    if ($null -ne $command) {
        return $command.Source
    }

    return ""
}

function Resolve-CmakePath {
    $command = Get-Command "cmake.exe" -ErrorAction SilentlyContinue
    if ($null -ne $command) {
        return $command.Source
    }

    $command = Get-Command "cmake" -ErrorAction SilentlyContinue
    if ($null -ne $command) {
        return $command.Source
    }

    return ""
}

function Resolve-GitPath {
    $command = Get-Command "git.exe" -ErrorAction SilentlyContinue
    if ($null -ne $command) {
        return $command.Source
    }

    $command = Get-Command "git" -ErrorAction SilentlyContinue
    if ($null -ne $command) {
        return $command.Source
    }

    return ""
}

function Resolve-MakeNsisPath {
    $command = Get-Command "makensis.exe" -ErrorAction SilentlyContinue
    if ($null -ne $command) {
        return $command.Source
    }

    foreach ($candidate in @(
            "C:\Program Files (x86)\NSIS\makensis.exe",
            "C:\Program Files (x86)\NSIS\Bin\makensis.exe"
        )) {
        if (Test-Path $candidate) {
            return $candidate
        }
    }

    return ""
}

function Resolve-Python312 {
    $pyLauncher = Get-Command "py.exe" -ErrorAction SilentlyContinue
    if ($null -ne $pyLauncher) {
        $resolved = & $pyLauncher.Source -3.12 -c "import sys; print(sys.executable)" 2>$null
        if ($LASTEXITCODE -eq 0 -and -not [string]::IsNullOrWhiteSpace($resolved)) {
            return ($resolved | Out-String).Trim()
        }
    }

    $python = Get-Command "python.exe" -ErrorAction SilentlyContinue
    if ($null -ne $python) {
        $version = & $python.Source -c "import sys; print(f'{sys.version_info.major}.{sys.version_info.minor}')" 2>$null
        if ($LASTEXITCODE -eq 0 -and ($version | Out-String).Trim() -eq "3.12") {
            return $python.Source
        }
    }

    return ""
}

function Resolve-VsDevCmd {
    if (-not [string]::IsNullOrWhiteSpace($env:VSINSTALLDIR)) {
        $candidate = Join-Path $env:VSINSTALLDIR "Common7\Tools\VsDevCmd.bat"
        if (Test-Path $candidate) {
            return $candidate
        }
    }

    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) {
        return ""
    }

    $installationPath = & $vswhere -latest -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2>$null
    if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($installationPath)) {
        return ""
    }

    $candidate = Join-Path $installationPath.Trim() "Common7\Tools\VsDevCmd.bat"
    if (Test-Path $candidate) {
        return $candidate
    }

    return ""
}

function Resolve-RtxRuntimeRoot {
    param([string]$RuntimeRepoRoot)

    if ([string]::IsNullOrWhiteSpace($RuntimeRepoRoot)) {
        return ""
    }

    $candidate = Join-Path $RuntimeRepoRoot "vendor\onnxruntime-windows-rtx"
    if (Test-Path $candidate) {
        return [System.IO.Path]::GetFullPath($candidate)
    }

    return ""
}

function Get-GpuSummary {
    $gpus = [System.Collections.Generic.List[object]]::new()

    $nvidiaSmi = Get-Command "nvidia-smi.exe" -ErrorAction SilentlyContinue
    if ($null -ne $nvidiaSmi) {
        $lines = & $nvidiaSmi.Source --query-gpu=name,memory.total,driver_version --format=csv,noheader,nounits 2>$null
        if ($LASTEXITCODE -eq 0 -and $lines) {
            foreach ($line in $lines) {
                $parts = $line -split ","
                if ($parts.Count -ge 3) {
                    $gpus.Add([pscustomobject]@{
                            name = $parts[0].Trim()
                            memory_mb = [int]($parts[1].Trim())
                            driver_version = $parts[2].Trim()
                        }) | Out-Null
                }
            }
            return @($gpus)
        }
    }

    foreach ($gpu in Get-CimInstance Win32_VideoController -ErrorAction SilentlyContinue) {
        $ramMb = 0
        if ($gpu.AdapterRAM) {
            $ramMb = [int]([math]::Round($gpu.AdapterRAM / 1MB))
        }
        $gpus.Add([pscustomobject]@{
                name = $gpu.Name
                memory_mb = $ramMb
                driver_version = $gpu.DriverVersion
            }) | Out-Null
    }

    return @($gpus)
}

$results = [System.Collections.Generic.List[object]]::new()
$parentRoot = (Get-Location).Path
$sourceRepoRoot = Resolve-SourceRepoRoot -ParentRoot $parentRoot -ExplicitName $SourceRepoName
$runtimeRepoRoot = Resolve-RuntimeRepoRoot -ParentRoot $parentRoot -RepoName $RuntimeRepoName
$checkpointPath = Resolve-CheckpointPath -SourceRepoRoot $sourceRepoRoot -RuntimeRepoRoot $runtimeRepoRoot
$gitPath = Resolve-GitPath
$uvPath = Resolve-UvPath
$cmakePath = Resolve-CmakePath
$nsisPath = Resolve-MakeNsisPath
$python312Path = Resolve-Python312
$vsDevCmd = Resolve-VsDevCmd
$cudaRoot = Resolve-CudaRoot
$tensorRtRtxRoot = Resolve-TensorRtRtxRoot -ParentRoot $parentRoot
$rtxRuntimeRoot = Resolve-RtxRuntimeRoot -RuntimeRepoRoot $runtimeRepoRoot
$gpuSummary = @(Get-GpuSummary)

if (-not [string]::IsNullOrWhiteSpace($sourceRepoRoot)) {
    Add-CheckResult -Results $results -Name "source_repo" -Status "PASS" -Detail $sourceRepoRoot
} else {
    Add-CheckResult -Results $results -Name "source_repo" -Status "FAIL" -Detail "Expected a sibling CorridorKey or CorridorKey-Engine folder."
}

if (-not [string]::IsNullOrWhiteSpace($runtimeRepoRoot)) {
    Add-CheckResult -Results $results -Name "runtime_repo" -Status "INFO" -Detail $runtimeRepoRoot
} else {
    Add-CheckResult -Results $results -Name "runtime_repo" -Status "INFO" -Detail "CorridorKey-Runtime clone not present yet. The bootstrap script can clone it."
}

if (-not [string]::IsNullOrWhiteSpace($checkpointPath)) {
    Add-CheckResult -Results $results -Name "checkpoint" -Status "PASS" -Detail $checkpointPath
} else {
    Add-CheckResult -Results $results -Name "checkpoint" -Status "FAIL" -Detail "No usable CorridorKey checkpoint was found."
}

foreach ($commandCheck in @(
        @{ name = "git"; path = $gitPath },
        @{ name = "uv"; path = $uvPath },
        @{ name = "cmake"; path = $cmakePath },
        @{ name = "nsis"; path = $nsisPath }
    )) {
    if (-not [string]::IsNullOrWhiteSpace($commandCheck.path)) {
        Add-CheckResult -Results $results -Name $commandCheck.name -Status "PASS" -Detail $commandCheck.path
    } else {
        Add-CheckResult -Results $results -Name $commandCheck.name -Status "FAIL" -Detail "Not found."
    }
}

if (-not [string]::IsNullOrWhiteSpace($env:VCPKG_ROOT) -and (Test-Path $env:VCPKG_ROOT)) {
    Add-CheckResult -Results $results -Name "vcpkg_root" -Status "PASS" -Detail $env:VCPKG_ROOT
} else {
    Add-CheckResult -Results $results -Name "vcpkg_root" -Status "FAIL" -Detail "VCPKG_ROOT is not set or does not exist."
}

if ($gpuSummary.Count -gt 0) {
    $gpuText = $gpuSummary | ForEach-Object {
        "$($_.name) ($($_.memory_mb) MB, driver $($_.driver_version))"
    }
    Add-CheckResult -Results $results -Name "gpu" -Status "INFO" -Detail ($gpuText -join "; ")
} else {
    Add-CheckResult -Results $results -Name "gpu" -Status "WARN" -Detail "No GPU information was detected."
}

$certificationGpu = @($gpuSummary | Where-Object {
        $_.memory_mb -ge 24000 -and $_.name -match "RTX"
    } | Select-Object -First 1)
if ($certificationGpu.Count -gt 0) {
    Add-CheckResult -Results $results -Name "certification_gpu" -Status "PASS" -Detail $certificationGpu[0].name
} else {
    Add-CheckResult -Results $results -Name "certification_gpu" -Status "FAIL" -Detail "No RTX GPU with at least 24 GB VRAM was detected."
}

if (-not [string]::IsNullOrWhiteSpace($rtxRuntimeRoot)) {
    Add-CheckResult -Results $results -Name "rtx_runtime_bundle" -Status "PASS" -Detail $rtxRuntimeRoot
} else {
    Add-CheckResult -Results $results -Name "rtx_runtime_bundle" -Status "WARN" -Detail "Curated vendor\\onnxruntime-windows-rtx is not present. The collaborator flow will try to stage it."

    foreach ($buildPrereq in @(
            @{ name = "python312"; value = $python312Path; ok = (-not [string]::IsNullOrWhiteSpace($python312Path)); detail = if (-not [string]::IsNullOrWhiteSpace($python312Path)) { $python312Path } else { "Python 3.12 not found." } },
            @{ name = "vs_toolchain"; value = $vsDevCmd; ok = (-not [string]::IsNullOrWhiteSpace($vsDevCmd)); detail = if (-not [string]::IsNullOrWhiteSpace($vsDevCmd)) { $vsDevCmd } else { "Visual Studio C++ toolchain not found." } },
            @{ name = "cuda"; value = $cudaRoot; ok = (-not [string]::IsNullOrWhiteSpace($cudaRoot)); detail = if (-not [string]::IsNullOrWhiteSpace($cudaRoot)) { $cudaRoot } else { "CUDA toolkit not found." } },
            @{ name = "tensorrt_rtx"; value = $tensorRtRtxRoot; ok = (-not [string]::IsNullOrWhiteSpace($tensorRtRtxRoot)); detail = if (-not [string]::IsNullOrWhiteSpace($tensorRtRtxRoot)) { $tensorRtRtxRoot } else { "TensorRT RTX SDK not found." } }
        )) {
        $status = if ($buildPrereq.ok) { "PASS" } else { "FAIL" }
        Add-CheckResult -Results $results -Name $buildPrereq.name -Status $status -Detail $buildPrereq.detail
    }
}

$failedChecks = @($results | Where-Object { $_.status -eq "FAIL" })
$warningChecks = @($results | Where-Object { $_.status -eq "WARN" })
$overallStatus = if ($failedChecks.Count -gt 0) { "FAIL" } elseif ($warningChecks.Count -gt 0) { "WARN" } else { "PASS" }

$payload = [ordered]@{
    overall_status = $overallStatus
    parent_root = $parentRoot
    source_repo = $sourceRepoRoot
    runtime_repo = $runtimeRepoRoot
    checkpoint = $checkpointPath
    results = @($results)
}

if ($Json.IsPresent) {
    $payload | ConvertTo-Json -Depth 6
    exit 0
}

Write-Host "[preflight] Parent folder: $parentRoot" -ForegroundColor Cyan
if (-not [string]::IsNullOrWhiteSpace($sourceRepoRoot)) {
    Write-Host "[preflight] Source repo: $sourceRepoRoot" -ForegroundColor Cyan
}
if (-not [string]::IsNullOrWhiteSpace($runtimeRepoRoot)) {
    Write-Host "[preflight] Runtime repo: $runtimeRepoRoot" -ForegroundColor Cyan
}

foreach ($result in $results) {
    $color = switch ($result.status) {
        "PASS" { "Green" }
        "WARN" { "Yellow" }
        "FAIL" { "Red" }
        default { "Cyan" }
    }
    Write-Host ("[{0}] {1}: {2}" -f $result.status, $result.name, $result.detail) -ForegroundColor $color
}

Write-Host ""
$overallColor = switch ($overallStatus) {
    "PASS" { "Green" }
    "WARN" { "Yellow" }
    default { "Red" }
}
Write-Host ("[preflight] Overall status: {0}" -f $overallStatus) -ForegroundColor $overallColor
