param(
    [string]$VendorRoot = "",
    [string]$SmokeTsPath = "",
    [int]$SmokeResolution = 512,
    [string]$OutputReport = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot "windows_runtime_helpers.ps1")

# Sister to certify_windows_rtx_artifacts.ps1, but scoped to the
# blue-pack TorchTRT runtime payload. Verifies:
#   1. vendor/torchtrt-windows/{bin,lib,include} layout is present
#   2. every DLL in Get-CorridorKeyWindowsTorchTrtBuildContract is on disk
#   3. (optional) Python smoke load: torch.jit.load on a known good
#      blue .ts using ONLY DLLs in vendor/torchtrt-windows/bin via
#      os.add_dll_directory - this is the same delayed-load path PR 2
#      will exercise from C++ via AddDllDirectory + LoadLibrary
#
# The smoke step is skipped when no -SmokeTsPath is provided and there
# is no Sprint 0 artifact to fall back to. This keeps the certify task
# usable in a fresh clone (where the user has not yet compiled engines).

if ([string]::IsNullOrWhiteSpace($VendorRoot)) {
    $VendorRoot = Join-Path $repoRoot "vendor\torchtrt-windows"
}
if ([string]::IsNullOrWhiteSpace($SmokeTsPath)) {
    $candidate = Join-Path $repoRoot ("temp\blue-diagnose\green-torchtrt-local-windows\corridorkey_torchtrt_fp16_" + $SmokeResolution + ".ts")
    if (Test-Path $candidate) {
        $SmokeTsPath = $candidate
    }
}
if ([string]::IsNullOrWhiteSpace($OutputReport)) {
    $OutputReport = Join-Path $VendorRoot "windows_torchtrt_validation_report.json"
}

$contract = Get-CorridorKeyWindowsTorchTrtBuildContract
$report = [ordered]@{
    vendor_root = [System.IO.Path]::GetFullPath($VendorRoot)
    contract = [ordered]@{
        torch_version = $contract.torch_version
        torch_tensorrt_version = $contract.torch_tensorrt_version
        tensorrt_cu12_version = $contract.tensorrt_cu12_version
    }
    layout_checks = @()
    dll_checks = @()
    smoke = $null
    verdict = "unknown"
}

function Add-LayoutCheck {
    param([string]$Name, [string]$Path, [bool]$Required = $true)
    $exists = Test-Path $Path
    $report.layout_checks += [ordered]@{
        name = $Name
        path = $Path
        exists = $exists
        required = $Required
    }
    return $exists
}

function Add-DllCheck {
    param([string]$Name, [string]$Path)
    $exists = Test-Path $Path
    $entry = [ordered]@{
        name = $Name
        path = $Path
        exists = $exists
    }
    if ($exists) {
        $info = Get-Item -Path $Path
        $entry["size_bytes"] = $info.Length
    }
    $report.dll_checks += $entry
    return $exists
}

# ---- layout checks ----

$binDir = Join-Path $VendorRoot "bin"
$libDir = Join-Path $VendorRoot "lib"
$includeDir = Join-Path $VendorRoot "include"
$manifestPath = Join-Path $VendorRoot "torchtrt_manifest.json"

$layoutOk = $true
foreach ($probe in @(
    @{ Name = "vendor_root"; Path = $VendorRoot },
    @{ Name = "bin"; Path = $binDir },
    @{ Name = "lib"; Path = $libDir },
    @{ Name = "include"; Path = $includeDir },
    @{ Name = "manifest"; Path = $manifestPath }
)) {
    $ok = Add-LayoutCheck -Name $probe.Name -Path $probe.Path -Required:$true
    if (-not $ok) { $layoutOk = $false }
}

# ---- DLL checks ----

$expectedDlls = @() + $contract.torch_runtime_dlls + $contract.torch_tensorrt_runtime_dlls + $contract.tensorrt_runtime_dlls
$dllsOk = $true
foreach ($dll in $expectedDlls) {
    $ok = Add-DllCheck -Name $dll -Path (Join-Path $binDir $dll)
    if (-not $ok) { $dllsOk = $false }
}

# ---- optional smoke load ----

if (-not [string]::IsNullOrWhiteSpace($SmokeTsPath) -and (Test-Path $SmokeTsPath)) {
    # Smoke load needs a Python with torch==2.8.0+cu128 + torch_tensorrt==2.8.0
    # already importable. The host Python typically has neither (or has a
    # different torch version that would shadow our vendored DLLs anyway).
    # Prefer the Sprint 0 validate venv if present; otherwise mark the smoke
    # as skipped rather than failing certify on environment mismatch -
    # PR 2's C++ integration test is the load-bearing runtime check.
    $pythonExe = ""
    $sprint0Venv = Join-Path $repoRoot "temp\blue-diagnose\.venv-validate\Scripts\python.exe"
    if (Test-Path $sprint0Venv) {
        $pythonExe = $sprint0Venv
    } else {
        foreach ($candidate in @("python.exe", "python", "py.exe", "py")) {
            $cmd = Get-Command $candidate -ErrorAction SilentlyContinue
            if ($null -ne $cmd) {
                $pythonExe = $cmd.Source
                break
            }
        }
    }
    if ([string]::IsNullOrWhiteSpace($pythonExe)) {
        Write-Host "[certify-torchtrt] python not on PATH; skipping smoke load" -ForegroundColor Yellow
        $report["smoke"] = [ordered]@{
            attempted = $false
            reason = "python not on PATH"
        }
    } else {
        Write-Host "[certify-torchtrt] Smoke load via $pythonExe ..." -ForegroundColor Cyan
        $smokeScript = @"
import json
import os
import sys
import time

bin_dir = r'$binDir'
ts_path = r'$SmokeTsPath'
resolution = $SmokeResolution

if hasattr(os, 'add_dll_directory'):
    os.add_dll_directory(bin_dir)
else:
    os.environ['PATH'] = bin_dir + os.pathsep + os.environ.get('PATH', '')

result = {'bin_dir': bin_dir, 'ts_path': ts_path}
try:
    import torch
    result['torch_version'] = torch.__version__
    import torch_tensorrt as tt  # noqa: F401
    result['torch_tensorrt_version'] = tt.__version__
    if not torch.cuda.is_available():
        result['verdict'] = 'no-cuda'
        print(json.dumps(result))
        sys.exit(0)
    t0 = time.perf_counter()
    loaded = torch.jit.load(ts_path, map_location='cuda').eval()
    result['load_ms'] = round((time.perf_counter() - t0) * 1000.0, 1)
    import numpy as np
    rng = np.random.default_rng(42 + resolution)
    x = torch.from_numpy(rng.random((1, 4, resolution, resolution), dtype=np.float32)).to(dtype=torch.float16, device='cuda')
    with torch.no_grad():
        out = loaded(x)
    if isinstance(out, (tuple, list)):
        alpha = out[0]
    else:
        alpha = out
    a = alpha.detach().float().cpu().numpy()
    result['alpha_min'] = float(a.min())
    result['alpha_max'] = float(a.max())
    result['has_nan'] = bool(a.size and (a != a).any())
    result['verdict'] = 'compatible'
except Exception as exc:
    result['verdict'] = 'fail'
    result['error'] = f'{type(exc).__name__}: {str(exc)[:400]}'
print(json.dumps(result))
"@
        $smokeScriptPath = Join-Path $env:TEMP ("certify_torchtrt_smoke_" + [System.Guid]::NewGuid().ToString("N") + ".py")
        $smokeStdoutPath = Join-Path $env:TEMP ("certify_torchtrt_smoke_" + [System.Guid]::NewGuid().ToString("N") + ".stdout.txt")
        $smokeStderrPath = Join-Path $env:TEMP ("certify_torchtrt_smoke_" + [System.Guid]::NewGuid().ToString("N") + ".stderr.txt")
        Set-Content -Path $smokeScriptPath -Value $smokeScript -Encoding UTF8
        try {
            # PowerShell 5.1 wraps native stderr lines in
            # NativeCommandError objects under StrictMode + Stop,
            # whether you use 2>&1 OR `2> file` (in-process redirection
            # is still observed). Use Start-Process with file-descriptor
            # redirection so stderr never enters the PS pipeline.
            $proc = Start-Process -FilePath $pythonExe `
                -ArgumentList @($smokeScriptPath) `
                -NoNewWindow -Wait -PassThru `
                -RedirectStandardOutput $smokeStdoutPath `
                -RedirectStandardError $smokeStderrPath
            $smokeStdout = if (Test-Path $smokeStdoutPath) { Get-Content -Path $smokeStdoutPath } else { @() }
            $smokeStderrTail = if (Test-Path $smokeStderrPath) { Get-Content -Path $smokeStderrPath -Tail 4 } else { @() }
            $smokeJson = $smokeStdout | Where-Object { $_ -match "^\{" } | Select-Object -Last 1
            if (-not [string]::IsNullOrWhiteSpace($smokeJson)) {
                $report["smoke"] = $smokeJson | ConvertFrom-Json | ConvertTo-Json -Depth 6 | ConvertFrom-Json
            } else {
                $report["smoke"] = [ordered]@{
                    attempted = $true
                    verdict = "fail"
                    error = "Smoke script produced no JSON output."
                    stderr_tail = ($smokeStderrTail -join "`n")
                }
            }
        } finally {
            Remove-Item $smokeScriptPath -Force -ErrorAction SilentlyContinue
            Remove-Item $smokeStdoutPath -Force -ErrorAction SilentlyContinue
            Remove-Item $smokeStderrPath -Force -ErrorAction SilentlyContinue
        }
    }
} else {
    $report["smoke"] = [ordered]@{
        attempted = $false
        reason = "no -SmokeTsPath provided and no Sprint 0 artifact found at temp/blue-diagnose/..."
    }
}

# ---- verdict ----

if (-not $layoutOk) {
    $report["verdict"] = "fail-layout"
} elseif (-not $dllsOk) {
    $report["verdict"] = "fail-dll-set"
} else {
    $smokeVerdict = if ($null -eq $report["smoke"]) { "skipped" } else {
        if ($report["smoke"] -is [System.Collections.IDictionary]) {
            if ($report["smoke"].ContainsKey("verdict")) { $report["smoke"]["verdict"] } else { "unknown" }
        } else {
            try { $report["smoke"].verdict } catch { "unknown" }
        }
    }
    if ($smokeVerdict -eq "compatible") {
        $report["verdict"] = "pass-with-smoke"
    } elseif ($smokeVerdict -eq "fail") {
        # Smoke failures are reported but not gated. Certifying the bundle
        # layout is the contract this script owns; the load-bearing
        # runtime smoke is exercised by PR 2's C++ integration test, which
        # uses AddDllDirectory + LoadLibrary the same way the production
        # blue-pack will.
        $report["verdict"] = "pass-layout-smoke-soft-fail"
    } else {
        $report["verdict"] = "pass-layout-only"
    }
}

# ---- write report ----

$outputDir = Split-Path -Parent $OutputReport
if (-not (Test-Path $outputDir)) {
    New-Item -ItemType Directory -Path $outputDir -Force | Out-Null
}
$report | ConvertTo-Json -Depth 8 | Set-Content -Path $OutputReport -Encoding UTF8

Write-Host ("[certify-torchtrt] Report: {0}" -f $OutputReport) -ForegroundColor Green
Write-Host ("[certify-torchtrt] Verdict: {0}" -f $report["verdict"]) -ForegroundColor Cyan

if ($report["verdict"] -in @("fail-layout", "fail-dll-set")) {
    throw ("Certification failed: " + $report["verdict"])
}
