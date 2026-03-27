param(
    [string]$Version = "",
    [switch]$SkipTests,
    [switch]$CleanOnly
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
Set-Location $repoRoot
. (Join-Path $PSScriptRoot "windows_runtime_helpers.ps1")

function Write-Step([string]$msg) {
    Write-Host "`n=== [STEP] $msg ===" -ForegroundColor Cyan
}

function Write-Success([string]$msg) {
    Write-Host "[SUCCESS] $msg" -ForegroundColor Green
}

try {
    $Version = Initialize-CorridorKeyVersion -RepoRoot $repoRoot -Version $Version -SyncGuiMetadata

    $rtxOrtRoot = Get-CorridorKeyWindowsOrtRootPath -RepoRoot $repoRoot -Track "rtx"
    $directMlOrtRoot = Get-CorridorKeyWindowsOrtRootPath -RepoRoot $repoRoot -Track "dml"

    Write-Step "Sanitizing Environment"
    $dirsToClean = @("build/release", "dist", "temp")
    foreach ($dir in $dirsToClean) {
        if (Test-Path $dir) {
            Write-Host "Cleaning $dir..."
            Remove-Item $dir -Recurse -Force
        }
    }

    $systemLogDir = "$env:LOCALAPPDATA\CorridorKey\Logs"
    if (Test-Path $systemLogDir) {
        Write-Host "Clearing system logs at $systemLogDir..." -ForegroundColor Yellow
        Remove-Item "$systemLogDir\*" -Force -ErrorAction SilentlyContinue
    }

    if ($CleanOnly) { exit 0 }

    if (-not (Test-Path $rtxOrtRoot)) {
        throw "Curated RTX runtime not found at $rtxOrtRoot. Build it with scripts\prepare_windows_rtx_release.ps1 or scripts\build_ort_windows_rtx.ps1 first."
    }
    $rtxOrtVersion = Get-CorridorKeyWindowsOrtBinaryVersion -RepoRoot $repoRoot -Track "rtx"

    Write-Step "Synchronizing DirectML Runtimes"
    & powershell.exe -NoProfile -File "scripts/sync_onnxruntime_directml.ps1" -OrtVersion $rtxOrtVersion
    if ($LASTEXITCODE -ne 0) { throw "DirectML runtime synchronization failed." }
    Write-Success "DirectML runtimes synchronized."
    if (-not (Test-Path $directMlOrtRoot)) {
        throw "DirectML runtime not found at $directMlOrtRoot after synchronization."
    }

    Write-Step "Building Project (Release Mode)"
    $vcvars = Get-ChildItem -Path "C:\Program Files\Microsoft Visual Studio" -Filter vcvars64.bat -Recurse | Select-Object -First 1 -ExpandProperty FullName
    if (-not $vcvars) { throw "vcvars64.bat not found. MSVC environment required." }

    & cmd /c "call `"$vcvars`" && set `"CORRIDORKEY_WINDOWS_ORT_ROOT=$rtxOrtRoot`" && cmake --preset release -DCORRIDORKEY_WINDOWS_ORT_ROOT=`"$rtxOrtRoot`" && cmake --build --preset release -j 8"
    if ($LASTEXITCODE -ne 0) { throw "Build failed." }
    Write-Success "Build completed successfully."

    if (-not $SkipTests) {
        Write-Step "Quality Gate: Running Automated Tests"
        & cmd /c "call `"$vcvars`" && set `"CORRIDORKEY_WINDOWS_ORT_ROOT=$rtxOrtRoot`" && cmake --build --preset release --target test_unit test_regression && cd build/release && ctest --output-on-failure"
        if ($LASTEXITCODE -ne 0) { throw "Tests failed." }
        Write-Success "All tests passed."
    }

    Write-Step "Quality Gate: Packaging and Backend Validation"

    $variants = @(
        @{ Suffix = "DirectML"; Root = $directMlOrtRoot },
        @{ Suffix = "RTX";      Root = $rtxOrtRoot }
    )

    foreach ($v in $variants) {
        Write-Host "--- Packaging Variant: $($v.Suffix) ---" -ForegroundColor Yellow
        & powershell.exe -NoProfile -File "scripts/package_ofx_installer_windows.ps1" `
            -Version $Version `
            -ReleaseSuffix $v.Suffix `
            -OrtRoot $v.Root

        if ($LASTEXITCODE -ne 0) { throw "Packaging failed for variant: $($v.Suffix)" }

        $expectedInstaller = Join-Path $repoRoot "dist/CorridorKey_Resolve_v${Version}_Windows_$($v.Suffix)_Installer.exe"
        if (-not (Test-Path $expectedInstaller)) {
            throw "CRITICAL: Pipeline claimed success but installer was NOT found at: $expectedInstaller"
        }
        $expectedValidationReport = Join-Path $repoRoot "dist/CorridorKey_Resolve_v${Version}_Windows_$($v.Suffix)\bundle_validation.json"
        if (-not (Test-Path $expectedValidationReport)) {
            throw "CRITICAL: Bundle validation did not produce a validation report at: $expectedValidationReport"
        }
        $validation = Get-Content -Path $expectedValidationReport -Raw | ConvertFrom-Json
        Write-Host "[VERIFIED] Artifact created: $expectedInstaller" -ForegroundColor Green
        Write-Host "[VERIFIED] Bundle validation report created: $expectedValidationReport" -ForegroundColor Green
        if ($validation.models.missing_count -gt 0) {
            Write-Host "[WARN] $($v.Suffix) artifact is missing model(s): $($validation.models.missing_models -join ', ')" -ForegroundColor Yellow
        }
        if (-not $validation.doctor.succeeded) {
            Write-Host "[WARN] $($v.Suffix) doctor did not produce a report. Reason: $($validation.doctor.failure_reason)" -ForegroundColor Yellow
        }
    }

    Write-Success "All installers generated, physically verified, and validated."

    Write-Step "Release v$Version is READY"
    Get-ChildItem "dist/*.exe" | Select-Object Name, @{Name="Size(MB)"; Expression={"{0:N2}" -f ($_.Length / 1MB)}} | Format-Table -AutoSize

} catch {
    Write-Host "`n[FATAL ERROR] Pipeline failed at step: $($_.InvocationInfo.ScriptName)" -ForegroundColor Red
    Write-Host "Error Details: $($_.Exception.Message)" -ForegroundColor Red
    exit 1
}
