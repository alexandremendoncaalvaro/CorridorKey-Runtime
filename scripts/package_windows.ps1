param(
    [string]$Version = "",
    [string]$BuildDir = "",
    [string]$OrtRoot = "",
    [switch]$CompileContexts,
    [string[]]$ForbiddenPathPrefix = @()
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot

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
    $BuildDir = Join-Path $repoRoot "build\release"
}
if ([string]::IsNullOrWhiteSpace($OrtRoot)) {
    $OrtRoot = Join-Path $repoRoot "vendor\onnxruntime-windows-rtx"
}

$releaseBasename = "CorridorKey_Runtime_v${Version}_Windows"
$distDir = Join-Path $repoRoot ("dist\" + $releaseBasename)
$zipPath = Join-Path $repoRoot ("dist\" + $releaseBasename + ".zip")
$engineSource = Join-Path $BuildDir "src\cli\corridorkey.exe"
$guiSource = Join-Path $repoRoot "src\gui\src-tauri\target\release\corridorkey-gui.exe"
$modelsSource = Join-Path $repoRoot "models"
$bundleModelsDir = Join-Path $distDir "models"
$bundleOutputsDir = Join-Path $distDir "outputs"

function Assert-FileExists {
    param([string]$Path, [string]$Message)
    if (-not (Test-Path $Path)) {
        throw $Message
    }
}

function Copy-DllsFromDir {
    param([string]$SourceDir, [string]$DestinationDir)
    if (Test-Path $SourceDir) {
        Copy-Item (Join-Path $SourceDir "*.dll") $DestinationDir -Force
    }
}

function Assert-NoForbiddenPathPrefix {
    param([string[]]$Prefixes, [string]$BundleRoot)

    $normalized = @(
        $Prefixes | Where-Object { -not [string]::IsNullOrWhiteSpace($_) } |
            ForEach-Object { [System.IO.Path]::GetFullPath($_) } | Select-Object -Unique
    )
    if ($normalized.Count -eq 0) {
        return
    }

    $items = Get-ChildItem -Path $BundleRoot -Recurse -Force
    foreach ($item in $items) {
        if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) {
            $target = (Get-Item $item.FullName -Force).Target
            if (-not $target) {
                continue
            }
            $targetPath = [System.IO.Path]::GetFullPath($target)
            foreach ($prefix in $normalized) {
                if ($targetPath.StartsWith($prefix, [System.StringComparison]::OrdinalIgnoreCase)) {
                    throw "Bundle contains a link to forbidden path: $targetPath"
                }
            }
        }
    }
}

Write-Host "[1/6] Cleaning and creating bundle layout..."
if (Test-Path $distDir) {
    Remove-Item $distDir -Recurse -Force
}
if (Test-Path $zipPath) {
    Remove-Item $zipPath -Force
}
New-Item -ItemType Directory -Path $distDir -Force | Out-Null
New-Item -ItemType Directory -Path $bundleModelsDir -Force | Out-Null
New-Item -ItemType Directory -Path $bundleOutputsDir -Force | Out-Null

Write-Host "[2/6] Copying ck-engine.exe..."
Assert-FileExists -Path $engineSource -Message "ck-engine source not found at $engineSource"
Copy-Item $engineSource (Join-Path $distDir "ck-engine.exe") -Force

Write-Host "[3/6] Copying runtime DLLs..."
Copy-DllsFromDir -SourceDir (Join-Path $BuildDir "src\cli") -DestinationDir $distDir
Copy-DllsFromDir -SourceDir (Join-Path $OrtRoot "lib") -DestinationDir $distDir
Copy-DllsFromDir -SourceDir (Join-Path $OrtRoot "bin") -DestinationDir $distDir

Write-Host "[4/6] Copying GUI..."
Assert-FileExists -Path $guiSource -Message "GUI binary not found at $guiSource"
Copy-Item $guiSource (Join-Path $distDir "CorridorKey_Runtime.exe") -Force

Write-Host "[5/6] Copying models..."
$targetModels = @(
    "corridorkey_fp16_768.onnx",
    "corridorkey_fp16_1024.onnx",
    "corridorkey_int8_512.onnx"
)
foreach ($model in $targetModels) {
    $sourcePath = Join-Path $modelsSource $model
    Assert-FileExists -Path $sourcePath -Message "Missing model: $sourcePath"
    Copy-Item $sourcePath $bundleModelsDir -Force
}

if ($CompileContexts.IsPresent) {
    Write-Host "[5.5/6] Compiling TensorRT contexts..."
    foreach ($model in $targetModels) {
        $modelPath = Join-Path $bundleModelsDir $model
        & (Join-Path $distDir "ck-engine.exe") compile-context --model $modelPath --device tensorrt
        if ($LASTEXITCODE -ne 0) {
            throw "Failed to compile TensorRT context for $model"
        }
    }
}

Write-Host "[6/6] Writing README and smoke test..."
$readmePath = Join-Path $distDir "README.txt"
$smokePath = Join-Path $distDir "smoke_test.bat"

@"
CorridorKey Runtime v$Version - Windows Portable Release
=======================================================

Quick start:
1. Open PowerShell or Command Prompt in this folder.
2. Run: .\ck-engine.exe doctor
3. Run: .\smoke_test.bat
4. Process a video:
   .\ck-engine.exe process input.mp4 output.mov

Notes:
- Lossless output is the default. Use --video-encode balanced for lossy output.
- The models folder must remain next to ck-engine.exe.
"@ | Set-Content -Path $readmePath -Encoding ASCII

@'
@echo off
setlocal
cd /d "%~dp0"

ck-engine.exe info
if errorlevel 1 exit /b 1

ck-engine.exe doctor --json > doctor_report.json
if errorlevel 1 exit /b 1

powershell -NoProfile -Command "$report = Get-Content -Raw '.\\doctor_report.json' | ConvertFrom-Json; if (-not $report.summary.video_healthy) { Write-Error 'Video output is not healthy.'; exit 1 }"
if errorlevel 1 exit /b 1

exit /b 0
'@ | Set-Content -Path $smokePath -Encoding ASCII

Assert-NoForbiddenPathPrefix -Prefixes $ForbiddenPathPrefix -BundleRoot $distDir

Write-Host "[6.5/6] Creating ZIP archive..."
Compress-Archive -Path $distDir -DestinationPath $zipPath -CompressionLevel Optimal

Write-Host "Bundle ready at: $distDir"
Write-Host "Archive ready at: $zipPath"
