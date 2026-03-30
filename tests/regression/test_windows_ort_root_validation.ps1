Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
. (Join-Path $repoRoot "scripts\windows_runtime_helpers.ps1")

$tempRoot = Join-Path $env:TEMP ("corridorkey-ort-root-test-" + [guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $tempRoot -Force | Out-Null

try {
    $ortRoot = Join-Path $tempRoot "onnxruntime-windows-rtx"
    New-Item -ItemType Directory -Path (Join-Path $ortRoot "bin") -Force | Out-Null
    New-Item -ItemType Directory -Path (Join-Path $ortRoot "lib") -Force | Out-Null
    New-Item -ItemType Directory -Path (Join-Path $ortRoot "include") -Force | Out-Null

    Set-Content -Path (Join-Path $ortRoot "bin\onnxruntime.dll") -Value "" -Encoding Ascii
    Set-Content -Path (Join-Path $ortRoot "lib\onnxruntime.lib") -Value "" -Encoding Ascii
    Set-Content -Path (Join-Path $ortRoot "include\onnxruntime_c_api.h") -Value "" -Encoding Ascii

    if (-not (Test-CorridorKeyWindowsOrtRoot -OrtRoot $ortRoot)) {
        throw "Expected Test-CorridorKeyWindowsOrtRoot to accept the curated include/bin/lib layout."
    }

    Remove-Item -Path (Join-Path $ortRoot "include\onnxruntime_c_api.h") -Force
    New-Item -ItemType Directory -Path (Join-Path $ortRoot "include\onnxruntime") -Force | Out-Null
    Set-Content -Path (Join-Path $ortRoot "include\onnxruntime\onnxruntime_c_api.h") -Value "" -Encoding Ascii

    if (-not (Test-CorridorKeyWindowsOrtRoot -OrtRoot $ortRoot)) {
        throw "Expected Test-CorridorKeyWindowsOrtRoot to accept the nested include\\onnxruntime layout."
    }

    Remove-Item -Path (Join-Path $ortRoot "bin\onnxruntime.dll") -Force
    if (Test-CorridorKeyWindowsOrtRoot -OrtRoot $ortRoot) {
        throw "Expected Test-CorridorKeyWindowsOrtRoot to reject a root without runtime DLLs."
    }

    Write-Host "[PASS] Windows ORT root validation regression checks passed." -ForegroundColor Green
} finally {
    if (Test-Path $tempRoot) {
        Remove-Item -Path $tempRoot -Recurse -Force
    }
}
