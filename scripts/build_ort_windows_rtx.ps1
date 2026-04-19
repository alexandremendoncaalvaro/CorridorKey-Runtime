param(
    [string]$OrtSourceDir = "",
    [string]$InstallDir = "",
    [string]$BuildConfig = "Release",
    [string]$CudaHome = "",
    [string]$TensorRtRtxHome = "",
    [string]$VsDevCmd = "",
    [string]$PythonExe = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot "windows_runtime_helpers.ps1")
$rtxBuildContract = Get-CorridorKeyWindowsRtxBuildContract
$cmakeGenerator = $rtxBuildContract.cmake_generator
$buildDirName = "windows-rtx-vs2022"

function Resolve-VsDevCmd {
    param([string]$ExplicitPath)
    $resolvedPath = Resolve-CorridorKeyVsDevCmdPath -ExplicitPath $ExplicitPath
    if (-not [string]::IsNullOrWhiteSpace($resolvedPath)) {
        return $resolvedPath
    }

    throw "Unable to locate VsDevCmd.bat. Pass -VsDevCmd or run from a Visual Studio developer shell."
}

function Resolve-PythonExe {
    param([string]$ExplicitPath)

    $resolvedPath = Resolve-CorridorKeyPython312Path -ExplicitPath $ExplicitPath
    if (-not [string]::IsNullOrWhiteSpace($resolvedPath)) {
        return $resolvedPath
    }

    throw "Python $($rtxBuildContract.required_python_version) was not found. Install Python $($rtxBuildContract.required_python_version) or pass -PythonExe."
}

function Resolve-CmakePath {
    $resolved = Resolve-CorridorKeyWindowsCmake -MinimumVersion $rtxBuildContract.minimum_cmake_version
    if ($resolved.meets_minimum) {
        return $resolved.path
    }

    throw "CMake $($rtxBuildContract.minimum_cmake_version)+ was not found. Install CMake $($rtxBuildContract.minimum_cmake_version) or newer."
}

function Resolve-CmakeVersion {
    param([string]$CmakePath)

    $resolvedVersion = Get-CorridorKeyCmakeVersion -CmakePath $CmakePath
    if (-not [string]::IsNullOrWhiteSpace($resolvedVersion)) {
        return $resolvedVersion
    }

    throw "Could not parse CMake version from $CmakePath"
}

function Assert-CmakeVersion {
    param(
        [string]$CmakePath,
        [string]$MinimumVersion = ""
    )

    $resolvedMinimumVersion = if ([string]::IsNullOrWhiteSpace($MinimumVersion)) {
        $rtxBuildContract.minimum_cmake_version
    } else {
        $MinimumVersion
    }

    $resolvedVersion = Resolve-CmakeVersion -CmakePath $CmakePath
    if ([version]$resolvedVersion -lt [version]$resolvedMinimumVersion) {
        throw "ONNX Runtime RTX builds require CMake $resolvedMinimumVersion or newer. Resolved: $CmakePath ($resolvedVersion)"
    }
}

function Assert-PythonVersion {
    param([string]$ExecutablePath)

    $version = & $ExecutablePath -c "import sys; print(f'{sys.version_info.major}.{sys.version_info.minor}')" 2>$null
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to query Python version from $ExecutablePath"
    }

    if ($version.Trim() -ne $rtxBuildContract.required_python_version) {
        throw "TensorRT RTX ONNX Runtime builds require Python $($rtxBuildContract.required_python_version). Resolved: $ExecutablePath ($($version.Trim()))."
    }
}

function Resolve-OrtSourceDir {
    param(
        [string]$ExplicitPath,
        [string]$RepoRoot
    )

    if (-not [string]::IsNullOrWhiteSpace($ExplicitPath)) {
        return $ExplicitPath
    }

    foreach ($candidate in @(
            (Join-Path $RepoRoot "vendor\onnxruntime-src"),
            (Join-Path $RepoRoot "vendor\onnxruntime-source")
        )) {
        if (Test-Path (Join-Path $candidate "build.bat")) {
            return $candidate
        }
    }

    throw "Set -OrtSourceDir to an ONNX Runtime source checkout, or place it at vendor\onnxruntime-src."
}

function Merge-DirectoryContents {
    param(
        [string]$SourceDir,
        [string]$DestinationDir
    )

    if (-not (Test-Path $SourceDir)) {
        return
    }

    New-Item -ItemType Directory -Force -Path $DestinationDir | Out-Null
    Get-ChildItem -Path $SourceDir -Force | Copy-Item -Destination $DestinationDir -Recurse -Force
}

function Test-CudaToolkitRoot {
    param([string]$CandidatePath)

    return (Test-Path (Join-Path $CandidatePath "bin\nvcc.exe")) -and
           (Test-Path (Join-Path $CandidatePath "include\cuda_runtime.h")) -and
           (Test-Path (Join-Path $CandidatePath "lib\x64\cudart.lib"))
}

function Get-ComponentRoots {
    param([string]$ExtractedRoot)

    $roots = [System.Collections.Generic.List[string]]::new()
    foreach ($entry in Get-ChildItem -Path $ExtractedRoot -Directory -ErrorAction SilentlyContinue) {
        $candidatePaths = @($entry.FullName)
        $candidatePaths += Get-ChildItem -Path $entry.FullName -Directory -ErrorAction SilentlyContinue |
            Select-Object -ExpandProperty FullName

        foreach ($candidatePath in ($candidatePaths | Select-Object -Unique)) {
            if ((Test-Path (Join-Path $candidatePath "bin")) -or
                (Test-Path (Join-Path $candidatePath "include")) -or
                (Test-Path (Join-Path $candidatePath "lib")) -or
                (Test-Path (Join-Path $candidatePath "nvvm")) -or
                (Test-Path (Join-Path $candidatePath "libdevice"))) {
                [void]$roots.Add($candidatePath)
            }
        }
    }

    return $roots | Select-Object -Unique
}

function Resolve-CudaVersionLabel {
    param([string]$ExtractedRoot)

    $versionJson = Join-Path $ExtractedRoot "CUDAToolkit\version.json"
    if (Test-Path $versionJson) {
        try {
            $metadata = Get-Content -Path $versionJson -Raw | ConvertFrom-Json
            if ($null -ne $metadata.cuda -and -not [string]::IsNullOrWhiteSpace($metadata.cuda.version)) {
                return $metadata.cuda.version
            }
        } catch {
        }
    }

    $leaf = Split-Path $ExtractedRoot -Leaf
    if ($leaf -match '(\d+\.\d+(\.\d+)?)') {
        return $Matches[1]
    }

    return "local"
}

function Materialize-ComponentizedCudaToolkit {
    param([string]$ExtractedRoot)

    $versionLabel = Resolve-CudaVersionLabel -ExtractedRoot $ExtractedRoot
    $parentDir = Split-Path -Parent $ExtractedRoot
    $destinationRoot = Join-Path $parentDir ("cuda-" + $versionLabel + "-local")

    if (Test-CudaToolkitRoot -CandidatePath $destinationRoot) {
        return $destinationRoot
    }

    Write-Host "Materializing local CUDA toolkit at $destinationRoot ..."
    New-Item -ItemType Directory -Force -Path (Join-Path $destinationRoot "bin") | Out-Null
    New-Item -ItemType Directory -Force -Path (Join-Path $destinationRoot "include") | Out-Null
    New-Item -ItemType Directory -Force -Path (Join-Path $destinationRoot "lib\x64") | Out-Null

    $versionJson = Join-Path $ExtractedRoot "CUDAToolkit\version.json"
    if (Test-Path $versionJson) {
        Copy-Item $versionJson -Destination (Join-Path $destinationRoot "version.json") -Force
    }

    foreach ($componentRoot in Get-ComponentRoots -ExtractedRoot $ExtractedRoot) {
        Merge-DirectoryContents -SourceDir (Join-Path $componentRoot "bin") `
            -DestinationDir (Join-Path $destinationRoot "bin")
        Merge-DirectoryContents -SourceDir (Join-Path $componentRoot "include") `
            -DestinationDir (Join-Path $destinationRoot "include")
        Merge-DirectoryContents -SourceDir (Join-Path $componentRoot "lib\x64") `
            -DestinationDir (Join-Path $destinationRoot "lib\x64")
        Merge-DirectoryContents -SourceDir (Join-Path $componentRoot "extras") `
            -DestinationDir (Join-Path $destinationRoot "extras")
        Merge-DirectoryContents -SourceDir (Join-Path $componentRoot "nvvm") `
            -DestinationDir (Join-Path $destinationRoot "nvvm")
        Merge-DirectoryContents -SourceDir (Join-Path $componentRoot "libdevice") `
            -DestinationDir (Join-Path $destinationRoot "libdevice")
    }

    if (-not (Test-CudaToolkitRoot -CandidatePath $destinationRoot)) {
        throw "Failed to materialize a usable CUDA toolkit root from: $ExtractedRoot"
    }

    return $destinationRoot
}

function Normalize-CudaHome {
    param([string]$CandidatePath)

    if (Test-CudaToolkitRoot -CandidatePath $CandidatePath) {
        return $CandidatePath
    }

    if ((Test-Path (Join-Path $CandidatePath "cuda_nvcc")) -and
        (Test-Path (Join-Path $CandidatePath "cuda_cudart"))) {
        return (Materialize-ComponentizedCudaToolkit -ExtractedRoot $CandidatePath)
    }

    throw "CUDA toolkit root is unusable: $CandidatePath"
}

function Resolve-CudaHome {
    param(
        [string]$ExplicitPath,
        [string]$RepoRoot
    )

    if (-not [string]::IsNullOrWhiteSpace($ExplicitPath)) {
        return (Normalize-CudaHome -CandidatePath $ExplicitPath)
    }

    if (-not [string]::IsNullOrWhiteSpace($env:CUDA_PATH)) {
        return (Normalize-CudaHome -CandidatePath $env:CUDA_PATH)
    }

    $cudaRoot = Join-Path ${env:ProgramFiles} "NVIDIA GPU Computing Toolkit\CUDA"
    if (Test-Path $cudaRoot) {
        $candidate = Get-ChildItem -Path $cudaRoot -Directory -Filter "v*" |
            Sort-Object Name -Descending | Select-Object -First 1
        if ($null -ne $candidate) {
            return (Normalize-CudaHome -CandidatePath $candidate.FullName)
        }
    }

    $vendorRoot = Join-Path $RepoRoot "vendor"
    if (Test-Path $vendorRoot) {
        $candidate = Get-ChildItem -Path $vendorRoot -Directory |
            Where-Object { $_.Name -match '^(cuda-|CUDA-).*(extracted|local)?$' } |
            Sort-Object Name -Descending | Select-Object -First 1
        if ($null -ne $candidate) {
            return (Normalize-CudaHome -CandidatePath $candidate.FullName)
        }
    }

    throw "CUDA 12.9+ home not found. Pass -CudaHome or install CUDA Toolkit."
}

function Test-TensorRtRtxRoot {
    param([string]$CandidatePath)

    $hasInclude = Test-Path (Join-Path $CandidatePath "include\NvInfer.h")
    $hasLib = (Get-ChildItem -Path (Join-Path $CandidatePath "lib") -Filter "tensorrt_rtx*.lib" `
        -File -ErrorAction SilentlyContinue | Measure-Object).Count -gt 0
    $hasBin = (Get-ChildItem -Path (Join-Path $CandidatePath "bin") -Filter "tensorrt_rtx*.dll" `
        -File -ErrorAction SilentlyContinue | Measure-Object).Count -gt 0

    return $hasInclude -and $hasLib -and $hasBin
}

function Normalize-TensorRtRtxHome {
    param([string]$CandidatePath)

    if (Test-TensorRtRtxRoot -CandidatePath $CandidatePath) {
        return $CandidatePath
    }

    $nestedCandidate = Get-ChildItem -Path $CandidatePath -Directory -ErrorAction SilentlyContinue |
        Where-Object { Test-TensorRtRtxRoot -CandidatePath $_.FullName } |
        Sort-Object Name | Select-Object -First 1
    if ($null -ne $nestedCandidate) {
        return $nestedCandidate.FullName
    }

    throw "TensorRT RTX root is unusable: $CandidatePath"
}

function Resolve-TensorRtRtxHome {
    param(
        [string]$ExplicitPath,
        [string]$RepoRoot
    )

    if (-not [string]::IsNullOrWhiteSpace($ExplicitPath)) {
        return (Normalize-TensorRtRtxHome -CandidatePath $ExplicitPath)
    }

    if (-not [string]::IsNullOrWhiteSpace($env:TENSORRT_RTX_HOME)) {
        return (Normalize-TensorRtRtxHome -CandidatePath $env:TENSORRT_RTX_HOME)
    }

    $vendorRoot = Join-Path $RepoRoot "vendor"
    if (Test-Path $vendorRoot) {
        $candidate = Get-ChildItem -Path $vendorRoot -Directory |
            Where-Object { $_.Name -match '^(TensorRT-RTX|tensorrt-rtx)' } |
            Sort-Object Name -Descending | Select-Object -First 1
        if ($null -ne $candidate) {
            return (Normalize-TensorRtRtxHome -CandidatePath $candidate.FullName)
        }
    }

    # SDK not staged yet; auto-download the pinned version. The download
    # URL lives in the rtx_build_contract so every pipeline (release,
    # prepare-rtx, collaborator) fetches the same SDK from the same place.
    Write-Host "[build_ort_windows_rtx] TensorRT-RTX SDK not staged; fetching pinned version..." -ForegroundColor Yellow
    $resolvedPath = Ensure-CorridorKeyTensorRtRtxHome -RepoRoot $RepoRoot
    return (Normalize-TensorRtRtxHome -CandidatePath $resolvedPath)
}

function Resolve-DumpbinPath {
    $dumpbinCommand = Get-Command "dumpbin.exe" -ErrorAction SilentlyContinue
    if ($null -ne $dumpbinCommand) {
        return $dumpbinCommand.Source
    }

    $toolRoots = @()
    if (-not [string]::IsNullOrWhiteSpace($env:VSINSTALLDIR)) {
        $toolRoots += Join-Path $env:VSINSTALLDIR "VC\Tools\MSVC"
    }

    $vswhereCommand = Get-Command "vswhere.exe" -ErrorAction SilentlyContinue
    if ($null -ne $vswhereCommand) {
        $installationPath = & $vswhereCommand.Source -latest -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        if ($LASTEXITCODE -eq 0 -and -not [string]::IsNullOrWhiteSpace($installationPath)) {
            $toolRoots += Join-Path $installationPath.Trim() "VC\Tools\MSVC"
        }
    }

    foreach ($installationRoot in @(
            "C:\Program Files\Microsoft Visual Studio\2022\Community",
            "C:\Program Files\Microsoft Visual Studio\2022\BuildTools",
            "C:\Program Files\Microsoft Visual Studio\2022\Professional",
            "C:\Program Files\Microsoft Visual Studio\2022\Enterprise"
        )) {
        $toolRoots += Join-Path $installationRoot "VC\Tools\MSVC"
    }

    foreach ($toolRoot in ($toolRoots | Select-Object -Unique)) {
        if (-not (Test-Path $toolRoot)) {
            continue
        }

        $candidate = Get-ChildItem -Path $toolRoot -Directory | Sort-Object Name -Descending |
            ForEach-Object { Join-Path $_.FullName "bin\Hostx64\x64\dumpbin.exe" } |
            Where-Object { Test-Path $_ } | Select-Object -First 1
        if ($null -ne $candidate) {
            return $candidate
        }
    }

    throw "Unable to locate dumpbin.exe. Run from a Visual Studio developer shell or install MSVC tools."
}

function Get-DllImports {
    param(
        [string]$DumpbinPath,
        [string]$DllPath
    )

    $output = & $DumpbinPath /DEPENDENTS $DllPath 2>$null
    if ($LASTEXITCODE -ne 0) {
        return @()
    }

    return $output |
        ForEach-Object { $_.Trim() } |
        Where-Object { $_ -match '^[A-Za-z0-9_.-]+\.dll$' } |
        Select-Object -Unique
}

function Find-DependencyPath {
    param(
        [string]$FileName,
        [string[]]$SearchRoots
    )

    foreach ($root in $SearchRoots) {
        if ([string]::IsNullOrWhiteSpace($root) -or -not (Test-Path $root)) {
            continue
        }

        $match = Get-ChildItem -Path $root -Filter $FileName -File -Recurse -ErrorAction SilentlyContinue |
            Select-Object -First 1
        if ($null -ne $match) {
            return $match.FullName
        }
    }

    return $null
}

function Copy-RuntimeClosure {
    param(
        [string]$DumpbinPath,
        [string[]]$SeedDlls,
        [string[]]$SearchRoots,
        [string]$DestinationDir
    )

    $ignored = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
    foreach ($dll in @(
            "KERNEL32.dll","USER32.dll","GDI32.dll","ADVAPI32.dll","SHELL32.dll","OLE32.dll",
            "OLEAUT32.dll","WS2_32.dll","CRYPT32.dll","BCRYPT.dll","BCRYPTPRIMITIVES.dll",
            "SECUR32.dll","NCRYPT.dll","COMDLG32.dll","VCRUNTIME140.dll","VCRUNTIME140_1.dll",
            "MSVCP140.dll","MSVCP140_1.dll","MSVCP140_2.dll","UCRTBASE.dll","api-ms-win-crt-runtime-l1-1-0.dll",
            "api-ms-win-crt-heap-l1-1-0.dll","api-ms-win-crt-stdio-l1-1-0.dll","api-ms-win-crt-string-l1-1-0.dll",
            "api-ms-win-crt-convert-l1-1-0.dll","api-ms-win-crt-math-l1-1-0.dll","api-ms-win-crt-filesystem-l1-1-0.dll",
            "api-ms-win-crt-environment-l1-1-0.dll","api-ms-win-crt-time-l1-1-0.dll","api-ms-win-crt-locale-l1-1-0.dll",
            "api-ms-win-crt-utility-l1-1-0.dll","api-ms-win-crt-conio-l1-1-0.dll","api-ms-win-crt-process-l1-1-0.dll"
        )) {
        [void]$ignored.Add($dll)
    }

    $copied = [System.Collections.Generic.Dictionary[string,string]]::new([System.StringComparer]::OrdinalIgnoreCase)
    $queue = [System.Collections.Generic.Queue[string]]::new()
    foreach ($seed in $SeedDlls) {
        $queue.Enqueue($seed)
    }

    while ($queue.Count -gt 0) {
        $current = $queue.Dequeue()
        if (-not (Test-Path $current)) {
            continue
        }

        $currentName = [System.IO.Path]::GetFileName($current)
        if ($copied.ContainsKey($currentName)) {
            continue
        }

        Copy-Item $current -Destination (Join-Path $DestinationDir $currentName) -Force
        $copied[$currentName] = $current

        foreach ($dependency in Get-DllImports -DumpbinPath $DumpbinPath -DllPath $current) {
            if ($ignored.Contains($dependency) -or $copied.ContainsKey($dependency)) {
                continue
            }

            $resolved = Find-DependencyPath -FileName $dependency -SearchRoots $SearchRoots
            if ($null -ne $resolved) {
                $queue.Enqueue($resolved)
            }
        }
    }

    return $copied.Values
}

function Get-OrtBuildLogHints {
    param(
        [string]$BuildDir,
        [string]$BuildConfig
    )

    $candidates = @(
        (Join-Path $BuildDir "CMakeFiles\CMakeError.log"),
        (Join-Path $BuildDir "CMakeFiles\CMakeOutput.log"),
        (Join-Path $BuildDir (Join-Path $BuildConfig "CMakeFiles\CMakeError.log")),
        (Join-Path $BuildDir (Join-Path $BuildConfig "CMakeFiles\CMakeOutput.log")),
        (Join-Path $BuildDir (Join-Path $BuildConfig (Join-Path $BuildConfig "CMakeFiles\CMakeError.log"))),
        (Join-Path $BuildDir (Join-Path $BuildConfig (Join-Path $BuildConfig "CMakeFiles\CMakeOutput.log")))
    )

    return @($candidates | Where-Object { Test-Path $_ } | Select-Object -Unique)
}

if ([string]::IsNullOrWhiteSpace($InstallDir)) {
    $InstallDir = Join-Path (Split-Path -Parent $PSScriptRoot) "vendor\onnxruntime-windows-rtx"
}

$OrtSourceDir = Resolve-OrtSourceDir -ExplicitPath $OrtSourceDir -RepoRoot $repoRoot
$OrtSourceDir = [System.IO.Path]::GetFullPath($OrtSourceDir)
$InstallDir = [System.IO.Path]::GetFullPath($InstallDir)
$CudaHome = Resolve-CudaHome -ExplicitPath $CudaHome -RepoRoot $repoRoot
$TensorRtRtxHome = Resolve-TensorRtRtxHome -ExplicitPath $TensorRtRtxHome -RepoRoot $repoRoot
$VsDevCmd = Resolve-VsDevCmd -ExplicitPath $VsDevCmd
$PythonExe = Resolve-PythonExe -ExplicitPath $PythonExe
$CmakePath = Resolve-CmakePath
Assert-PythonVersion -ExecutablePath $PythonExe
Assert-CmakeVersion -CmakePath $CmakePath

if (-not (Test-Path $OrtSourceDir)) {
    throw "ONNX Runtime source directory not found: $OrtSourceDir"
}

if (-not (Test-Path $CudaHome)) {
    throw "CUDA home not found: $CudaHome"
}

if (-not (Test-Path $TensorRtRtxHome)) {
    throw "TensorRT RTX home not found: $TensorRtRtxHome"
}

if (-not (Test-Path $VsDevCmd)) {
    throw "VsDevCmd.bat not found: $VsDevCmd"
}

$buildDir = Join-Path $OrtSourceDir ("build\" + $buildDirName)
$dumpbinPath = Resolve-DumpbinPath
$cmakeDir = Split-Path -Parent $CmakePath
$pythonDir = Split-Path -Parent $PythonExe
$cudaBinDir = Join-Path $CudaHome "bin"
$cudaCompiler = Join-Path $cudaBinDir "nvcc.exe"
$buildPy = Join-Path $OrtSourceDir "tools\ci_build\build.py"
$cmakeVersion = Resolve-CmakeVersion -CmakePath $CmakePath
$vswhereDir = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer"
$command = @(
    "set `"PATH=$vswhereDir;$cmakeDir;%PATH%`"",
    "set `"PATH=$pythonDir;%PATH%`"",
    "set `"PATH=$cudaBinDir;%PATH%`"",
    "set `"CUDA_PATH=$CudaHome`"",
    "set `"CUDAToolkit_ROOT=$CudaHome`"",
    "call `"$VsDevCmd`" -arch=x64",
    "cd /d `"$OrtSourceDir`"",
    "`"$PythonExe`" `"$buildPy`" --config $BuildConfig --build_dir `"$buildDir`" --parallel --use_nv_tensorrt_rtx --tensorrt_rtx_home `"$TensorRtRtxHome`" --cuda_home `"$CudaHome`" --cmake_path `"$CmakePath`" --cmake_generator `"$cmakeGenerator`" --build_shared_lib --skip_tests --build --update --use_vcpkg --cmake_extra_defines CUDAToolkit_ROOT=`"$CudaHome`" CMAKE_CUDA_COMPILER=`"$cudaCompiler`""
) -join " && "

Write-Host "[1/3] Building ONNX Runtime with TensorRT RTX support..."
Write-Host "Using Python: $PythonExe"
Write-Host "Using CMake: $CmakePath ($cmakeVersion)"
Write-Host "Using generator: $cmakeGenerator"
cmd.exe /c $command
if ($LASTEXITCODE -ne 0) {
    $logHints = @(Get-OrtBuildLogHints -BuildDir $buildDir -BuildConfig $BuildConfig)
    if ($logHints.Count -gt 0) {
        Write-Host "[build-ort] Likely CMake logs:" -ForegroundColor Yellow
        foreach ($logHint in $logHints) {
            Write-Host " - $logHint" -ForegroundColor Yellow
        }
    }
    throw "ONNX Runtime build failed."
}

Write-Host "[2/3] Staging curated runtime into $InstallDir ..."
New-Item -ItemType Directory -Force -Path (Join-Path $InstallDir "bin") | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $InstallDir "lib") | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $InstallDir "include") | Out-Null

Copy-Item -Recurse -Force (Join-Path $OrtSourceDir "include\onnxruntime") (Join-Path $InstallDir "include")

$runtimeBinDir = Join-Path $buildDir (Join-Path $BuildConfig $BuildConfig)
if (-not (Test-Path $runtimeBinDir)) {
    throw "ONNX Runtime build output directory not found: $runtimeBinDir"
}

$seedDlls = Get-ChildItem -Path $runtimeBinDir -Filter "onnxruntime*.dll" -File | Select-Object -ExpandProperty FullName
if ($seedDlls.Count -eq 0) {
    throw "No ONNX Runtime DLLs were found in $runtimeBinDir"
}

$searchRoots = @(
    $runtimeBinDir,
    $TensorRtRtxHome,
    (Join-Path $TensorRtRtxHome "bin"),
    (Join-Path $TensorRtRtxHome "lib"),
    $CudaHome,
    (Join-Path $CudaHome "bin")
) | Select-Object -Unique

Get-ChildItem -Path (Join-Path $InstallDir "bin") -Filter "*.dll" -File -ErrorAction SilentlyContinue |
    Remove-Item -Force

$stagedDlls = Copy-RuntimeClosure -DumpbinPath $dumpbinPath -SeedDlls $seedDlls `
    -SearchRoots $searchRoots -DestinationDir (Join-Path $InstallDir "bin")

Get-ChildItem -Path $runtimeBinDir -Filter "onnxruntime*.lib" | Copy-Item -Destination (Join-Path $InstallDir "lib") -Force

Write-Host "[3/3] Done."
Write-Host "Staged runtime DLLs:"
$stagedDlls | Sort-Object | ForEach-Object { Write-Host " - $([System.IO.Path]::GetFileName($_))" }
Write-Host "Set CORRIDORKEY_WINDOWS_ORT_ROOT to: $InstallDir"
