param(
    [string]$Version = "",
    [string]$OutputDir = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot "windows_runtime_helpers.ps1")

function Resolve-OrtVersion {
    param([string]$RepoRoot)

    if (-not [string]::IsNullOrWhiteSpace($Version)) {
        return $Version
    }

    $candidate = Join-Path $RepoRoot "vendor\onnxruntime-windows-rtx\onnxruntime.dll"
    if (-not (Test-Path $candidate)) {
        throw "Unable to infer ONNX Runtime version from vendor\onnxruntime-windows-rtx\onnxruntime.dll. Pass -Version explicitly or stage the curated RTX runtime first."
    }

    return (Get-Item $candidate).VersionInfo.ProductVersion
}

function Download-Package {
    param([string]$Url, [string]$Path)

    Write-Host "Downloading $Url" -ForegroundColor Cyan
    curl.exe -L $Url -o $Path
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to download package from $Url"
    }
}

function Extract-Package {
    param([string]$ArchivePath, [string]$Destination)

    if (Test-Path $Destination) {
        Remove-Item $Destination -Recurse -Force
    }
    New-Item -ItemType Directory -Path $Destination -Force | Out-Null
    tar.exe -xf $ArchivePath -C $Destination
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to extract package: $ArchivePath"
    }
}

$resolvedVersion = Resolve-OrtVersion -RepoRoot $repoRoot
if ([string]::IsNullOrWhiteSpace($OutputDir)) {
    $OutputDir = Get-CorridorKeyWindowsOrtRootPath -RepoRoot $repoRoot -Track "dml"
}

$tempRoot = Join-Path $env:TEMP ("corridorkey-ort-dml-sync-" + [System.Guid]::NewGuid().ToString("N"))
$ortPackage = Join-Path $tempRoot "onnxruntime-directml.nupkg"
$directmlPackage = Join-Path $tempRoot "directml.nupkg"
$ortExtractDir = Join-Path $tempRoot "onnxruntime-directml"
$directmlExtractDir = Join-Path $tempRoot "directml"

New-Item -ItemType Directory -Path $tempRoot -Force | Out-Null

try {
    Download-Package -Url "https://www.nuget.org/api/v2/package/Microsoft.ML.OnnxRuntime.DirectML/$resolvedVersion" -Path $ortPackage
    Download-Package -Url "https://www.nuget.org/api/v2/package/Microsoft.AI.DirectML/1.15.4" -Path $directmlPackage

    Extract-Package -ArchivePath $ortPackage -Destination $ortExtractDir
    Extract-Package -ArchivePath $directmlPackage -Destination $directmlExtractDir

    if (Test-Path $OutputDir) {
        Remove-Item $OutputDir -Recurse -Force
    }

    $includeDir = Join-Path $OutputDir "include"
    $binDir = Join-Path $OutputDir "bin"
    $libDir = Join-Path $OutputDir "lib"
    New-Item -ItemType Directory -Path $includeDir -Force | Out-Null
    New-Item -ItemType Directory -Path $binDir -Force | Out-Null
    New-Item -ItemType Directory -Path $libDir -Force | Out-Null

    Copy-Item (Join-Path $ortExtractDir "build\native\include\*") $includeDir -Recurse -Force
    Copy-Item (Join-Path $ortExtractDir "runtimes\win-x64\native\onnxruntime.dll") $binDir -Force
    Copy-Item (Join-Path $ortExtractDir "runtimes\win-x64\native\onnxruntime_providers_shared.dll") $binDir -Force
    Copy-Item (Join-Path $ortExtractDir "runtimes\win-x64\native\onnxruntime.lib") $libDir -Force
    Copy-Item (Join-Path $directmlExtractDir "bin\x64-win\DirectML.dll") $binDir -Force

    Copy-Item (Join-Path $binDir "onnxruntime.dll") $OutputDir -Force
    Copy-Item (Join-Path $binDir "onnxruntime_providers_shared.dll") $OutputDir -Force
    Copy-Item (Join-Path $binDir "DirectML.dll") $OutputDir -Force
    Copy-Item (Join-Path $libDir "onnxruntime.lib") $OutputDir -Force

    Copy-Item (Join-Path $ortExtractDir "LICENSE") (Join-Path $OutputDir "LICENSE.onnxruntime.txt") -Force
    Copy-Item (Join-Path $directmlExtractDir "LICENSE.txt") (Join-Path $OutputDir "LICENSE.directml.txt") -Force
    Copy-Item (Join-Path $ortExtractDir "README.md") (Join-Path $OutputDir "README.onnxruntime-directml.md") -Force

    Write-Host "Official DirectML runtime staged at: $OutputDir" -ForegroundColor Green
    Write-Host "ONNX Runtime version: $resolvedVersion" -ForegroundColor Green
} finally {
    if (Test-Path $tempRoot) {
        Remove-Item $tempRoot -Recurse -Force
    }
}
