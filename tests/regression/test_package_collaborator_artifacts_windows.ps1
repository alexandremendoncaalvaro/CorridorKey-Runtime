Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$sourcePackageScript = Join-Path $repoRoot "scripts\package_collaborator_artifacts_windows.ps1"
$sourceHelpersScript = Join-Path $repoRoot "scripts\windows_runtime_helpers.ps1"

$tempRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("ck-collaborator-artifacts-" + [System.Guid]::NewGuid().ToString("N"))
$tempRepo = Join-Path $tempRoot "repo"

function Write-Utf8File {
    param(
        [string]$Path,
        [string]$Content
    )

    $parent = Split-Path -Parent $Path
    if (-not [string]::IsNullOrWhiteSpace($parent) -and -not (Test-Path $parent)) {
        New-Item -ItemType Directory -Path $parent -Force | Out-Null
    }

    $utf8NoBom = [System.Text.UTF8Encoding]::new($false)
    [System.IO.File]::WriteAllText($Path, ($Content -replace "`r`n", "`n"), $utf8NoBom)
}

try {
    New-Item -ItemType Directory -Path (Join-Path $tempRepo "scripts") -Force | Out-Null
    New-Item -ItemType Directory -Path (Join-Path $tempRepo "models") -Force | Out-Null
    New-Item -ItemType Directory -Path (Join-Path $tempRepo "dist") -Force | Out-Null

    Copy-Item -Path $sourcePackageScript -Destination (Join-Path $tempRepo "scripts\package_collaborator_artifacts_windows.ps1")
    Copy-Item -Path $sourceHelpersScript -Destination (Join-Path $tempRepo "scripts\windows_runtime_helpers.ps1")

    Write-Utf8File -Path (Join-Path $tempRepo "CMakeLists.txt") -Content @"
project(CorridorKey-Runtime
    VERSION 0.5.4
)
"@

    Write-Utf8File -Path (Join-Path $tempRepo "models\corridorkey_fp16_2048.onnx") -Content "fake-2048-model"
    Write-Utf8File -Path (Join-Path $tempRepo "models\corridorkey_fp16_2048_ctx.onnx") -Content "fake-2048-context"
    Write-Utf8File -Path (Join-Path $tempRepo "dist\CorridorKey_Resolve_v0.6.0_Windows_RTX_Full_Installer.exe") -Content "fake-installer"
    Write-Utf8File -Path (Join-Path $tempRepo "dist\CorridorKey_Resolve_v0.6.0_Windows_RTX_Full\bundle_validation.json") -Content "{`"healthy`":true}"
    Write-Utf8File -Path (Join-Path $tempRepo "dist\CorridorKey_Resolve_v0.6.0_Windows_RTX_Full\doctor_report.json") -Content "{`"healthy`":true}"
    Write-Utf8File -Path (Join-Path $tempRepo "dist\CorridorKey_Resolve_v0.6.0_Windows_RTX_Full\model_inventory.json") -Content "{`"present_models`":[`"corridorkey_fp16_2048.onnx`"]}"

    $outputZip = Join-Path $tempRepo "dist\CorridorKey_Collaborator_v0.6.0_Windows_RTX.zip"
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File `
        (Join-Path $tempRepo "scripts\package_collaborator_artifacts_windows.ps1") `
        -Version "0.6.0" `
        -OutputZip $outputZip
    if ($LASTEXITCODE -ne 0) {
        throw "package_collaborator_artifacts_windows.ps1 exited with code $LASTEXITCODE"
    }

    if (-not (Test-Path $outputZip)) {
        throw "Expected collaborator artifact archive to be created."
    }

    $cmakeListsContent = Get-Content -Path (Join-Path $tempRepo "CMakeLists.txt") -Raw
    if ($cmakeListsContent -match "0\.6\.0") {
        throw "Packaging script must not mutate project version files."
    }

    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $zipArchive = [System.IO.Compression.ZipFile]::OpenRead($outputZip)
    try {
        $zipEntries = $zipArchive.Entries | Select-Object -ExpandProperty FullName
    } finally {
        $zipArchive.Dispose()
    }

    foreach ($expectedEntry in @(
            "models\corridorkey_fp16_2048.onnx",
            "models\corridorkey_fp16_2048_ctx.onnx",
            "installers\CorridorKey_Resolve_v0.6.0_Windows_RTX_Full_Installer.exe",
            "reports\RTX_Full_bundle_validation.json",
            "reports\RTX_Full_doctor_report.json",
            "reports\RTX_Full_model_inventory.json",
            "artifact_manifest.json"
        )) {
        if ($expectedEntry -notin $zipEntries) {
            throw "Expected zip entry missing: $expectedEntry"
        }
    }

    Write-Host "PASS: collaborator artifact packaging fallback archive is created without mutating version metadata."
} finally {
    if (Test-Path $tempRoot) {
        Remove-Item -Path $tempRoot -Recurse -Force
    }
}
