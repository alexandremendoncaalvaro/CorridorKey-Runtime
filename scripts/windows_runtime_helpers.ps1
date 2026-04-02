Set-StrictMode -Version Latest

function Test-CorridorKeyWindowsHost {
    return [System.Environment]::OSVersion.Platform -eq [System.PlatformID]::Win32NT
}

function Get-CorridorKeyWindowsRtxBuildContract {
    return [pscustomobject]@{
        ort_source_ref = "v1.23.0"
        minimum_cmake_version = "3.28.0"
        required_python_version = "3.12"
        required_cuda_version = "12.9"
        tensorrt_rtx_version = "1.2.0.54"
        tensorrt_rtx_download_url = "https://developer.nvidia.com/downloads/trt/rtx_sdk/secure/1.2/tensorrt-rtx-1.2.0.54-win10-amd64-cuda-12.9-release-external.zip"
        cmake_generator = "Visual Studio 17 2022"
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

function Get-CorridorKeyRegistryValue {
    param(
        [string]$KeyPath,
        [string]$ValueName = ""
    )

    try {
        $key = Get-Item -Path $KeyPath -ErrorAction Stop
        $resolvedValueName = if ([string]::IsNullOrWhiteSpace($ValueName)) { "" } else { $ValueName }
        $value = $key.GetValue($resolvedValueName, $null, "DoNotExpandEnvironmentNames")
        if ($null -eq $value) {
            return ""
        }

        return ($value | Out-String).Trim()
    } catch {
        return ""
    }
}

function Get-CorridorKeyUniquePathList {
    param(
        [string[]]$Paths,
        [switch]$ExistingOnly
    )

    $seen = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
    $results = [System.Collections.Generic.List[string]]::new()

    foreach ($candidate in @($Paths)) {
        if ([string]::IsNullOrWhiteSpace($candidate)) {
            continue
        }

        try {
            $normalizedCandidate = [System.IO.Path]::GetFullPath($candidate)
        } catch {
            continue
        }

        if ($ExistingOnly.IsPresent -and -not (Test-Path $normalizedCandidate)) {
            continue
        }

        if ($seen.Add($normalizedCandidate)) {
            [void]$results.Add($normalizedCandidate)
        }
    }

    return $results.ToArray()
}

function Get-CorridorKeyResolvedCommandSources {
    param([string[]]$CandidateNames)

    $paths = [System.Collections.Generic.List[string]]::new()
    foreach ($candidateName in @($CandidateNames)) {
        if ([string]::IsNullOrWhiteSpace($candidateName)) {
            continue
        }

        $command = Get-Command $candidateName -ErrorAction SilentlyContinue
        if ($null -ne $command -and -not [string]::IsNullOrWhiteSpace($command.Source)) {
            [void]$paths.Add($command.Source)
        }
    }

    return @(Get-CorridorKeyUniquePathList -Paths $paths.ToArray() -ExistingOnly)
}

function Get-CorridorKeyCmakeVersion {
    param([string]$CmakePath)

    if ([string]::IsNullOrWhiteSpace($CmakePath) -or -not (Test-Path $CmakePath)) {
        return ""
    }

    $firstLine = & $CmakePath --version 2>$null | Select-Object -First 1
    if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($firstLine)) {
        return ""
    }

    if (($firstLine | Out-String).Trim() -match 'cmake version ([0-9]+\.[0-9]+\.[0-9]+)') {
        return $Matches[1]
    }

    return ""
}

function Select-CorridorKeyBestVersionedPath {
    param(
        [object[]]$Candidates,
        [string]$MinimumVersion = ""
    )

    $candidateList = @($Candidates | Where-Object {
            $null -ne $_ -and
            -not [string]::IsNullOrWhiteSpace($_.path) -and
            -not [string]::IsNullOrWhiteSpace($_.version)
        })

    if ($candidateList.Count -eq 0) {
        return [pscustomobject]@{
            path = ""
            version = ""
            meets_minimum = $false
            candidate_count = 0
        }
    }

    $ordered = @($candidateList | Sort-Object @{
                Expression = { [version]$_.version }
                Descending = $true
            }, @{
                Expression = { $_.path }
                Descending = $false
            })

    if (-not [string]::IsNullOrWhiteSpace($MinimumVersion)) {
        $minimum = [version]$MinimumVersion
        $matching = @($ordered | Where-Object { [version]$_.version -ge $minimum })
        if ($matching.Count -gt 0) {
            return [pscustomobject]@{
                path = $matching[0].path
                version = $matching[0].version
                meets_minimum = $true
                candidate_count = $candidateList.Count
            }
        }
    }

    return [pscustomobject]@{
        path = $ordered[0].path
        version = $ordered[0].version
        meets_minimum = $false
        candidate_count = $candidateList.Count
    }
}

function Get-CorridorKeyWindowsCmakeCandidatePaths {
    param(
        [string[]]$AdditionalCandidatePaths = @(),
        [switch]$PreferCandidatePathsOnly
    )

    $candidatePaths = [System.Collections.Generic.List[string]]::new()
    foreach ($candidate in @($AdditionalCandidatePaths)) {
        if (-not [string]::IsNullOrWhiteSpace($candidate)) {
            [void]$candidatePaths.Add($candidate)
        }
    }

    if ($PreferCandidatePathsOnly.IsPresent) {
        return @(Get-CorridorKeyUniquePathList -Paths $candidatePaths.ToArray() -ExistingOnly)
    }

    foreach ($registryPath in @(
            "HKLM:\SOFTWARE\Kitware\CMake",
            "HKCU:\SOFTWARE\Kitware\CMake"
        )) {
        $installDir = Get-CorridorKeyRegistryValue -KeyPath $registryPath -ValueName "InstallDir"
        if (-not [string]::IsNullOrWhiteSpace($installDir)) {
            [void]$candidatePaths.Add((Join-Path $installDir "bin\cmake.exe"))
        }
    }

    foreach ($appPathKey in @(
            "HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\App Paths\cmake.exe",
            "HKCU:\SOFTWARE\Microsoft\Windows\CurrentVersion\App Paths\cmake.exe"
        )) {
        $appPath = Get-CorridorKeyRegistryValue -KeyPath $appPathKey
        if (-not [string]::IsNullOrWhiteSpace($appPath)) {
            [void]$candidatePaths.Add($appPath)
        }
    }

    $commonCmakeCandidates = [System.Collections.Generic.List[string]]::new()
    foreach ($candidatePath in @(
            "C:\Program Files\CMake\bin\cmake.exe",
            "C:\Program Files (x86)\CMake\bin\cmake.exe"
        )) {
        [void]$commonCmakeCandidates.Add($candidatePath)
    }
    if (-not [string]::IsNullOrWhiteSpace($env:LOCALAPPDATA)) {
        [void]$commonCmakeCandidates.Add((Join-Path $env:LOCALAPPDATA "Programs\CMake\bin\cmake.exe"))
    }

    foreach ($candidatePath in $commonCmakeCandidates) {
        if (-not [string]::IsNullOrWhiteSpace($candidatePath)) {
            [void]$candidatePaths.Add($candidatePath)
        }
    }

    foreach ($resolvedCommandPath in Get-CorridorKeyResolvedCommandSources -CandidateNames @("cmake.exe", "cmake")) {
        [void]$candidatePaths.Add($resolvedCommandPath)
    }

    return @(Get-CorridorKeyUniquePathList -Paths $candidatePaths.ToArray() -ExistingOnly)
}

function Resolve-CorridorKeyWindowsCmake {
    param(
        [string[]]$AdditionalCandidatePaths = @(),
        [string]$MinimumVersion = "",
        [switch]$PreferCandidatePathsOnly
    )

    $requirements = Get-CorridorKeyWindowsRtxBuildContract
    $resolvedMinimumVersion = if ([string]::IsNullOrWhiteSpace($MinimumVersion)) {
        $requirements.minimum_cmake_version
    } else {
        $MinimumVersion
    }

    $candidateInfos = [System.Collections.Generic.List[object]]::new()
    foreach ($candidatePath in Get-CorridorKeyWindowsCmakeCandidatePaths `
            -AdditionalCandidatePaths $AdditionalCandidatePaths `
            -PreferCandidatePathsOnly:$PreferCandidatePathsOnly.IsPresent) {
        $candidateVersion = Get-CorridorKeyCmakeVersion -CmakePath $candidatePath
        if (-not [string]::IsNullOrWhiteSpace($candidateVersion)) {
            [void]$candidateInfos.Add([pscustomobject]@{
                    path = $candidatePath
                    version = $candidateVersion
                })
        }
    }

    return Select-CorridorKeyBestVersionedPath -Candidates $candidateInfos.ToArray() -MinimumVersion $resolvedMinimumVersion
}

function Resolve-CorridorKeyGitPath {
    $candidatePaths = @(
        "C:\Program Files\Git\cmd\git.exe",
        "C:\Program Files\Git\bin\git.exe"
    ) + (Get-CorridorKeyResolvedCommandSources -CandidateNames @("git.exe", "git"))

    $resolved = @(Get-CorridorKeyUniquePathList -Paths @($candidatePaths) -ExistingOnly)
    if ($resolved.Count -gt 0) {
        return $resolved[0]
    }

    return ""
}

function Resolve-CorridorKeyUvPath {
    $candidatePaths = @()
    if (-not [string]::IsNullOrWhiteSpace($env:USERPROFILE)) {
        $candidatePaths += Join-Path $env:USERPROFILE ".local\bin\uv.exe"
    }
    $candidatePaths += Get-CorridorKeyResolvedCommandSources -CandidateNames @("uv.exe", "uv")

    $resolved = @(Get-CorridorKeyUniquePathList -Paths @($candidatePaths) -ExistingOnly)
    if ($resolved.Count -gt 0) {
        return $resolved[0]
    }

    return ""
}

function Resolve-CorridorKeyMakeNsisPath {
    $candidatePaths = [System.Collections.Generic.List[string]]::new()

    foreach ($candidate in @(
            "C:\Program Files (x86)\NSIS\makensis.exe",
            "C:\Program Files (x86)\NSIS\Bin\makensis.exe",
            "C:\Program Files\NSIS\makensis.exe",
            "C:\Program Files\NSIS\Bin\makensis.exe"
        )) {
        [void]$candidatePaths.Add($candidate)
    }

    foreach ($registryPath in @(
            "HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\Nullsoft Install System",
            "HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\Nullsoft Install System",
            "HKCU:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\Nullsoft Install System"
        )) {
        $installLocation = Get-CorridorKeyRegistryValue -KeyPath $registryPath -ValueName "InstallLocation"
        if (-not [string]::IsNullOrWhiteSpace($installLocation)) {
            [void]$candidatePaths.Add((Join-Path $installLocation "makensis.exe"))
            [void]$candidatePaths.Add((Join-Path $installLocation "Bin\makensis.exe"))
        }
    }

    foreach ($resolvedCommandPath in Get-CorridorKeyResolvedCommandSources -CandidateNames @("makensis.exe")) {
        [void]$candidatePaths.Add($resolvedCommandPath)
    }

    $resolved = @(Get-CorridorKeyUniquePathList -Paths $candidatePaths.ToArray() -ExistingOnly)
    if ($resolved.Count -gt 0) {
        return $resolved[0]
    }

    return ""
}

function Get-CorridorKeyPythonVersion {
    param([string]$ExecutablePath)

    if ([string]::IsNullOrWhiteSpace($ExecutablePath) -or -not (Test-Path $ExecutablePath)) {
        return ""
    }

    $version = & $ExecutablePath -c "import sys; print(f'{sys.version_info.major}.{sys.version_info.minor}')" 2>$null
    if ($LASTEXITCODE -ne 0) {
        return ""
    }

    return ($version | Out-String).Trim()
}

function Resolve-CorridorKeyPython312Path {
    param([string]$ExplicitPath = "")

    $requirements = Get-CorridorKeyWindowsRtxBuildContract

    if (-not [string]::IsNullOrWhiteSpace($ExplicitPath)) {
        $resolvedVersion = Get-CorridorKeyPythonVersion -ExecutablePath $ExplicitPath
        if ($resolvedVersion -eq $requirements.required_python_version) {
            return [System.IO.Path]::GetFullPath($ExplicitPath)
        }
        return ""
    }

    $pyLauncher = Get-Command "py.exe" -ErrorAction SilentlyContinue
    if ($null -ne $pyLauncher) {
        $resolved = & $pyLauncher.Source -3.12 -c "import sys; print(sys.executable)" 2>$null
        if ($LASTEXITCODE -eq 0 -and -not [string]::IsNullOrWhiteSpace($resolved)) {
            return ($resolved | Out-String).Trim()
        }
    }

    $candidatePaths = @()
    if (-not [string]::IsNullOrWhiteSpace($env:LOCALAPPDATA)) {
        $candidatePaths += Join-Path $env:LOCALAPPDATA "Programs\Python\Python312\python.exe"
    }
    if (-not [string]::IsNullOrWhiteSpace(${env:ProgramFiles})) {
        $candidatePaths += Join-Path ${env:ProgramFiles} "Python312\python.exe"
    }
    if (-not [string]::IsNullOrWhiteSpace(${env:ProgramFiles(x86)})) {
        $candidatePaths += Join-Path ${env:ProgramFiles(x86)} "Python312-32\python.exe"
    }
    $candidatePaths += Get-CorridorKeyResolvedCommandSources -CandidateNames @("python.exe")

    foreach ($candidatePath in @(Get-CorridorKeyUniquePathList -Paths @($candidatePaths) -ExistingOnly)) {
        if ((Get-CorridorKeyPythonVersion -ExecutablePath $candidatePath) -eq $requirements.required_python_version) {
            return $candidatePath
        }
    }

    return ""
}

function Resolve-CorridorKeyVsDevCmdPath {
    param([string]$ExplicitPath = "")

    if (-not [string]::IsNullOrWhiteSpace($ExplicitPath)) {
        if (Test-Path $ExplicitPath) {
            return [System.IO.Path]::GetFullPath($ExplicitPath)
        }
        return ""
    }

    $candidatePaths = [System.Collections.Generic.List[string]]::new()

    if (-not [string]::IsNullOrWhiteSpace($env:VSINSTALLDIR)) {
        [void]$candidatePaths.Add((Join-Path $env:VSINSTALLDIR "Common7\Tools\VsDevCmd.bat"))
    }

    $vswhereCandidates = Get-CorridorKeyUniquePathList -Paths @(
        (Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"),
        (Get-CorridorKeyResolvedCommandSources -CandidateNames @("vswhere.exe"))
    ) -ExistingOnly

    foreach ($vswherePath in $vswhereCandidates) {
        $installationPath = & $vswherePath -latest -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2>$null
        if ($LASTEXITCODE -eq 0 -and -not [string]::IsNullOrWhiteSpace($installationPath)) {
            [void]$candidatePaths.Add((Join-Path ($installationPath | Out-String).Trim() "Common7\Tools\VsDevCmd.bat"))
        }
    }

    foreach ($installationRoot in @(
            "C:\Program Files\Microsoft Visual Studio\2022\Community",
            "C:\Program Files\Microsoft Visual Studio\2022\BuildTools",
            "C:\Program Files\Microsoft Visual Studio\2022\Professional",
            "C:\Program Files\Microsoft Visual Studio\2022\Enterprise"
        )) {
        [void]$candidatePaths.Add((Join-Path $installationRoot "Common7\Tools\VsDevCmd.bat"))
    }

    $resolved = @(Get-CorridorKeyUniquePathList -Paths $candidatePaths.ToArray() -ExistingOnly)
    if ($resolved.Count -gt 0) {
        return $resolved[0]
    }

    return ""
}

function Test-CorridorKeyCudaToolkitRoot {
    param([string]$CandidatePath)

    return (Test-Path (Join-Path $CandidatePath "bin\nvcc.exe")) -and
           (Test-Path (Join-Path $CandidatePath "include\cuda_runtime.h"))
}

function Resolve-CorridorKeyCudaToolkitRoot {
    param([string]$RepoRoot = "")

    if (-not [string]::IsNullOrWhiteSpace($env:CUDA_PATH) -and
        (Test-CorridorKeyCudaToolkitRoot -CandidatePath $env:CUDA_PATH)) {
        return [System.IO.Path]::GetFullPath($env:CUDA_PATH)
    }

    $cudaRoot = Join-Path ${env:ProgramFiles} "NVIDIA GPU Computing Toolkit\CUDA"
    if (Test-Path $cudaRoot) {
        $candidate = Get-ChildItem -Path $cudaRoot -Directory -Filter "v*" -ErrorAction SilentlyContinue |
            Sort-Object Name -Descending | Select-Object -First 1
        if ($null -ne $candidate -and (Test-CorridorKeyCudaToolkitRoot -CandidatePath $candidate.FullName)) {
            return $candidate.FullName
        }
    }

    if (-not [string]::IsNullOrWhiteSpace($RepoRoot)) {
        $vendorRoot = Join-Path $RepoRoot "vendor"
        if (Test-Path $vendorRoot) {
            $candidate = Get-ChildItem -Path $vendorRoot -Directory -ErrorAction SilentlyContinue |
                Where-Object { $_.Name -match '^(cuda-|CUDA-)' } |
                Sort-Object Name -Descending | Select-Object -First 1
            if ($null -ne $candidate -and (Test-CorridorKeyCudaToolkitRoot -CandidatePath $candidate.FullName)) {
                return $candidate.FullName
            }
        }
    }

    return ""
}

function Test-CorridorKeyTensorRtRtxRoot {
    param([string]$CandidatePath)

    return (Test-Path (Join-Path $CandidatePath "include\NvInfer.h")) -and
           ((Get-ChildItem -Path (Join-Path $CandidatePath "bin") -Filter "tensorrt_rtx*.dll" -File -ErrorAction SilentlyContinue |
               Measure-Object).Count -gt 0)
}

function Resolve-CorridorKeyTensorRtRtxHome {
    param([string]$RepoRoot = "")

    if (-not [string]::IsNullOrWhiteSpace($env:TENSORRT_RTX_HOME) -and
        (Test-CorridorKeyTensorRtRtxRoot -CandidatePath $env:TENSORRT_RTX_HOME)) {
        return [System.IO.Path]::GetFullPath($env:TENSORRT_RTX_HOME)
    }

    if (-not [string]::IsNullOrWhiteSpace($RepoRoot)) {
        $vendorRoot = Join-Path $RepoRoot "vendor"
        if (Test-Path $vendorRoot) {
            foreach ($candidate in (Get-ChildItem -Path $vendorRoot -Directory -ErrorAction SilentlyContinue |
                    Where-Object { $_.Name -match '^(TensorRT-RTX|tensorrt-rtx)' } |
                    Sort-Object Name -Descending)) {
                if (Test-CorridorKeyTensorRtRtxRoot -CandidatePath $candidate.FullName) {
                    return $candidate.FullName
                }
            }
        }
    }

    return ""
}

function Get-CorridorKeyProjectVersion {
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

function Assert-CorridorKeySemVer {
    param([string]$Version)

    if ([string]::IsNullOrWhiteSpace($Version) -or
        $Version -notmatch '^[0-9]+\.[0-9]+\.[0-9]+$') {
        throw "Version must use SemVer MAJOR.MINOR.PATCH. Received: $Version"
    }
}

function Set-CorridorKeyProjectVersion {
    param(
        [string]$RepoRoot,
        [string]$Version
    )

    Assert-CorridorKeySemVer -Version $Version

    $cmakePath = Join-Path $RepoRoot "CMakeLists.txt"
    if (-not (Test-Path $cmakePath)) {
        throw "Could not update project version because CMakeLists.txt was not found at $cmakePath"
    }

    $pattern = '^(?<prefix>\s*VERSION\s+)(?<version>[0-9]+\.[0-9]+\.[0-9]+)(?<suffix>\s*)$'
    $regex = [System.Text.RegularExpressions.Regex]::new(
        $pattern,
        [System.Text.RegularExpressions.RegexOptions]::Multiline
    )
    $utf8NoBom = [System.Text.UTF8Encoding]::new($false)
    $content = [System.IO.File]::ReadAllText($cmakePath, $utf8NoBom)
    $updated = $regex.Replace(
        $content,
        [System.Text.RegularExpressions.MatchEvaluator]{
            param($match)
            return $match.Groups["prefix"].Value + $Version + $match.Groups["suffix"].Value
        },
        1
    )

    if ($content -eq $updated) {
        $currentVersion = Get-CorridorKeyProjectVersion -RepoRoot $RepoRoot
        if ($currentVersion -eq $Version) {
            return $Version
        }
        throw "Could not update VERSION in $cmakePath"
    }

    [System.IO.File]::WriteAllText($cmakePath, $updated, $utf8NoBom)
    return $Version
}

function Set-CorridorKeyTextVersionField {
    param(
        [string]$Path,
        [string]$Pattern,
        [string]$Version,
        [string]$Description
    )

    if (-not (Test-Path $Path)) {
        throw "Could not update $Description because the file was not found at $Path"
    }

    $regex = [System.Text.RegularExpressions.Regex]::new(
        $Pattern,
        [System.Text.RegularExpressions.RegexOptions]::Multiline
    )
    $utf8NoBom = [System.Text.UTF8Encoding]::new($false)
    $content = [System.IO.File]::ReadAllText($Path, $utf8NoBom)
    $updated = $regex.Replace(
        $content,
        [System.Text.RegularExpressions.MatchEvaluator]{
            param($match)
            return $match.Groups["prefix"].Value + $Version + $match.Groups["suffix"].Value
        },
        1
    )

    if ($content -eq $updated) {
        $match = $regex.Match($content)
        if ($match.Success -and $match.Groups["version"].Value -eq $Version) {
            return
        }
        throw "Could not update $Description in $Path"
    }

    [System.IO.File]::WriteAllText($Path, $updated, $utf8NoBom)
}

function Sync-CorridorKeyGuiVersionMetadata {
    param(
        [string]$RepoRoot,
        [string]$Version
    )

    Assert-CorridorKeySemVer -Version $Version

    $guiRoot = Join-Path $RepoRoot "src\gui"
    Set-CorridorKeyTextVersionField `
        -Path (Join-Path $guiRoot "package.json") `
        -Pattern '^(?<prefix>\s*"version"\s*:\s*")(?<version>[0-9]+\.[0-9]+\.[0-9]+)(?<suffix>".*)$' `
        -Version $Version `
        -Description "GUI package version"

    Set-CorridorKeyTextVersionField `
        -Path (Join-Path $guiRoot "src-tauri\tauri.conf.json") `
        -Pattern '^(?<prefix>\s*"version"\s*:\s*")(?<version>[0-9]+\.[0-9]+\.[0-9]+)(?<suffix>".*)$' `
        -Version $Version `
        -Description "Tauri app version"

    Set-CorridorKeyTextVersionField `
        -Path (Join-Path $guiRoot "src-tauri\Cargo.toml") `
        -Pattern '^(?<prefix>version\s*=\s*")(?<version>[0-9]+\.[0-9]+\.[0-9]+)(?<suffix>"\s*)$' `
        -Version $Version `
        -Description "Tauri Cargo package version"

    return $Version
}

function Initialize-CorridorKeyVersion {
    param(
        [string]$RepoRoot,
        [string]$Version = "",
        [switch]$SyncGuiMetadata
    )

    $resolvedVersion = $Version
    $shouldSyncGuiMetadata = $SyncGuiMetadata.IsPresent -or
        (-not [string]::IsNullOrWhiteSpace($Version))

    if ([string]::IsNullOrWhiteSpace($resolvedVersion)) {
        $resolvedVersion = Get-CorridorKeyProjectVersion -RepoRoot $RepoRoot
    } else {
        $resolvedVersion = Set-CorridorKeyProjectVersion -RepoRoot $RepoRoot -Version $resolvedVersion
    }

    if ($shouldSyncGuiMetadata) {
        Sync-CorridorKeyGuiVersionMetadata -RepoRoot $RepoRoot -Version $resolvedVersion | Out-Null
    }

    return $resolvedVersion
}

function Get-CorridorKeyWindowsOrtRootPath {
    param(
        [string]$RepoRoot,
        [ValidateSet("rtx", "dml")]
        [string]$Track
    )

    $directoryName = if ($Track -eq "rtx") {
        "onnxruntime-windows-rtx"
    } else {
        "onnxruntime-windows-dml"
    }

    return [System.IO.Path]::GetFullPath((Join-Path $RepoRoot ("vendor\" + $directoryName)))
}

function Test-CorridorKeyWindowsOrtRoot {
    param([string]$OrtRoot)

    if ([string]::IsNullOrWhiteSpace($OrtRoot) -or -not (Test-Path $OrtRoot)) {
        return $false
    }

    $binDir = Join-Path $OrtRoot "bin"
    $libDir = Join-Path $OrtRoot "lib"
    $headerCandidates = @(
        (Join-Path $OrtRoot "include\onnxruntime\onnxruntime_c_api.h"),
        (Join-Path $OrtRoot "include\onnxruntime_c_api.h")
    )
    $runtimeDllSearchRoots = @($OrtRoot, $binDir)
    $importLibSearchRoots = @($OrtRoot, $libDir)

    $hasHeaders = $false
    foreach ($headerCandidate in $headerCandidates) {
        if (Test-Path $headerCandidate) {
            $hasHeaders = $true
            break
        }
    }

    $hasRuntimeDlls = $false
    foreach ($searchRoot in $runtimeDllSearchRoots) {
        if (-not (Test-Path $searchRoot)) {
            continue
        }
        if ((Get-ChildItem -Path $searchRoot -Filter "onnxruntime*.dll" -File -ErrorAction SilentlyContinue |
                Measure-Object).Count -gt 0) {
            $hasRuntimeDlls = $true
            break
        }
    }

    $hasImportLibs = $false
    foreach ($searchRoot in $importLibSearchRoots) {
        if (-not (Test-Path $searchRoot)) {
            continue
        }
        if ((Get-ChildItem -Path $searchRoot -Filter "onnxruntime*.lib" -File -ErrorAction SilentlyContinue |
                Measure-Object).Count -gt 0) {
            $hasImportLibs = $true
            break
        }
    }

    return $hasHeaders -and $hasRuntimeDlls -and $hasImportLibs
}

function Get-CorridorKeyWindowsOrtBinaryVersion {
    param(
        [string]$RepoRoot,
        [ValidateSet("rtx", "dml")]
        [string]$Track
    )

    $ortRoot = Get-CorridorKeyWindowsOrtRootPath -RepoRoot $RepoRoot -Track $Track
    $candidates = @(
        (Join-Path $ortRoot "bin\onnxruntime.dll"),
        (Join-Path $ortRoot "onnxruntime.dll")
    )

    foreach ($candidate in $candidates) {
        if (Test-Path $candidate) {
            $productVersion = (Get-Item $candidate).VersionInfo.ProductVersion
            if (-not [string]::IsNullOrWhiteSpace($productVersion)) {
                return $productVersion
            }
        }
    }

    throw "Unable to determine the curated ONNX Runtime version for the '$Track' track from $ortRoot. Stage the curated runtime first or pass -OrtVersion explicitly."
}

function Get-CorridorKeyWindowsTrackFromReleaseSuffix {
    param(
        [string]$ReleaseSuffix,
        [ValidateSet("rtx", "dml", "any")]
        [string]$DefaultTrack = "rtx"
    )

    if ([string]::IsNullOrWhiteSpace($ReleaseSuffix)) {
        return $DefaultTrack
    }

    if ($ReleaseSuffix -match "DirectML" -or $ReleaseSuffix -match "DML") {
        return "dml"
    }

    if ($ReleaseSuffix -match "RTX") {
        return "rtx"
    }

    return $DefaultTrack
}

function Get-CorridorKeyOfxModelProfileFromReleaseSuffix {
    param([string]$ReleaseSuffix)

    if ([string]::IsNullOrWhiteSpace($ReleaseSuffix)) {
        return "windows-rtx"
    }

    if ($ReleaseSuffix -match "DirectML" -or $ReleaseSuffix -match "DML") {
        return "windows-universal"
    }

    if ($ReleaseSuffix -match "RTX") {
        return "windows-rtx"
    }

    return "windows-rtx"
}

function Get-CorridorKeyWindowsReleaseLabelFromSuffix {
    param([string]$ReleaseSuffix)

    $modelProfile = Get-CorridorKeyOfxModelProfileFromReleaseSuffix -ReleaseSuffix $ReleaseSuffix
    switch ($modelProfile) {
        "windows-rtx" { return "Windows RTX" }
        "windows-universal" { return "Windows DirectML" }
        default { return "Windows RTX" }
    }
}

function Resolve-CorridorKeyWindowsOrtRoot {
    param(
        [string]$RepoRoot,
        [string]$ExplicitRoot = "",
        [ValidateSet("rtx", "dml", "any")]
        [string]$PreferredTrack = "any",
        [switch]$AllowEnvironmentOverride
    )

    if (-not [string]::IsNullOrWhiteSpace($ExplicitRoot)) {
        if (-not (Test-CorridorKeyWindowsOrtRoot -OrtRoot $ExplicitRoot)) {
            throw "Configured ONNX Runtime root is missing curated runtime files: $ExplicitRoot"
        }
        return [System.IO.Path]::GetFullPath($ExplicitRoot)
    }

    if ($AllowEnvironmentOverride.IsPresent -and -not [string]::IsNullOrWhiteSpace($env:CORRIDORKEY_WINDOWS_ORT_ROOT)) {
        if (-not (Test-CorridorKeyWindowsOrtRoot -OrtRoot $env:CORRIDORKEY_WINDOWS_ORT_ROOT)) {
            throw "CORRIDORKEY_WINDOWS_ORT_ROOT is missing curated runtime files: $env:CORRIDORKEY_WINDOWS_ORT_ROOT"
        }
        return [System.IO.Path]::GetFullPath($env:CORRIDORKEY_WINDOWS_ORT_ROOT)
    }

    $tracksToCheck = switch ($PreferredTrack) {
        "rtx" { @("rtx") }
        "dml" { @("dml") }
        default { @("rtx", "dml") }
    }

    foreach ($track in $tracksToCheck) {
        $candidate = Get-CorridorKeyWindowsOrtRootPath -RepoRoot $RepoRoot -Track $track
        if (Test-CorridorKeyWindowsOrtRoot -OrtRoot $candidate) {
            return $candidate
        }
    }

    if ($PreferredTrack -eq "rtx") {
        throw "Curated RTX runtime not found. Stage vendor\onnxruntime-windows-rtx or pass -OrtRoot explicitly."
    }

    if ($PreferredTrack -eq "dml") {
        throw "Curated DirectML runtime not found. Stage vendor\onnxruntime-windows-dml or pass -OrtRoot explicitly."
    }

    throw "Windows builds require a curated ONNX Runtime root. Stage vendor\onnxruntime-windows-rtx or vendor\onnxruntime-windows-dml, or set CORRIDORKEY_WINDOWS_ORT_ROOT."
}

function Get-CorridorKeyPreparedModelList {
    return @(
        "corridorkey_fp16_512.onnx",
        "corridorkey_fp16_768.onnx",
        "corridorkey_fp16_1024.onnx",
        "corridorkey_fp16_1536.onnx",
        "corridorkey_fp16_2048.onnx",
        "corridorkey_int8_512.onnx",
        "corridorkey_int8_768.onnx",
        "corridorkey_int8_1024.onnx"
    )
}

function Get-CorridorKeyWindowsRtxPromotedModelList {
    # TorchTRT FP16 ladder only. INT8 and legacy .onnx are not packaged in the Windows RTX release.
    return @(
        "corridorkey_torchtrt_fp16_512.ts",
        "corridorkey_torchtrt_fp16_1024.ts",
        "corridorkey_torchtrt_fp16_1536.ts",
        "corridorkey_torchtrt_fp16_2048.ts"
    )
}

function Get-CorridorKeyWindowsRtxTorchTensorRtArtifactList {
    return @(
        "corridorkey_torchtrt_fp16_512.ts",
        "corridorkey_torchtrt_fp16_1024.ts",
        "corridorkey_torchtrt_fp16_1536.ts",
        "corridorkey_torchtrt_fp16_2048.ts"
    )
}

function Get-CorridorKeyIntermediateModelList {
    return @(
        "corridorkey_fp32_512.onnx",
        "corridorkey_fp32_768.onnx",
        "corridorkey_fp32_1024.onnx",
        "corridorkey_fp32_1536.onnx",
        "corridorkey_fp32_2048.onnx"
    )
}

function Get-CorridorKeyOfxBundleTargetModels {
    param(
        [ValidateSet("windows-rtx", "windows-universal")]
        [string]$ModelProfile = "windows-rtx"
    )

    switch ($ModelProfile) {
        "windows-rtx" {
            return @(Get-CorridorKeyWindowsRtxPromotedModelList)
        }
        "windows-universal" {
            return @(
                "corridorkey_fp16_512.onnx",
                "corridorkey_fp16_768.onnx",
                "corridorkey_fp16_1024.onnx",
                "corridorkey_fp16_1536.onnx",
                "corridorkey_fp16_2048.onnx",
                "corridorkey_int8_512.onnx",
                "corridorkey_int8_768.onnx",
                "corridorkey_int8_1024.onnx"
            )
        }
        default {
            return @(Get-CorridorKeyWindowsRtxPromotedModelList)
        }
    }
}

function Get-CorridorKeyModelProfileContract {
    param(
        [ValidateSet("windows-rtx", "windows-universal")]
        [string]$ModelProfile = "windows-rtx"
    )

    switch ($ModelProfile) {
        "windows-rtx" {
            return [pscustomobject]@{
                model_profile = "windows-rtx"
                package_type = "ofx_bundle"
                bundle_track = "rtx"
                release_label = "Windows RTX"
                optimization_profile_id = "windows-rtx"
                optimization_profile_label = "Windows RTX"
                backend_intent = "tensorrt"
                fallback_policy = "safe_auto_quality_with_manual_override"
                warmup_policy = "precompiled_context_or_first_run_compile"
                certification_tier = "packaged_fp16_ladder_through_2048"
                unrestricted_quality_attempt = $true
                expects_compiled_context_models = $true
            }
        }
        "windows-universal" {
            return [pscustomobject]@{
                model_profile = "windows-universal"
                package_type = "ofx_bundle"
                bundle_track = "dml"
                release_label = "Windows DirectML"
                optimization_profile_id = "windows-directml"
                optimization_profile_label = "Windows DirectML"
                backend_intent = "dml"
                fallback_policy = "experimental_gpu_then_cpu_tolerant_workflows"
                warmup_policy = "provider_specific_session_warmup"
                certification_tier = "experimental"
                unrestricted_quality_attempt = $false
                expects_compiled_context_models = $false
            }
        }
        default {
            return Get-CorridorKeyModelProfileContract -ModelProfile "windows-rtx"
        }
    }
}

function Get-CorridorKeyExpectedCompiledContextModels {
    param(
        [string[]]$PresentModels,
        [ValidateSet("windows-rtx", "windows-universal")]
        [string]$ModelProfile = "windows-rtx"
    )

    $contract = Get-CorridorKeyModelProfileContract -ModelProfile $ModelProfile
    if (-not $contract.expects_compiled_context_models) {
        return @()
    }

    return @(
        $PresentModels |
            Where-Object { $_ -match '^corridorkey_fp16_[0-9]+\.onnx$' } |
            ForEach-Object { ([System.IO.Path]::GetFileNameWithoutExtension($_)) + "_ctx.onnx" }
    )
}

function Get-CorridorKeyExpectedTorchTensorRtArtifacts {
    param(
        [string[]]$PresentModels,
        [ValidateSet("windows-rtx", "windows-universal")]
        [string]$ModelProfile = "windows-rtx"
    )

    if ($ModelProfile -ne "windows-rtx") {
        return @()
    }

    return @(
        $PresentModels |
            Where-Object { $_ -match '^corridorkey_fp16_[0-9]+\.onnx$' } |
            ForEach-Object {
                if ($_ -match 'corridorkey_fp16_([0-9]+)\.onnx') {
                    "corridorkey_torchtrt_fp16_$($Matches[1]).ts"
                }
            }
    )
}

function Get-CorridorKeyWindowsOfxReleaseVariants {
    param(
        [ValidateSet("rtx", "dml", "all")]
        [string]$Track = "all"
    )

    $variants = @()

    if ($Track -in @("rtx", "all")) {
        $variants += [pscustomobject]@{
            Label = "RTX"
            Suffix = "RTX"
            Track = "rtx"
            ModelProfile = "windows-rtx"
        }
    }

    if ($Track -in @("dml", "all")) {
        $variants += [pscustomobject]@{
            Label = "DirectML"
            Suffix = "DirectML"
            Track = "dml"
            ModelProfile = "windows-universal"
        }
    }

    return $variants
}

function Get-CorridorKeyPortableRuntimeTargetModels {
    return @(
        "corridorkey_fp16_768.onnx",
        "corridorkey_fp16_1024.onnx",
        "corridorkey_int8_512.onnx"
    )
}

function Get-CorridorKeyModelInventory {
    param(
        [string]$ModelsDir,
        [string[]]$ExpectedModels
    )

    $presentModels = @()
    $missingModels = @()
    foreach ($model in $ExpectedModels) {
        $sourcePath = Join-Path $ModelsDir $model
        if (Test-Path $sourcePath) {
            $presentModels += $model
        } else {
            $missingModels += $model
        }
    }

    return [ordered]@{
        expected_models = @($ExpectedModels)
        present_models = @($presentModels)
        missing_models = @($missingModels)
        present_count = @($presentModels).Count
        missing_count = @($missingModels).Count
    }
}

function Write-CorridorKeyJsonFile {
    param(
        [string]$Path,
        [object]$Payload
    )

    $directory = Split-Path -Parent $Path
    if (-not [string]::IsNullOrWhiteSpace($directory) -and -not (Test-Path $directory)) {
        New-Item -ItemType Directory -Path $directory -Force | Out-Null
    }

    $json = $Payload | ConvertTo-Json -Depth 8
    $utf8NoBom = [System.Text.UTF8Encoding]::new($false)
    [System.IO.File]::WriteAllText($Path, $json, $utf8NoBom)
}

function Test-CorridorKeyPsProperty {
    param(
        [object]$Object,
        [string]$Name
    )

    if ($null -eq $Object) {
        return $false
    }

    return $Object.PSObject.Properties.Match($Name).Count -gt 0
}

function Test-CorridorKeyDoctorMissingModelProbeFailuresOnly {
    param(
        [object]$Doctor,
        [string[]]$MissingModels
    )

    if ($null -eq $Doctor -or @($MissingModels).Count -eq 0) {
        return $false
    }

    if (-not (Test-CorridorKeyPsProperty -Object $Doctor -Name "windows_universal")) {
        return $false
    }

    $windowsUniversal = $Doctor.windows_universal
    if ($null -eq $windowsUniversal -or
        -not (Test-CorridorKeyPsProperty -Object $windowsUniversal -Name "execution_probes")) {
        return $false
    }

    $failedProbes = @(
        @($windowsUniversal.execution_probes) |
            Where-Object {
                (-not [bool]$_.session_create_ok) -or (-not [bool]$_.frame_execute_ok)
            }
    )
    if ($failedProbes.Count -eq 0) {
        return $false
    }

    foreach ($probe in $failedProbes) {
        if ($null -eq $probe -or -not (Test-CorridorKeyPsProperty -Object $probe -Name "model")) {
            return $false
        }

        $model = [string]$probe.model
        if ([string]::IsNullOrWhiteSpace($model) -or @($MissingModels) -notcontains $model) {
            return $false
        }

        $modelFound = $true
        if (Test-CorridorKeyPsProperty -Object $probe -Name "model_found") {
            $modelFound = [bool]$probe.model_found
        }
        if ($modelFound) {
            return $false
        }

        $error = ""
        if (Test-CorridorKeyPsProperty -Object $probe -Name "error") {
            $error = [string]$probe.error
        }

        $expectedError = "Model not found: $model"
        if ($error -ne "Model not found" -and $error -ne $expectedError) {
            return $false
        }
    }

    return $true
}

function Read-CorridorKeyBundleValidationReport {
    param([string]$ValidationReportPath)

    if (-not (Test-Path $ValidationReportPath)) {
        throw "Bundle validation report not found: $ValidationReportPath"
    }

    $rawJson = Get-Content -Path $ValidationReportPath -Raw -ErrorAction Stop
    if ([string]::IsNullOrWhiteSpace($rawJson)) {
        throw "Bundle validation report is empty: $ValidationReportPath"
    }

    return $rawJson | ConvertFrom-Json
}

function Get-CorridorKeyBundleValidationIssues {
    param(
        [object]$Validation
    )

    $issues = @()
    if ($null -eq $Validation) {
        return @("Bundle validation payload is empty.")
    }

    if (-not (Test-CorridorKeyPsProperty -Object $Validation -Name "validation_passed") -or
        -not [bool]$Validation.validation_passed) {
        $issues += "Bundle validation did not pass."
    }

    if (-not (Test-CorridorKeyPsProperty -Object $Validation -Name "models") -or
        $null -eq $Validation.models) {
        $issues += "Bundle validation is missing the models payload."
        return @($issues)
    }

    $modelsPayload = $Validation.models
    $missingModelCount = if (Test-CorridorKeyPsProperty -Object $modelsPayload -Name "missing_count") {
        [int]$modelsPayload.missing_count
    } else {
        0
    }

    if (-not (Test-CorridorKeyPsProperty -Object $modelsPayload -Name "inventory_contract") -or
        $null -eq $modelsPayload.inventory_contract) {
        $issues += "Bundle validation is missing the inventory contract payload."
    } else {
        $inventoryContract = $modelsPayload.inventory_contract
        if (-not (Test-CorridorKeyPsProperty -Object $inventoryContract -Name "complete") -or
            -not [bool]$inventoryContract.complete) {
            $issues += "Bundle inventory contract is incomplete."
        }

        $expectedContract = if (Test-CorridorKeyPsProperty -Object $inventoryContract -Name "expected_contract") {
            $inventoryContract.expected_contract
        } else {
            $null
        }
        $expectsCompiledContexts = $false
        if ($null -ne $expectedContract -and
            (Test-CorridorKeyPsProperty -Object $expectedContract -Name "bundle_track") -and
            [string]$expectedContract.bundle_track -eq "rtx") {
            $expectsCompiledContexts = $true
        }

        if ($expectsCompiledContexts -and
            ((-not (Test-CorridorKeyPsProperty -Object $inventoryContract -Name "compiled_context_complete")) -or
             (-not [bool]$inventoryContract.compiled_context_complete))) {
            $issues += "Bundle inventory contract requires complete compiled TensorRT context models."
        }

        if ($expectsCompiledContexts) {
            if ((-not (Test-CorridorKeyPsProperty -Object $modelsPayload -Name "certification_contract")) -or
                $null -eq $modelsPayload.certification_contract) {
                $issues += "Bundle validation is missing the RTX certification contract payload."
            } elseif ((-not (Test-CorridorKeyPsProperty -Object $modelsPayload.certification_contract -Name "complete")) -or
                     (-not [bool]$modelsPayload.certification_contract.complete)) {
                $issues += "Bundle RTX certification contract is incomplete."
            }
        }
    }

    if (-not (Test-CorridorKeyPsProperty -Object $Validation -Name "doctor") -or
        $null -eq $Validation.doctor) {
        $issues += "Bundle validation is missing the doctor payload."
        return @($issues)
    }

    $doctorPayload = $Validation.doctor
    $doctorSucceeded = (Test-CorridorKeyPsProperty -Object $doctorPayload -Name "succeeded") -and
        [bool]$doctorPayload.succeeded
    $doctorHealthy = (Test-CorridorKeyPsProperty -Object $doctorPayload -Name "healthy") -and
        [bool]$doctorPayload.healthy
    $doctorFailureTolerated = (Test-CorridorKeyPsProperty -Object $doctorPayload -Name "failure_tolerated") -and
        [bool]$doctorPayload.failure_tolerated

    if (-not $doctorFailureTolerated) {
        if (-not $doctorSucceeded) {
            $issues += "Packaged runtime doctor did not succeed."
        }
        if (-not $doctorHealthy) {
            $issues += "Packaged runtime doctor reported unhealthy status."
        }
    } else {
        if ($missingModelCount -le 0) {
            $issues += "Doctor failure was tolerated even though the bundle is not a partial model package."
        }
        if ((-not (Test-CorridorKeyPsProperty -Object $doctorPayload -Name "failure_reason")) -or
            [string]::IsNullOrWhiteSpace([string]$doctorPayload.failure_reason)) {
            $issues += "Doctor failure was tolerated without a recorded failure reason."
        }
    }

    if ((Test-CorridorKeyPsProperty -Object $doctorPayload -Name "model_contracts_healthy") -and
        (-not [bool]$doctorPayload.model_contracts_healthy) -and
        (-not $doctorFailureTolerated)) {
        $issues += "Packaged runtime doctor reported unhealthy model contracts."
    }

    return @($issues)
}

function Assert-CorridorKeyBundleValidationHealthy {
    param(
        [string]$ValidationReportPath,
        [string]$Label = "packaged bundle"
    )

    $validation = Read-CorridorKeyBundleValidationReport -ValidationReportPath $ValidationReportPath
    $issues = Get-CorridorKeyBundleValidationIssues -Validation $validation
    if (@($issues).Count -gt 0) {
        throw "$Label validation is not acceptable. Issues: $($issues -join ' | ')"
    }

    return $validation
}

function Get-CorridorKeyFileSha256 {
    param([string]$Path)

    if (-not (Test-Path $Path)) {
        throw "Cannot hash a missing file: $Path"
    }

    return ([string](Get-FileHash -Path $Path -Algorithm SHA256).Hash).ToUpperInvariant()
}

function Get-CorridorKeyWindowsRtxArtifactManifestPath {
    param([string]$ModelsDir)

    return Join-Path $ModelsDir "artifact_manifest.json"
}

function Write-CorridorKeyWindowsRtxArtifactManifest {
    param(
        [string]$ModelsDir,
        [string]$ValidationReportPath,
        [string]$OutputPath = ""
    )

    if ([string]::IsNullOrWhiteSpace($OutputPath)) {
        $OutputPath = Get-CorridorKeyWindowsRtxArtifactManifestPath -ModelsDir $ModelsDir
    }

    if (-not (Test-Path $ModelsDir)) {
        throw "Cannot write RTX artifact manifest because the models directory does not exist: $ModelsDir"
    }

    if (-not (Test-Path $ValidationReportPath)) {
        throw "Cannot write RTX artifact manifest because the certification report does not exist: $ValidationReportPath"
    }

    $validation = Get-Content -Path $ValidationReportPath -Raw -ErrorAction Stop | ConvertFrom-Json
    if (-not (Test-CorridorKeyPsProperty -Object $validation -Name "all_profiles_certified") -or
        -not [bool]$validation.all_profiles_certified) {
        throw "RTX artifact manifest requires a certification report with all_profiles_certified=true."
    }

    $certifiedModels = @($validation.certified_models)
    $artifactFiles = Get-ChildItem -Path $ModelsDir -Filter "corridorkey*.onnx" -File -ErrorAction Stop |
        Sort-Object -Property Name

    $artifacts = foreach ($artifactFile in $artifactFiles) {
        $filename = $artifactFile.Name
        $isCompiledContext = $filename -like "*_ctx.onnx"
        $isFp16Runtime = $filename -match '^corridorkey_fp16_[0-9]+\.onnx$'
        $baseModel = if ($isCompiledContext) {
            $filename -replace '_ctx\.onnx$', '.onnx'
        } else {
            $filename
        }

        $artifactKind = if ($isCompiledContext) {
            "compiled_context"
        } elseif ($isFp16Runtime) {
            "runtime_model"
        } else {
            "supporting_model"
        }

        $certified = if ($isCompiledContext) {
            $certifiedModels -contains $baseModel
        } elseif ($isFp16Runtime) {
            $certifiedModels -contains $filename
        } else {
            $false
        }

        $resolution = $null
        if ($baseModel -match '_(\d+)\.onnx$') {
            $resolution = [int]$Matches[1]
        }

        [ordered]@{
            filename = $filename
            sha256 = Get-CorridorKeyFileSha256 -Path $artifactFile.FullName
            artifact_kind = $artifactKind
            base_model = $baseModel
            certified = $certified
            resolution = $resolution
        }
    }

    $payload = [ordered]@{
        manifest_type = "windows_rtx_artifact_manifest"
        bundle_track = "rtx"
        certification_report = [System.IO.Path]::GetFullPath($ValidationReportPath)
        all_profiles_certified = [bool]$validation.all_profiles_certified
        certified_models = @($certifiedModels)
        expected_promoted_models = @(Get-CorridorKeyWindowsRtxPromotedModelList)
        artifacts = @($artifacts)
    }

    if ((Test-CorridorKeyPsProperty -Object $validation -Name "certification_device") -and
        $null -ne $validation.certification_device) {
        $payload.certification_device = $validation.certification_device
    }

    Write-CorridorKeyJsonFile -Path $OutputPath -Payload $payload
    return [System.IO.Path]::GetFullPath($OutputPath)
}

function Read-CorridorKeyWindowsRtxArtifactManifest {
    param(
        [string]$ModelsDir = "",
        [string]$ArtifactManifestPath = ""
    )

    if ([string]::IsNullOrWhiteSpace($ArtifactManifestPath)) {
        if ([string]::IsNullOrWhiteSpace($ModelsDir)) {
            throw "Read-CorridorKeyWindowsRtxArtifactManifest requires -ModelsDir or -ArtifactManifestPath."
        }
        $ArtifactManifestPath = Get-CorridorKeyWindowsRtxArtifactManifestPath -ModelsDir $ModelsDir
    }

    if (-not (Test-Path $ArtifactManifestPath)) {
        throw "RTX artifact manifest not found: $ArtifactManifestPath. Windows RTX packaging now requires certified artifacts, not only raw models. Run scripts\\windows.ps1 -Task certify-rtx-artifacts -Version X.Y.Z for an existing local model set, or scripts\\windows.ps1 -Task regen-rtx-release -Version X.Y.Z to regenerate and certify the RTX ladder from the checkpoint."
    }

    $rawJson = Get-Content -Path $ArtifactManifestPath -Raw -ErrorAction Stop
    if ([string]::IsNullOrWhiteSpace($rawJson)) {
        throw "RTX artifact manifest is empty: $ArtifactManifestPath"
    }

    return $rawJson | ConvertFrom-Json
}

function Get-CorridorKeyWindowsRtxArtifactManifestIssues {
    param(
        [object]$Manifest,
        [string]$ArtifactsDir,
        [string[]]$ExpectedModels,
        [string[]]$ExpectedCompiledContextModels
    )

    $issues = @()
    if ($null -eq $Manifest) {
        return @("RTX artifact manifest payload is empty.")
    }

    if (-not (Test-CorridorKeyPsProperty -Object $Manifest -Name "manifest_type") -or
        [string]$Manifest.manifest_type -ne "windows_rtx_artifact_manifest") {
        $issues += "RTX artifact manifest type is missing or invalid."
    }

    if (-not (Test-CorridorKeyPsProperty -Object $Manifest -Name "bundle_track") -or
        [string]$Manifest.bundle_track -ne "rtx") {
        $issues += "RTX artifact manifest bundle_track must be 'rtx'."
    }

    if (-not (Test-CorridorKeyPsProperty -Object $Manifest -Name "all_profiles_certified") -or
        -not [bool]$Manifest.all_profiles_certified) {
        $issues += "RTX artifact manifest requires all_profiles_certified=true."
    }

    if (-not (Test-CorridorKeyPsProperty -Object $Manifest -Name "artifacts") -or
        $null -eq $Manifest.artifacts -or
        $Manifest.artifacts -isnot [System.Array]) {
        $issues += "RTX artifact manifest must include an artifacts array."
        return @($issues)
    }

    $artifactIndex = @{}
    foreach ($artifact in @($Manifest.artifacts)) {
        if ($null -eq $artifact -or -not (Test-CorridorKeyPsProperty -Object $artifact -Name "filename")) {
            $issues += "RTX artifact manifest contains an invalid artifact entry."
            continue
        }

        $filename = [string]$artifact.filename
        if ([string]::IsNullOrWhiteSpace($filename)) {
            $issues += "RTX artifact manifest contains an artifact with an empty filename."
            continue
        }

        $artifactIndex[$filename] = $artifact
    }

    foreach ($expectedModel in @($ExpectedModels)) {
        if (-not $artifactIndex.ContainsKey($expectedModel)) {
            $issues += "RTX artifact manifest is missing packaged model '$expectedModel'."
            continue
        }

        $artifact = $artifactIndex[$expectedModel]
        $artifactPath = Join-Path $ArtifactsDir $expectedModel
        if (-not (Test-Path $artifactPath)) {
            $issues += "Packaged model '$expectedModel' is missing from $ArtifactsDir."
            continue
        }

        $actualHash = Get-CorridorKeyFileSha256 -Path $artifactPath
        if (-not (Test-CorridorKeyPsProperty -Object $artifact -Name "sha256") -or
            [string]::IsNullOrWhiteSpace([string]$artifact.sha256)) {
            $issues += "RTX artifact manifest is missing sha256 for '$expectedModel'."
        } elseif ([string]$artifact.sha256 -ne $actualHash) {
            $issues += "RTX artifact manifest hash mismatch for '$expectedModel'."
        }

        if ($expectedModel -match '^corridorkey_fp16_[0-9]+\.onnx$') {
            if ((-not (Test-CorridorKeyPsProperty -Object $artifact -Name "certified")) -or
                (-not [bool]$artifact.certified)) {
                $issues += "RTX artifact manifest does not certify '$expectedModel'."
            }
        }
    }

    foreach ($expectedContext in @($ExpectedCompiledContextModels)) {
        if (-not $artifactIndex.ContainsKey($expectedContext)) {
            $issues += "RTX artifact manifest is missing compiled context '$expectedContext'."
            continue
        }

        $artifact = $artifactIndex[$expectedContext]
        $artifactPath = Join-Path $ArtifactsDir $expectedContext
        if (-not (Test-Path $artifactPath)) {
            $issues += "Compiled context '$expectedContext' is missing from $ArtifactsDir."
            continue
        }

        $actualHash = Get-CorridorKeyFileSha256 -Path $artifactPath
        if (-not (Test-CorridorKeyPsProperty -Object $artifact -Name "sha256") -or
            [string]::IsNullOrWhiteSpace([string]$artifact.sha256)) {
            $issues += "RTX artifact manifest is missing sha256 for '$expectedContext'."
        } elseif ([string]$artifact.sha256 -ne $actualHash) {
            $issues += "RTX artifact manifest hash mismatch for '$expectedContext'."
        }

        if ((-not (Test-CorridorKeyPsProperty -Object $artifact -Name "certified")) -or
            (-not [bool]$artifact.certified)) {
            $issues += "RTX artifact manifest does not certify '$expectedContext'."
        }
    }

    return @($issues)
}

function Assert-CorridorKeyWindowsRtxArtifactManifestHealthy {
    param(
        [string]$ArtifactsDir,
        [string[]]$ExpectedModels,
        [string[]]$ExpectedCompiledContextModels,
        [string]$ArtifactManifestPath = "",
        [string]$Label = "Windows RTX artifact set"
    )

    $manifest = Read-CorridorKeyWindowsRtxArtifactManifest -ModelsDir $ArtifactsDir -ArtifactManifestPath $ArtifactManifestPath
    $issues = Get-CorridorKeyWindowsRtxArtifactManifestIssues `
        -Manifest $manifest `
        -ArtifactsDir $ArtifactsDir `
        -ExpectedModels $ExpectedModels `
        -ExpectedCompiledContextModels $ExpectedCompiledContextModels
    if (@($issues).Count -gt 0) {
        throw "$Label does not match a certified RTX artifact manifest. Issues: $($issues -join ' | ')"
    }

    return $manifest
}
