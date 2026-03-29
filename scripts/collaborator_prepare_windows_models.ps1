param(
    [string]$Version = "",
    [string]$Checkpoint = "",
    [string]$CorridorKeyRepo = "",
    [string]$CommitBranch = "",
    [string]$CommitMessage = "chore: regenerate windows rtx model pack",
    [string]$GitUserName = "",
    [string]$GitUserEmail = "",
    [switch]$CreateCommit,
    [switch]$Push,
    [switch]$BuildRelease,
    [switch]$SkipReleaseTests
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
. (Join-Path $repoRoot "scripts\windows_runtime_helpers.ps1")

function Invoke-RepoCommand {
    param(
        [string]$FilePath,
        [string[]]$Arguments
    )

    Write-Host ("  > " + ((@($FilePath) + $Arguments) -join " ")) -ForegroundColor DarkGray
    & $FilePath @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed: $FilePath"
    }
}

function Assert-InGitRepository {
    if (-not (Test-Path (Join-Path $repoRoot ".git"))) {
        throw "This script must be run from a CorridorKey-Runtime git clone."
    }
}

function New-OrSwitchArgument {
    param(
        [switch]$Condition,
        [string]$Value
    )

    if ($Condition.IsPresent) {
        return @($Value)
    }
    return @()
}

function Get-GitConfigValue {
    param(
        [string[]]$Arguments
    )

    $value = & git @Arguments 2>$null
    if ($LASTEXITCODE -ne 0) {
        return ""
    }
    return ($value | Out-String).Trim()
}

function Read-RequiredValue {
    param(
        [string]$Prompt
    )

    while ($true) {
        $value = (Read-Host -Prompt $Prompt).Trim()
        if (-not [string]::IsNullOrWhiteSpace($value)) {
            return $value
        }
        Write-Host "[collaborator] A value is required to continue." -ForegroundColor Yellow
    }
}

function Ensure-GitIdentity {
    param(
        [string]$PreferredName,
        [string]$PreferredEmail
    )

    $localName = Get-GitConfigValue -Arguments @("config", "--local", "--get", "user.name")
    $localEmail = Get-GitConfigValue -Arguments @("config", "--local", "--get", "user.email")
    $globalName = Get-GitConfigValue -Arguments @("config", "--global", "--get", "user.name")
    $globalEmail = Get-GitConfigValue -Arguments @("config", "--global", "--get", "user.email")

    $resolvedName = $PreferredName
    if ([string]::IsNullOrWhiteSpace($resolvedName)) {
        $resolvedName = $localName
    }
    if ([string]::IsNullOrWhiteSpace($resolvedName)) {
        $resolvedName = $globalName
    }

    $resolvedEmail = $PreferredEmail
    if ([string]::IsNullOrWhiteSpace($resolvedEmail)) {
        $resolvedEmail = $localEmail
    }
    if ([string]::IsNullOrWhiteSpace($resolvedEmail)) {
        $resolvedEmail = $globalEmail
    }

    if ([string]::IsNullOrWhiteSpace($resolvedName)) {
        Write-Host "[collaborator] Git user.name is not configured. This script will set it for this CorridorKey-Runtime clone only." -ForegroundColor Yellow
        $resolvedName = Read-RequiredValue -Prompt "Enter your Git name"
    }

    if ([string]::IsNullOrWhiteSpace($resolvedEmail)) {
        Write-Host "[collaborator] Git user.email is not configured. This script will set it for this CorridorKey-Runtime clone only." -ForegroundColor Yellow
        $resolvedEmail = Read-RequiredValue -Prompt "Enter your Git email"
    }

    if ($localName -ne $resolvedName) {
        Invoke-RepoCommand -FilePath "git" -Arguments @("config", "--local", "user.name", $resolvedName)
    }
    if ($localEmail -ne $resolvedEmail) {
        Invoke-RepoCommand -FilePath "git" -Arguments @("config", "--local", "user.email", $resolvedEmail)
    }

    Write-Host "[collaborator] Git identity ready for local commit: $resolvedName <$resolvedEmail>" -ForegroundColor Cyan
}

function Invoke-CollaboratorWorkingCommand {
    param(
        [string]$FilePath,
        [string[]]$Arguments,
        [string]$WorkingDirectory = ""
    )

    Write-Host ("  > " + ((@($FilePath) + $Arguments) -join " ")) -ForegroundColor DarkGray

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

function Test-CorridorKeyUsableCheckpointFile {
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

function Resolve-CollaboratorCheckpointPath {
    param(
        [string]$ExplicitPath,
        [string]$SourceRepo
    )

    $candidates = @()
    if (-not [string]::IsNullOrWhiteSpace($ExplicitPath)) {
        $candidates += $ExplicitPath
    }
    if (-not [string]::IsNullOrWhiteSpace($SourceRepo)) {
        $candidates += @(
            (Join-Path $SourceRepo "CorridorKeyModule\checkpoints\CorridorKey.pth"),
            (Join-Path $SourceRepo "CorridorKeyModule\checkpoints\CorridorKey_v1.0.pth")
        )
    }
    $candidates += @(
        (Join-Path $repoRoot "models\CorridorKey.pth"),
        (Join-Path $repoRoot "models\CorridorKey_v1.0.pth")
    )

    foreach ($candidate in ($candidates | Where-Object { -not [string]::IsNullOrWhiteSpace($_) } | Select-Object -Unique)) {
        if (Test-CorridorKeyUsableCheckpointFile -Path $candidate) {
            return [System.IO.Path]::GetFullPath($candidate)
        }
    }

    return ""
}

function Test-CollaboratorCudaRoot {
    param([string]$CandidatePath)

    return (Test-Path (Join-Path $CandidatePath "bin\nvcc.exe")) -and
           (Test-Path (Join-Path $CandidatePath "include\cuda_runtime.h"))
}

function Resolve-CollaboratorCudaRoot {
    if (-not [string]::IsNullOrWhiteSpace($env:CUDA_PATH) -and (Test-CollaboratorCudaRoot -CandidatePath $env:CUDA_PATH)) {
        return [System.IO.Path]::GetFullPath($env:CUDA_PATH)
    }

    $cudaRoot = Join-Path ${env:ProgramFiles} "NVIDIA GPU Computing Toolkit\CUDA"
    if (Test-Path $cudaRoot) {
        $candidate = Get-ChildItem -Path $cudaRoot -Directory -Filter "v*" -ErrorAction SilentlyContinue |
            Sort-Object Name -Descending | Select-Object -First 1
        if ($null -ne $candidate -and (Test-CollaboratorCudaRoot -CandidatePath $candidate.FullName)) {
            return $candidate.FullName
        }
    }

    $vendorRoot = Join-Path $repoRoot "vendor"
    if (Test-Path $vendorRoot) {
        $candidate = Get-ChildItem -Path $vendorRoot -Directory -ErrorAction SilentlyContinue |
            Where-Object { $_.Name -match '^(cuda-|CUDA-)' } |
            Sort-Object Name -Descending | Select-Object -First 1
        if ($null -ne $candidate -and (Test-CollaboratorCudaRoot -CandidatePath $candidate.FullName)) {
            return $candidate.FullName
        }
    }

    return ""
}

function Test-CollaboratorTensorRtRtxRoot {
    param([string]$CandidatePath)

    return (Test-Path (Join-Path $CandidatePath "include\NvInfer.h")) -and
           ((Get-ChildItem -Path (Join-Path $CandidatePath "bin") -Filter "tensorrt_rtx*.dll" -File -ErrorAction SilentlyContinue | Measure-Object).Count -gt 0)
}

function Resolve-CollaboratorTensorRtRtxRoot {
    if (-not [string]::IsNullOrWhiteSpace($env:TENSORRT_RTX_HOME) -and
        (Test-CollaboratorTensorRtRtxRoot -CandidatePath $env:TENSORRT_RTX_HOME)) {
        return [System.IO.Path]::GetFullPath($env:TENSORRT_RTX_HOME)
    }

    $vendorRoot = Join-Path $repoRoot "vendor"
    if (Test-Path $vendorRoot) {
        foreach ($candidate in (Get-ChildItem -Path $vendorRoot -Directory -ErrorAction SilentlyContinue |
                Where-Object { $_.Name -match '^(TensorRT-RTX|tensorrt-rtx)' } |
                Sort-Object Name -Descending)) {
            if (Test-CollaboratorTensorRtRtxRoot -CandidatePath $candidate.FullName) {
                return $candidate.FullName
            }
        }
    }

    return ""
}

function Test-CollaboratorPython312 {
    $pyLauncher = Get-Command "py.exe" -ErrorAction SilentlyContinue
    if ($null -ne $pyLauncher) {
        & $pyLauncher.Source -3.12 -c "import sys" 2>$null
        if ($LASTEXITCODE -eq 0) {
            return $true
        }
    }

    $python = Get-Command "python.exe" -ErrorAction SilentlyContinue
    if ($null -ne $python) {
        $version = & $python.Source -c "import sys; print(f'{sys.version_info.major}.{sys.version_info.minor}')" 2>$null
        if ($LASTEXITCODE -eq 0 -and ($version | Out-String).Trim() -eq "3.12") {
            return $true
        }
    }

    return $false
}

function Test-CollaboratorVsToolchain {
    if (-not [string]::IsNullOrWhiteSpace($env:VSINSTALLDIR)) {
        $candidate = Join-Path $env:VSINSTALLDIR "Common7\Tools\VsDevCmd.bat"
        if (Test-Path $candidate) {
            return $true
        }
    }

    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    return Test-Path $vswhere
}

function Extract-CollaboratorArchive {
    param(
        [string]$ArchivePath,
        [string]$DestinationDir
    )

    if (Test-Path $DestinationDir) {
        Remove-Item $DestinationDir -Recurse -Force
    }
    New-Item -ItemType Directory -Path $DestinationDir -Force | Out-Null

    $tar = Get-Command "tar.exe" -ErrorAction SilentlyContinue
    if ($null -ne $tar) {
        & $tar.Source -xf $ArchivePath -C $DestinationDir
        if ($LASTEXITCODE -eq 0) {
            return
        }
    }

    Expand-Archive -Path $ArchivePath -DestinationPath $DestinationDir -Force
}

function Test-CollaboratorNsis {
    $command = Get-Command "makensis.exe" -ErrorAction SilentlyContinue
    if ($null -ne $command) {
        return $true
    }

    return (Test-Path "C:\Program Files (x86)\NSIS\makensis.exe") -or
           (Test-Path "C:\Program Files (x86)\NSIS\Bin\makensis.exe")
}

function Ensure-CollaboratorVcpkgRoot {
    $candidateRoots = @()
    if (-not [string]::IsNullOrWhiteSpace($env:VCPKG_ROOT)) {
        $candidateRoots += $env:VCPKG_ROOT
    }
    $candidateRoots += @(
        "C:\tools\vcpkg",
        (Join-Path (Split-Path -Parent $repoRoot) "vcpkg")
    )

    foreach ($candidateRoot in ($candidateRoots | Where-Object { -not [string]::IsNullOrWhiteSpace($_) } | Select-Object -Unique)) {
        if (Test-Path (Join-Path $candidateRoot "bootstrap-vcpkg.bat")) {
            $env:VCPKG_ROOT = [System.IO.Path]::GetFullPath($candidateRoot)
            [Environment]::SetEnvironmentVariable("VCPKG_ROOT", $env:VCPKG_ROOT, "User")
            Write-Host "[collaborator] Using vcpkg at $($env:VCPKG_ROOT)" -ForegroundColor Cyan
            return
        }
    }

    $targetRoot = Join-Path (Split-Path -Parent $repoRoot) "vcpkg"
    if (-not (Test-Path $targetRoot)) {
        Write-Host "[collaborator] Bootstrapping vcpkg into $targetRoot" -ForegroundColor Cyan
        Invoke-CollaboratorWorkingCommand -FilePath "git" -WorkingDirectory (Split-Path -Parent $repoRoot) -Arguments @(
            "clone",
            "--depth", "1",
            "https://github.com/microsoft/vcpkg.git",
            $targetRoot
        )
    }

    Invoke-CollaboratorWorkingCommand -FilePath (Join-Path $targetRoot "bootstrap-vcpkg.bat") -Arguments @("-disableMetrics")
    $env:VCPKG_ROOT = [System.IO.Path]::GetFullPath($targetRoot)
    [Environment]::SetEnvironmentVariable("VCPKG_ROOT", $env:VCPKG_ROOT, "User")
    Write-Host "[collaborator] VCPKG_ROOT set to $($env:VCPKG_ROOT)" -ForegroundColor Green
}

function Ensure-CollaboratorNsis {
    if (Test-CollaboratorNsis) {
        return
    }

    $winget = Get-Command "winget.exe" -ErrorAction SilentlyContinue
    if ($null -eq $winget) {
        throw "NSIS is missing and winget is unavailable to install it automatically."
    }

    Write-Host "[collaborator] Installing NSIS with winget..." -ForegroundColor Cyan
    Invoke-CollaboratorWorkingCommand -FilePath $winget.Source -Arguments @(
        "install",
        "--id", "NSIS.NSIS",
        "-e",
        "--accept-package-agreements",
        "--accept-source-agreements",
        "--silent"
    )

    if (-not (Test-CollaboratorNsis)) {
        throw "NSIS installation did not expose makensis.exe."
    }
}

function Ensure-CollaboratorTensorRtRtxSdk {
    $existingRoot = Resolve-CollaboratorTensorRtRtxRoot
    if (-not [string]::IsNullOrWhiteSpace($existingRoot)) {
        $env:TENSORRT_RTX_HOME = $existingRoot
        return
    }

    $downloadUrl = "https://developer.nvidia.com/downloads/trt/rtx_sdk/secure/1.2/tensorrt-rtx-1.2.0.54-win10-amd64-cuda-12.9-release-external.zip"
    $vendorRoot = Join-Path $repoRoot "vendor"
    $sdkRoot = Join-Path $vendorRoot "TensorRT-RTX-1.2.0.54"
    $tempRoot = Join-Path $repoRoot "temp\collaborator-downloads"
    $archivePath = Join-Path $tempRoot "tensorrt-rtx-1.2.0.54-win10-amd64-cuda-12.9-release-external.zip"
    $extractRoot = Join-Path $tempRoot "tensorrt-rtx-sdk"

    if (Test-CollaboratorTensorRtRtxRoot -CandidatePath $sdkRoot) {
        $env:TENSORRT_RTX_HOME = $sdkRoot
        return
    }

    New-Item -ItemType Directory -Path $tempRoot -Force | Out-Null
    Write-Host "[collaborator] Downloading TensorRT RTX SDK 1.2 for CUDA 12.9..." -ForegroundColor Cyan
    Invoke-CollaboratorWorkingCommand -FilePath "curl.exe" -Arguments @(
        "-L",
        $downloadUrl,
        "-o",
        $archivePath
    )

    Write-Host "[collaborator] Extracting TensorRT RTX SDK..." -ForegroundColor Cyan
    Extract-CollaboratorArchive -ArchivePath $archivePath -DestinationDir $extractRoot

    $extractedSdkRoot = Join-Path $extractRoot "TensorRT-RTX-1.2.0.54"
    if (-not (Test-CollaboratorTensorRtRtxRoot -CandidatePath $extractedSdkRoot)) {
        throw "Downloaded TensorRT RTX SDK layout is invalid: $extractedSdkRoot"
    }

    if (Test-Path $sdkRoot) {
        Remove-Item $sdkRoot -Recurse -Force
    }
    Move-Item -Path $extractedSdkRoot -Destination $sdkRoot
    $env:TENSORRT_RTX_HOME = $sdkRoot
    Write-Host "[collaborator] TensorRT RTX SDK ready at $sdkRoot" -ForegroundColor Green
}

function Ensure-CollaboratorEnvironment {
    $rtxOrtRoot = Get-CorridorKeyWindowsOrtRootPath -RepoRoot $repoRoot -Track "rtx"

    Ensure-CollaboratorVcpkgRoot
    Ensure-CollaboratorNsis

    if (-not (Test-Path $rtxOrtRoot)) {
        if (-not (Test-CollaboratorPython312)) {
            throw "Python 3.12 is required before staging the curated Windows RTX runtime."
        }
        if (-not (Test-CollaboratorVsToolchain)) {
            throw "Visual Studio C++ toolchain is required before staging the curated Windows RTX runtime."
        }
        if ([string]::IsNullOrWhiteSpace((Resolve-CollaboratorCudaRoot))) {
            throw "CUDA toolkit is required before staging the curated Windows RTX runtime."
        }
        Ensure-CollaboratorTensorRtRtxSdk
    }
}

function Assert-CollaboratorPrerequisites {
    param(
        [string]$SourceRepo,
        [string]$ExplicitCheckpoint
    )

    $missing = [System.Collections.Generic.List[string]]::new()

    if ($null -eq (Get-Command "uv.exe" -ErrorAction SilentlyContinue) -and
        $null -eq (Get-Command "uv" -ErrorAction SilentlyContinue)) {
        [void]$missing.Add("uv")
    }

    if ($null -eq (Get-Command "cmake.exe" -ErrorAction SilentlyContinue) -and
        $null -eq (Get-Command "cmake" -ErrorAction SilentlyContinue)) {
        [void]$missing.Add("cmake")
    }

    if ([string]::IsNullOrWhiteSpace($env:VCPKG_ROOT) -or -not (Test-Path $env:VCPKG_ROOT)) {
        [void]$missing.Add("VCPKG_ROOT")
    }

    if (-not (Test-CollaboratorNsis)) {
        [void]$missing.Add("NSIS (makensis.exe)")
    }

    $checkpointPath = Resolve-CollaboratorCheckpointPath -ExplicitPath $ExplicitCheckpoint -SourceRepo $SourceRepo
    if ([string]::IsNullOrWhiteSpace($checkpointPath)) {
        [void]$missing.Add("CorridorKey checkpoint (.pth)")
    }

    $rtxOrtRoot = Get-CorridorKeyWindowsOrtRootPath -RepoRoot $repoRoot -Track "rtx"
    if (-not (Test-Path $rtxOrtRoot)) {
        if (-not (Test-CollaboratorPython312)) {
            [void]$missing.Add("Python 3.12 for ONNX Runtime RTX build")
        }
        if (-not (Test-CollaboratorVsToolchain)) {
            [void]$missing.Add("Visual Studio C++ toolchain")
        }
        if ([string]::IsNullOrWhiteSpace((Resolve-CollaboratorCudaRoot))) {
            [void]$missing.Add("CUDA toolkit")
        }
        if ([string]::IsNullOrWhiteSpace((Resolve-CollaboratorTensorRtRtxRoot))) {
            [void]$missing.Add("TensorRT RTX SDK")
        }
    }

    if ($missing.Count -gt 0) {
        throw ("Collaborator preflight failed. Missing: " + ($missing -join ", "))
    }

    Write-Host "[collaborator] Preflight OK." -ForegroundColor Green
    Write-Host "[collaborator] Checkpoint: $checkpointPath" -ForegroundColor Cyan
}

Assert-InGitRepository
Set-Location $repoRoot

Write-Host "[collaborator] Repository: $repoRoot" -ForegroundColor Cyan
Ensure-CollaboratorEnvironment
Assert-CollaboratorPrerequisites -SourceRepo $CorridorKeyRepo -ExplicitCheckpoint $Checkpoint
Write-Host "[collaborator] Step 1/5: Installing Git LFS hooks" -ForegroundColor Cyan
Invoke-RepoCommand -FilePath "git" -Arguments @("lfs", "install", "--local")

Write-Host "[collaborator] Step 2/5: Skipping repo LFS object pull for collaborator regeneration" -ForegroundColor Cyan
Write-Host "[collaborator] This workflow reuses a local checkpoint when available and regenerates the promoted model pack from source." -ForegroundColor DarkGray

if (-not [string]::IsNullOrWhiteSpace($CommitBranch)) {
    $currentBranch = (& git branch --show-current).Trim()
    if ([string]::IsNullOrWhiteSpace($currentBranch)) {
        throw "Could not determine the current branch."
    }
    if ($currentBranch -ne $CommitBranch) {
        Write-Host "[collaborator] Switching to branch: $CommitBranch" -ForegroundColor Cyan
        Invoke-RepoCommand -FilePath "git" -Arguments @("checkout", "-B", $CommitBranch)
    }
}

$rtxOrtRoot = Get-CorridorKeyWindowsOrtRootPath -RepoRoot $repoRoot -Track "rtx"
if (-not (Test-CorridorKeyWindowsOrtRoot -OrtRoot $rtxOrtRoot)) {
    $stageRuntimeArguments = @(
        "-NoProfile",
        "-ExecutionPolicy", "Bypass",
        "-File", (Join-Path $repoRoot "scripts\prepare_windows_rtx_release.ps1")
    )
    if (-not [string]::IsNullOrWhiteSpace($Version)) {
        $stageRuntimeArguments += @("-Version", $Version)
    }
    if (-not [string]::IsNullOrWhiteSpace($Checkpoint)) {
        $stageRuntimeArguments += @("-Checkpoint", $Checkpoint)
    }
    if (-not [string]::IsNullOrWhiteSpace($CorridorKeyRepo)) {
        $stageRuntimeArguments += @("-CorridorKeyRepo", $CorridorKeyRepo)
    }
    $stageRuntimeArguments += @(
        "-BootstrapOrtSource",
        "-SkipModelPreparation",
        "-SkipRuntimeBuild",
        "-SkipPackage"
    )

    Write-Host "[collaborator] Step 3/5: Staging the curated Windows RTX runtime" -ForegroundColor Cyan
    Invoke-RepoCommand -FilePath "powershell.exe" -Arguments $stageRuntimeArguments
    if (-not (Test-CorridorKeyWindowsOrtRoot -OrtRoot $rtxOrtRoot)) {
        throw "Curated Windows RTX runtime staging did not produce $rtxOrtRoot"
    }
}

$prepareArguments = @(
    "-NoProfile",
    "-ExecutionPolicy", "Bypass",
    "-File", (Join-Path $repoRoot "scripts\windows.ps1"),
    "-Task", "regen-rtx-release"
)
if (-not [string]::IsNullOrWhiteSpace($Version)) {
    $prepareArguments += @("-Version", $Version)
}
if (-not [string]::IsNullOrWhiteSpace($Checkpoint)) {
    $prepareArguments += @("-Checkpoint", $Checkpoint)
}
if (-not [string]::IsNullOrWhiteSpace($CorridorKeyRepo)) {
    $prepareArguments += @("-CorridorKeyRepo", $CorridorKeyRepo)
}

Write-Host "[collaborator] Step 4/5: Regenerating and certifying the Windows RTX release" -ForegroundColor Cyan
Invoke-RepoCommand -FilePath "powershell.exe" -Arguments $prepareArguments

if ($CreateCommit.IsPresent) {
    Ensure-GitIdentity -PreferredName $GitUserName -PreferredEmail $GitUserEmail

    $commitPaths = @(
        "models",
        "CMakeLists.txt",
        "src/gui/package.json",
        "src/gui/src-tauri/Cargo.toml",
        "src/gui/src-tauri/tauri.conf.json"
    )

    Write-Host "[collaborator] Step 5/5: Committing generated models and release version metadata" -ForegroundColor Cyan
    Invoke-RepoCommand -FilePath "git" -Arguments (@("add", "--") + $commitPaths)

    & git diff --cached --quiet -- @commitPaths
    if ($LASTEXITCODE -ne 0) {
        Invoke-RepoCommand -FilePath "git" -Arguments @("commit", "-m", $CommitMessage)
    } else {
        Write-Host "[collaborator] No regenerated model artifacts or version metadata were staged; skipping commit." -ForegroundColor Yellow
    }

    if ($Push.IsPresent) {
        Write-Host "[collaborator] Step 5/5: Pushing branch" -ForegroundColor Cyan
        Invoke-RepoCommand -FilePath "git" -Arguments @("push", "-u", "origin", "HEAD")
    }
}

if ($BuildRelease.IsPresent) {
    if ($SkipReleaseTests.IsPresent) {
        Write-Host "[collaborator] SkipReleaseTests is ignored for the certified Windows RTX regeneration flow." -ForegroundColor Yellow
    }
    Write-Host "[collaborator] Release artifacts are available under dist\\" -ForegroundColor Green
}
