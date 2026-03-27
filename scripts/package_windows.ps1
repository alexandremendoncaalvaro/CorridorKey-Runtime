param(
    [string]$Version = "",
    [string]$BuildDir = "",
    [string]$OrtRoot = "",
    [string]$ReleaseSuffix = "",
    [switch]$CompileContexts,
    [string[]]$ForbiddenPathPrefix = @()
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot "windows_runtime_helpers.ps1")

$Version = Initialize-CorridorKeyVersion -RepoRoot $repoRoot -Version $Version
if ([string]::IsNullOrWhiteSpace($BuildDir)) {
    $BuildDir = Join-Path $repoRoot "build\release"
}
$preferredTrack = Get-CorridorKeyWindowsTrackFromReleaseSuffix -ReleaseSuffix $ReleaseSuffix -DefaultTrack "rtx"
$OrtRoot = Resolve-CorridorKeyWindowsOrtRoot -RepoRoot $repoRoot -ExplicitRoot $OrtRoot -PreferredTrack $preferredTrack

$normalizedSuffix = ""
if (-not [string]::IsNullOrWhiteSpace($ReleaseSuffix)) {
    $normalizedSuffix = "_" + $ReleaseSuffix.Trim("_")
}
$releaseBasename = "CorridorKey_Runtime_v${Version}_Windows${normalizedSuffix}"
$distDir = Join-Path $repoRoot ("dist\" + $releaseBasename)
$zipPath = Join-Path $repoRoot ("dist\" + $releaseBasename + ".zip")
$engineSource = Join-Path $BuildDir "src\cli\corridorkey.exe"
$guiSource = Join-Path $repoRoot "src\gui\src-tauri\target\release\corridorkey-gui.exe"
$modelsSource = Join-Path $repoRoot "models"
$bundleModelsDir = Join-Path $distDir "models"
$bundleOutputsDir = Join-Path $distDir "outputs"
$modelInventoryPath = Join-Path $distDir "model_inventory.json"

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
$targetModels = Get-CorridorKeyPortableRuntimeTargetModels
$modelInventory = Get-CorridorKeyModelInventory -ModelsDir $modelsSource -ExpectedModels $targetModels
foreach ($model in $modelInventory.present_models) {
    $sourcePath = Join-Path $modelsSource $model
    Copy-Item $sourcePath $bundleModelsDir -Force
}

$inventoryPayload = [ordered]@{
    package_type = "runtime_bundle"
    models_dir = [System.IO.Path]::GetFullPath($modelsSource)
    expected_models = @($modelInventory.expected_models)
    present_models = @($modelInventory.present_models)
    missing_models = @($modelInventory.missing_models)
    present_count = $modelInventory.present_count
    missing_count = $modelInventory.missing_count
}
Write-CorridorKeyJsonFile -Path $modelInventoryPath -Payload $inventoryPayload

if ($modelInventory.missing_count -gt 0) {
    Write-Host "[WARN] Packaging runtime bundle without model(s): $($modelInventory.missing_models -join ', ')" -ForegroundColor Yellow
    Write-Host "[INFO] Wrote model inventory: $modelInventoryPath" -ForegroundColor Cyan
} else {
    Write-Host "[PASS] All targeted runtime models were packaged." -ForegroundColor Green
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
- model_inventory.json records any packaged models that are absent from this bundle.
"@ | Set-Content -Path $readmePath -Encoding ASCII

@'
@echo off
setlocal
cd /d "%~dp0"

ck-engine.exe info
if errorlevel 1 exit /b 1

ck-engine.exe doctor --json > doctor_report.json
if errorlevel 1 exit /b 1

powershell -NoProfile -Command "$report = Get-Content -Raw '.\\doctor_report.json' | ConvertFrom-Json; $inventory = if (Test-Path '.\\model_inventory.json') { Get-Content -Raw '.\\model_inventory.json' | ConvertFrom-Json } else { $null }; if ($null -ne $inventory -and $inventory.missing_count -gt 0) { Write-Warning ('Portable runtime bundle is missing model(s): ' + ($inventory.missing_models -join ', ')); exit 0 }; if (-not $report.summary.video_healthy) { Write-Error 'Video output is not healthy.'; exit 1 }"
if errorlevel 1 exit /b 1

exit /b 0
'@ | Set-Content -Path $smokePath -Encoding ASCII

Assert-NoForbiddenPathPrefix -Prefixes $ForbiddenPathPrefix -BundleRoot $distDir

Write-Host "[6.5/6] Creating ZIP archive..."
Compress-Archive -Path $distDir -DestinationPath $zipPath -CompressionLevel Optimal

Write-Host "Bundle ready at: $distDir"
Write-Host "Archive ready at: $zipPath"
