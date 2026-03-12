param(
    [string]$Version = "0.1.0",
    [string]$BuildDir = "",
    [string]$OrtRoot = "",
    [switch]$CompileContexts,
    [string[]]$ForbiddenPathPrefix = @()
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Get-MatchingFiles {
    param(
        [string]$Directory,
        [string[]]$Patterns
    )

    $matches = @()
    foreach ($pattern in $Patterns) {
        $matches += @(Get-ChildItem -Path $Directory -Filter $pattern -File -ErrorAction SilentlyContinue)
    }

    return @($matches | Sort-Object FullName -Unique)
}

function Assert-PortableRuntime {
    param([string]$RuntimeDir)

    if (-not (Test-Path $RuntimeDir)) {
        throw "Runtime directory not found: $RuntimeDir"
    }

    $requirements = @(
        @{ Label = "onnxruntime.dll"; Patterns = @("onnxruntime.dll") },
        @{ Label = "onnxruntime_providers_shared.dll"; Patterns = @("onnxruntime_providers_shared*.dll") },
        @{ Label = "onnxruntime_providers_nv_tensorrt_rtx.dll"; Patterns = @("onnxruntime_providers_nv_tensorrt_rtx*.dll", "onnxruntime_providers_nvtensorrtrtx*.dll") },
        @{ Label = "TensorRT RTX core runtime"; Patterns = @("tensorrt_rtx*.dll") },
        @{ Label = "TensorRT RTX ONNX parser"; Patterns = @("tensorrt_onnxparser_rtx*.dll") },
        @{ Label = "CUDA runtime"; Patterns = @("cudart64*.dll", "cudart*.dll") }
    )

    foreach ($requirement in $requirements) {
        $matches = Get-MatchingFiles -Directory $RuntimeDir -Patterns $requirement.Patterns
        if (@($matches).Count -eq 0) {
            throw "Portable runtime is missing $($requirement.Label) in $RuntimeDir"
        }
    }
}

function Resolve-MsvcRuntimeDir {
    $candidates = @(
        "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Redist\MSVC",
        "C:\Program Files\Microsoft Visual Studio\2022\BuildTools\VC\Redist\MSVC"
    )

    foreach ($root in $candidates) {
        if (-not (Test-Path $root)) {
            continue
        }

        $match = Get-ChildItem -Path $root -Directory -ErrorAction SilentlyContinue |
            Sort-Object Name -Descending |
            ForEach-Object {
                $crtDir = Join-Path $_.FullName "x64\Microsoft.VC143.CRT"
                if (Test-Path $crtDir) {
                    return $crtDir
                }
            }

        if ($null -ne $match -and -not [string]::IsNullOrWhiteSpace($match)) {
            return $match
        }
    }

    return $null
}

function Test-ByteSequence {
    param(
        [byte[]]$Haystack,
        [byte[]]$Needle
    )

    if ($Needle.Length -eq 0 -or $Haystack.Length -lt $Needle.Length) {
        return $false
    }

    for ($index = 0; $index -le ($Haystack.Length - $Needle.Length); $index++) {
        $matched = $true
        for ($needleIndex = 0; $needleIndex -lt $Needle.Length; $needleIndex++) {
            if ($Haystack[$index + $needleIndex] -ne $Needle[$needleIndex]) {
                $matched = $false
                break
            }
        }
        if ($matched) {
            return $true
        }
    }

    return $false
}

function Get-PortableScanTargets {
    param([string]$RootDir)

    $extensions = @(".txt", ".json", ".cmd", ".bat", ".onnx")
    return Get-ChildItem -Path $RootDir -File -Recurse | Where-Object {
        $extensions -contains $_.Extension.ToLowerInvariant()
    }
}

function Normalize-ForbiddenPath {
    param([string]$PathText)

    if ([string]::IsNullOrWhiteSpace($PathText)) {
        return $null
    }

    try {
        return [System.IO.Path]::GetFullPath($PathText).TrimEnd('\')
    } catch {
        return $PathText.TrimEnd('\')
    }
}

function Test-FileForForbiddenPath {
    param(
        [string]$FilePath,
        [string[]]$Markers
    )

    $bytes = [System.IO.File]::ReadAllBytes($FilePath)
    foreach ($marker in $Markers) {
        if ([string]::IsNullOrWhiteSpace($marker)) {
            continue
        }

        $patterns = @(
            [System.Text.Encoding]::UTF8.GetBytes($marker),
            [System.Text.Encoding]::Unicode.GetBytes($marker),
            [System.Text.Encoding]::ASCII.GetBytes($marker)
        )

        foreach ($pattern in $patterns) {
            if (Test-ByteSequence -Haystack $bytes -Needle $pattern) {
                return $marker
            }
        }
    }

    return $null
}

function Assert-NoHardcodedPaths {
    param(
        [string]$RootDir,
        [string[]]$Markers
    )

    $normalizedMarkers = $Markers |
        ForEach-Object { Normalize-ForbiddenPath -PathText $_ } |
        Where-Object { -not [string]::IsNullOrWhiteSpace($_) } |
        Select-Object -Unique

    if ($normalizedMarkers.Count -eq 0) {
        return
    }

    $hits = @()
    foreach ($file in Get-PortableScanTargets -RootDir $RootDir) {
        $marker = Test-FileForForbiddenPath -FilePath $file.FullName -Markers $normalizedMarkers
        if ($null -ne $marker) {
            $hits += [PSCustomObject]@{
                File = $file.FullName
                Marker = $marker
            }
        }
    }

    if ($hits.Count -gt 0) {
        $details = $hits | ForEach-Object { "$($_.File) -> $($_.Marker)" }
        throw "Portable bundle still contains machine-specific paths:`n$($details -join "`n")"
    }
}

$repoRoot = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($BuildDir)) {
    $BuildDir = Join-Path $repoRoot "build\release"
}
if ([string]::IsNullOrWhiteSpace($OrtRoot)) {
    $OrtRoot = $env:CORRIDORKEY_WINDOWS_ORT_ROOT
}
if ([string]::IsNullOrWhiteSpace($OrtRoot)) {
    $OrtRoot = Join-Path $repoRoot "vendor\onnxruntime-windows-rtx"
}

$distDir = Join-Path $repoRoot "dist\CorridorKey_Windows_RTX_v$Version"
$binDir = Join-Path $distDir "bin"
$modelsDir = Join-Path $distDir "models"

$exePath = Join-Path $BuildDir "src\cli\corridorkey.exe"
$coreDll = Join-Path $BuildDir "src\corridorkey_core.dll"

foreach ($required in @($exePath, $coreDll)) {
    if (-not (Test-Path $required)) {
        throw "Missing required build artifact: $required"
    }
}

$requiredModels = @(
    "corridorkey_fp16_768.onnx",
    "corridorkey_fp16_1024.onnx",
    "corridorkey_int8_512.onnx"
)
$optionalContextModels = @(
    "corridorkey_fp16_768_ctx.onnx",
    "corridorkey_fp16_1024_ctx.onnx"
)

foreach ($model in $requiredModels) {
    $modelPath = Join-Path $repoRoot "models\$model"
    if (-not (Test-Path $modelPath)) {
        throw "Missing required model: $modelPath"
    }
}

if (-not (Test-Path $OrtRoot)) {
    throw "Curated Windows ONNX Runtime root not found: $OrtRoot"
}

$ortBinDir = Join-Path $OrtRoot "bin"
Assert-PortableRuntime -RuntimeDir $ortBinDir

Write-Host "[1/5] Creating portable bundle layout..."
Remove-Item -Recurse -Force $distDir -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $binDir | Out-Null
New-Item -ItemType Directory -Force -Path $modelsDir | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $distDir "outputs") | Out-Null

Write-Host "[2/5] Copying binaries..."
Copy-Item $exePath $binDir -Force
Copy-Item $coreDll $binDir -Force
Get-ChildItem -Path (Join-Path $BuildDir "src\cli") -Filter "*.dll" -File |
    Copy-Item -Destination $binDir -Force
Get-ChildItem -Path $ortBinDir -Filter "*.dll" | Copy-Item -Destination $binDir -Force
Get-ChildItem -Path (Join-Path $OrtRoot "lib") -Filter "*.dll" -ErrorAction SilentlyContinue |
    Copy-Item -Destination $binDir -Force

$msvcRuntimeDir = Resolve-MsvcRuntimeDir
if ($null -ne $msvcRuntimeDir) {
    Get-ChildItem -Path $msvcRuntimeDir -Filter "*.dll" -File | Copy-Item -Destination $binDir -Force
}

$bundleExePath = Join-Path $binDir "corridorkey.exe"
Assert-PortableRuntime -RuntimeDir $binDir

Write-Host "[3/5] Copying models..."
foreach ($model in $requiredModels) {
    Copy-Item (Join-Path $repoRoot "models\$model") $modelsDir -Force
}
foreach ($model in $optionalContextModels) {
    $sourcePath = Join-Path $repoRoot "models\$model"
    if (Test-Path $sourcePath) {
        Copy-Item $sourcePath $modelsDir -Force
    }
}

if ($CompileContexts.IsPresent) {
    Write-Host "[3.5/5] Compiling TensorRT RTX context models..."
    foreach ($model in @("corridorkey_fp16_768.onnx", "corridorkey_fp16_1024.onnx")) {
        $modelPath = Join-Path $modelsDir $model
        $outputPath = Join-Path $modelsDir (([System.IO.Path]::GetFileNameWithoutExtension($model)) + "_ctx.onnx")
        & $bundleExePath compile-context --model $modelPath --device tensorrt --output $outputPath
        if ($LASTEXITCODE -ne 0) {
            throw "Failed to compile context model for $model"
        }
    }
}

Write-Host "[4/5] Writing bundle helpers..."
@"
@echo off
setlocal
set SCRIPT_DIR=%~dp0
pushd "%SCRIPT_DIR%" >nul
"%SCRIPT_DIR%bin\corridorkey.exe" %*
set EXIT_CODE=%ERRORLEVEL%
popd >nul
exit /b %EXIT_CODE%
"@ | Set-Content -Path (Join-Path $distDir "corridorkey.cmd") -Encoding ASCII

@"
CorridorKey Runtime v$Version - Windows RTX Portable Release
==================================================

This is a standalone Windows RTX release of CorridorKey.

The bundle is expected to carry its own ONNX Runtime, TensorRT RTX, and CUDA runtime DLLs.
The target machine still needs a compatible NVIDIA driver.

QUICK START:
1. Open PowerShell or Command Prompt.
2. Navigate to this folder.
3. Check the bundle:
   .\corridorkey.cmd doctor
4. Run the smoke test:
   .\smoke_test.bat
5. Process a regular input:
   .\corridorkey.cmd process input.mp4 output.mp4
6. Force CPU fallback when needed:
   .\corridorkey.cmd process input.mp4 output.mp4 --preset preview
"@ | Set-Content -Path (Join-Path $distDir "README.txt") -Encoding ASCII

@"
@echo off
setlocal
set SCRIPT_DIR=%~dp0
pushd "%SCRIPT_DIR%" >nul
call "%SCRIPT_DIR%corridorkey.cmd" info
call "%SCRIPT_DIR%corridorkey.cmd" doctor --json >nul
call "%SCRIPT_DIR%corridorkey.cmd" models --json >nul
call "%SCRIPT_DIR%corridorkey.cmd" presets --json >nul
set EXIT_CODE=%ERRORLEVEL%
popd >nul
exit /b %EXIT_CODE%
"@ | Set-Content -Path (Join-Path $distDir "smoke_test.bat") -Encoding ASCII

Write-Host "[4.5/5] Verifying that the bundle is free from hardcoded machine paths..."
$defaultForbiddenPaths = @(
    $repoRoot,
    $BuildDir,
    $OrtRoot,
    $env:USERPROFILE,
    $env:LOCALAPPDATA
) | Where-Object { -not [string]::IsNullOrWhiteSpace($_) }
Assert-NoHardcodedPaths -RootDir $distDir -Markers ($defaultForbiddenPaths + $ForbiddenPathPrefix)

Write-Host "[5/5] Creating zip archive..."
$zipPath = Join-Path $repoRoot "dist\CorridorKey_Windows_RTX_v$Version.zip"
Remove-Item $zipPath -Force -ErrorAction SilentlyContinue
$tar = Get-Command tar.exe -ErrorAction SilentlyContinue
if ($null -ne $tar) {
    Push-Location (Split-Path -Parent $distDir)
    try {
        & $tar.Source -a -cf $zipPath (Split-Path -Leaf $distDir)
        if ($LASTEXITCODE -ne 0) {
            throw "tar.exe failed to create $zipPath"
        }
    } finally {
        Pop-Location
    }
} else {
    Compress-Archive -Path $distDir -DestinationPath $zipPath
}

Write-Host "Portable Windows RTX bundle created at $zipPath"
