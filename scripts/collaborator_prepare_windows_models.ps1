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
$rtxBuildContract = Get-CorridorKeyWindowsRtxBuildContract
$gitExecutable = Resolve-CorridorKeyGitPath

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

function Invoke-RepoCommandCapture {
    param(
        [string]$FilePath,
        [string[]]$Arguments,
        [string]$WorkingDirectory = ""
    )

    Write-Host ("  > " + ((@($FilePath) + $Arguments) -join " ")) -ForegroundColor DarkGray

    $output = @()
    if ([string]::IsNullOrWhiteSpace($WorkingDirectory)) {
        $output = & $FilePath @Arguments 2>&1
    } else {
        Push-Location $WorkingDirectory
        try {
            $output = & $FilePath @Arguments 2>&1
        } finally {
            Pop-Location
        }
    }

    $exitCode = $LASTEXITCODE
    foreach ($line in @($output)) {
        Write-Host ($line | Out-String).TrimEnd()
    }

    return [pscustomobject]@{
        exit_code = $exitCode
        output = ($output | Out-String).Trim()
    }
}

function Assert-InGitRepository {
    if ([string]::IsNullOrWhiteSpace($gitExecutable)) {
        throw "git was not found."
    }

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

    $value = & $gitExecutable @Arguments 2>$null
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
        Invoke-RepoCommand -FilePath $gitExecutable -Arguments @("config", "--local", "user.name", $resolvedName)
    }
    if ($localEmail -ne $resolvedEmail) {
        Invoke-RepoCommand -FilePath $gitExecutable -Arguments @("config", "--local", "user.email", $resolvedEmail)
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

function New-CollaboratorArtifactArchive {
    param([string]$ResolvedVersion)

    $archiveScript = Join-Path $repoRoot "scripts\package_collaborator_artifacts_windows.ps1"
    if (-not (Test-Path $archiveScript)) {
        throw "Collaborator artifact packaging script not found: $archiveScript"
    }

    $archivePath = Join-Path $repoRoot ("dist\CorridorKey_Collaborator_v" + $ResolvedVersion + "_Windows_RTX.zip")
    $archiveArguments = @(
        "-NoProfile",
        "-ExecutionPolicy", "Bypass",
        "-File", $archiveScript,
        "-Version", $ResolvedVersion,
        "-OutputZip", $archivePath
    )
    Invoke-RepoCommand -FilePath "powershell.exe" -Arguments $archiveArguments
    return $archivePath
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

    return Test-CorridorKeyCudaToolkitRoot -CandidatePath $CandidatePath
}

function Resolve-CollaboratorCudaRoot {
    return Resolve-CorridorKeyCudaToolkitRoot -RepoRoot $repoRoot
}

function Test-CollaboratorTensorRtRtxRoot {
    param([string]$CandidatePath)

    return Test-CorridorKeyTensorRtRtxRoot -CandidatePath $CandidatePath
}

function Resolve-CollaboratorTensorRtRtxRoot {
    return Resolve-CorridorKeyTensorRtRtxHome -RepoRoot $repoRoot
}

function Resolve-CollaboratorCmakePath {
    $resolved = Resolve-CorridorKeyWindowsCmake -MinimumVersion $rtxBuildContract.minimum_cmake_version
    return $resolved.path
}

function Get-CollaboratorCmakeVersion {
    $resolved = Resolve-CorridorKeyWindowsCmake -MinimumVersion $rtxBuildContract.minimum_cmake_version
    return $resolved.version
}

function Test-CollaboratorCmakeRequirement {
    param([string]$MinimumVersion = "")

    $resolvedMinimumVersion = if ([string]::IsNullOrWhiteSpace($MinimumVersion)) {
        $rtxBuildContract.minimum_cmake_version
    } else {
        $MinimumVersion
    }

    $resolved = Resolve-CorridorKeyWindowsCmake -MinimumVersion $resolvedMinimumVersion
    return $resolved.meets_minimum
}

function Test-CollaboratorPython312 {
    return -not [string]::IsNullOrWhiteSpace((Resolve-CorridorKeyPython312Path))
}

function Test-CollaboratorVsToolchain {
    return -not [string]::IsNullOrWhiteSpace((Resolve-CorridorKeyVsDevCmdPath))
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
    return -not [string]::IsNullOrWhiteSpace((Resolve-CorridorKeyMakeNsisPath))
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
        Invoke-CollaboratorWorkingCommand -FilePath $gitExecutable -WorkingDirectory (Split-Path -Parent $repoRoot) -Arguments @(
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

function Ensure-CollaboratorCmake {
    $minimumVersion = [version]$rtxBuildContract.minimum_cmake_version
    $currentVersion = Get-CollaboratorCmakeVersion
    if (-not [string]::IsNullOrWhiteSpace($currentVersion) -and ([version]$currentVersion -ge $minimumVersion)) {
        return
    }

    $winget = Get-Command "winget.exe" -ErrorAction SilentlyContinue
    if ($null -eq $winget) {
        if ([string]::IsNullOrWhiteSpace($currentVersion)) {
            throw "CMake $($rtxBuildContract.minimum_cmake_version)+ is required and winget is unavailable to install it automatically."
        }
        throw "CMake $($rtxBuildContract.minimum_cmake_version)+ is required. Found $currentVersion and winget is unavailable to upgrade it automatically."
    }

    $commonArguments = @(
        "--id", "Kitware.CMake",
        "-e",
        "--accept-package-agreements",
        "--accept-source-agreements",
        "--silent"
    )

    $mode = if ([string]::IsNullOrWhiteSpace($currentVersion)) { "install" } else { "upgrade" }
    Write-Host "[collaborator] Ensuring CMake 3.28+ with winget..." -ForegroundColor Cyan
    & $winget.Source $mode @commonArguments
    $wingetExitCode = $LASTEXITCODE
    if ($wingetExitCode -ne 0 -and $mode -eq "upgrade") {
        Write-Host "[collaborator] winget upgrade did not complete; retrying with install..." -ForegroundColor Yellow
        & $winget.Source "install" @commonArguments
        $wingetExitCode = $LASTEXITCODE
    }
    if ($wingetExitCode -ne 0) {
        throw "Failed to install or upgrade CMake via winget."
    }

    $resolvedCmake = Resolve-CorridorKeyWindowsCmake -MinimumVersion $rtxBuildContract.minimum_cmake_version
    if (-not $resolvedCmake.meets_minimum) {
        $resolvedVersion = if ([string]::IsNullOrWhiteSpace($resolvedCmake.version)) { "not found" } else { $resolvedCmake.version }
        throw "CMake $($rtxBuildContract.minimum_cmake_version)+ is required for ONNX Runtime RTX builds. Resolved: $resolvedVersion"
    }
}

function Ensure-CollaboratorTensorRtRtxSdk {
    # Delegate to the shared helper so every pipeline (collaborator,
    # prepare-rtx, build_ort_windows_rtx) fetches the same SDK from the
    # same URL with the same extraction logic. Avoids drift between the
    # collaborator flow and the release flow.
    [void](Ensure-CorridorKeyTensorRtRtxHome -RepoRoot $repoRoot)
}

function Ensure-CollaboratorEnvironment {
    $rtxOrtRoot = Get-CorridorKeyWindowsOrtRootPath -RepoRoot $repoRoot -Track "rtx"

    Ensure-CollaboratorVcpkgRoot
    Ensure-CollaboratorCmake
    Ensure-CollaboratorNsis

    if (-not (Test-CorridorKeyWindowsOrtRoot -OrtRoot $rtxOrtRoot)) {
        if (-not (Test-CollaboratorPython312)) {
            throw "Python $($rtxBuildContract.required_python_version) is required before staging the curated Windows RTX runtime."
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

function Get-CollaboratorPreflightState {
    param(
        [string]$SourceRepo,
        [string]$ExplicitCheckpoint
    )

    $resolvedCmake = Resolve-CorridorKeyWindowsCmake -MinimumVersion $rtxBuildContract.minimum_cmake_version
    $resolvedCheckpointPath = Resolve-CollaboratorCheckpointPath -ExplicitPath $ExplicitCheckpoint -SourceRepo $SourceRepo
    $resolvedRtxOrtRoot = Get-CorridorKeyWindowsOrtRootPath -RepoRoot $repoRoot -Track "rtx"

    return [pscustomobject]@{
        uv_path = Resolve-CorridorKeyUvPath
        cmake = $resolvedCmake
        vcpkg_root = $env:VCPKG_ROOT
        nsis_path = Resolve-CorridorKeyMakeNsisPath
        checkpoint_path = $resolvedCheckpointPath
        rtx_ort_root = $resolvedRtxOrtRoot
        rtx_ort_ready = (Test-CorridorKeyWindowsOrtRoot -OrtRoot $resolvedRtxOrtRoot)
        python312_path = Resolve-CorridorKeyPython312Path
        vsdevcmd_path = Resolve-CorridorKeyVsDevCmdPath
        cuda_root = Resolve-CollaboratorCudaRoot
        tensorrt_rtx_root = Resolve-CollaboratorTensorRtRtxRoot
    }
}

function Assert-CollaboratorPrerequisites {
    param(
        [string]$SourceRepo,
        [string]$ExplicitCheckpoint
    )

    $missing = [System.Collections.Generic.List[string]]::new()

    $state = Get-CollaboratorPreflightState -SourceRepo $SourceRepo -ExplicitCheckpoint $ExplicitCheckpoint

    if ([string]::IsNullOrWhiteSpace($state.uv_path)) {
        [void]$missing.Add("uv")
    }

    if (-not $state.cmake.meets_minimum) {
        if ([string]::IsNullOrWhiteSpace($state.cmake.version)) {
            [void]$missing.Add("CMake $($rtxBuildContract.minimum_cmake_version)+")
        } else {
            [void]$missing.Add("CMake $($rtxBuildContract.minimum_cmake_version)+ (found $($state.cmake.version))")
        }
    }

    if ([string]::IsNullOrWhiteSpace($state.vcpkg_root) -or -not (Test-Path $state.vcpkg_root)) {
        [void]$missing.Add("VCPKG_ROOT")
    }

    if ([string]::IsNullOrWhiteSpace($state.nsis_path)) {
        [void]$missing.Add("NSIS (makensis.exe)")
    }

    if ([string]::IsNullOrWhiteSpace($state.checkpoint_path)) {
        [void]$missing.Add("CorridorKey checkpoint (.pth)")
    }

    if (-not $state.rtx_ort_ready) {
        if ([string]::IsNullOrWhiteSpace($state.python312_path)) {
            [void]$missing.Add("Python $($rtxBuildContract.required_python_version) for ONNX Runtime RTX build")
        }
        if ([string]::IsNullOrWhiteSpace($state.vsdevcmd_path)) {
            [void]$missing.Add("Visual Studio C++ toolchain")
        }
        if ([string]::IsNullOrWhiteSpace($state.cuda_root)) {
            [void]$missing.Add("CUDA toolkit")
        }
        if ([string]::IsNullOrWhiteSpace($state.tensorrt_rtx_root)) {
            [void]$missing.Add("TensorRT RTX SDK")
        }
    }

    if ($missing.Count -gt 0) {
        throw ("Collaborator preflight failed. Missing: " + ($missing -join ", "))
    }

    Write-Host "[collaborator] Preflight OK." -ForegroundColor Green
    Write-Host "[collaborator] Checkpoint: $($state.checkpoint_path)" -ForegroundColor Cyan
    Write-Host "[collaborator] CMake: $($state.cmake.path) ($($state.cmake.version))" -ForegroundColor Cyan
}

Assert-InGitRepository
Set-Location $repoRoot

$resolvedVersion = Initialize-CorridorKeyVersion -RepoRoot $repoRoot -Version $Version
$artifactArchivePath = ""
$pushFailed = $false

Write-Host "[collaborator] Repository: $repoRoot" -ForegroundColor Cyan
Ensure-CollaboratorEnvironment
Assert-CollaboratorPrerequisites -SourceRepo $CorridorKeyRepo -ExplicitCheckpoint $Checkpoint
Write-Host "[collaborator] Step 1/5: Installing Git LFS hooks" -ForegroundColor Cyan
Invoke-RepoCommand -FilePath $gitExecutable -Arguments @("lfs", "install", "--local")

Write-Host "[collaborator] Step 2/5: Skipping repo LFS object pull for collaborator regeneration" -ForegroundColor Cyan
Write-Host "[collaborator] This workflow reuses a local checkpoint when available and regenerates the promoted model pack from source." -ForegroundColor DarkGray

if (-not [string]::IsNullOrWhiteSpace($CommitBranch)) {
    $currentBranch = (& $gitExecutable branch --show-current).Trim()
    if ([string]::IsNullOrWhiteSpace($currentBranch)) {
        throw "Could not determine the current branch."
    }
    if ($currentBranch -ne $CommitBranch) {
        Write-Host "[collaborator] Switching to branch: $CommitBranch" -ForegroundColor Cyan
        Invoke-RepoCommand -FilePath $gitExecutable -Arguments @("checkout", "-B", $CommitBranch)
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

Write-Host "[collaborator] Step 5/6: Packaging collaborator artifact fallback" -ForegroundColor Cyan
$artifactArchivePath = New-CollaboratorArtifactArchive -ResolvedVersion $resolvedVersion
Write-Host "[collaborator] Manual artifact archive: $artifactArchivePath" -ForegroundColor Green

if ($CreateCommit.IsPresent) {
    Ensure-GitIdentity -PreferredName $GitUserName -PreferredEmail $GitUserEmail

    $commitPaths = @(
        "models",
        "CMakeLists.txt",
        "src/gui/package.json",
        "src/gui/src-tauri/Cargo.toml",
        "src/gui/src-tauri/tauri.conf.json"
    )

    Write-Host "[collaborator] Step 6/6: Committing generated models and release version metadata" -ForegroundColor Cyan
    Invoke-RepoCommand -FilePath $gitExecutable -Arguments (@("add", "--") + $commitPaths)

    & $gitExecutable diff --cached --quiet -- @commitPaths
    if ($LASTEXITCODE -ne 0) {
        Invoke-RepoCommand -FilePath $gitExecutable -Arguments @("commit", "-m", $CommitMessage)
    } else {
        Write-Host "[collaborator] No regenerated model artifacts or version metadata were staged; skipping commit." -ForegroundColor Yellow
    }

    if ($Push.IsPresent) {
        Write-Host "[collaborator] Step 6/6: Pushing branch" -ForegroundColor Cyan
        $pushResult = Invoke-RepoCommandCapture -FilePath $gitExecutable -Arguments @("push", "-u", "origin", "HEAD")
        if ($pushResult.exit_code -ne 0) {
            $pushFailed = $true
            Write-Host "[collaborator] Push failed after local generation completed." -ForegroundColor Yellow
            if ($pushResult.output -match "LFS budget" -or
                $pushResult.output -match "Git LFS" -or
                $pushResult.output -match "quota") {
                Write-Host "[collaborator] Remote Git LFS rejected the upload. The local artifact archive is ready for manual handoff." -ForegroundColor Yellow
            } else {
                Write-Host "[collaborator] The local artifact archive is ready even though the branch push failed." -ForegroundColor Yellow
            }
            if (-not [string]::IsNullOrWhiteSpace($artifactArchivePath)) {
                Write-Host "[collaborator] Share this archive manually if needed: $artifactArchivePath" -ForegroundColor Yellow
            }
        }
    }
}

if ($BuildRelease.IsPresent) {
    if ($SkipReleaseTests.IsPresent) {
        Write-Host "[collaborator] SkipReleaseTests is ignored for the certified Windows RTX regeneration flow." -ForegroundColor Yellow
    }
    Write-Host "[collaborator] Release artifacts are available under dist\\" -ForegroundColor Green
}

if (-not [string]::IsNullOrWhiteSpace($artifactArchivePath)) {
    Write-Host "[collaborator] Fallback archive ready: $artifactArchivePath" -ForegroundColor Green
}

if ($pushFailed) {
    Write-Host "[collaborator] Local generation succeeded, but remote push did not complete. Manual artifact transfer is available." -ForegroundColor Yellow
}
