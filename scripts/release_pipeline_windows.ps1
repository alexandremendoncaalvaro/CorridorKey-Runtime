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

    Write-Step "Building Project (Release Mode)"
    $vcvars = Get-ChildItem -Path "C:\Program Files\Microsoft Visual Studio" -Filter vcvars64.bat -Recurse | Select-Object -First 1 -ExpandProperty FullName
    if (-not $vcvars) { throw "vcvars64.bat not found. MSVC environment required." }

    & cmd /c "call `"$vcvars`" && cmake --preset release && cmake --build --preset release -j 8"
    if ($LASTEXITCODE -ne 0) { throw "Build failed." }
    Write-Success "Build completed successfully."

    if (-not $SkipTests) {
        Write-Step "Quality Gate: Running Automated Tests"
        # Temporarily append the OFX output directory to PATH so tests can resolve Torch/TensorRT DLLs natively
        $envPathOld = $env:PATH
        $env:PATH = "$repoRoot\build\release\src\plugins\ofx;$envPathOld"

        try {
            & cmd /c "call `"$vcvars`" && cmake --build --preset release --target test_unit test_integration test_regression test_e2e && cd build/release && ctest --output-on-failure"
            if ($LASTEXITCODE -ne 0) { throw "Tests failed." }
        } finally {
            $env:PATH = $envPathOld
        }

        Write-Success "All tests passed."
    }

    Write-Step "Quality Gate: Packaging and Backend Validation"

    Write-Host "--- Packaging Windows Torch-TRT Variant ---" -ForegroundColor Yellow
    & powershell.exe -NoProfile -File "scripts/package_ofx_installer_windows.ps1" -Version $Version

    if ($LASTEXITCODE -ne 0) { throw "Packaging failed" }

    Write-Success "Installer generated, physically verified, and validated."

    Write-Step "Release v$Version is READY"
    Get-ChildItem "dist/*.exe" | Select-Object Name, @{Name="Size(MB)"; Expression={"{0:N2}" -f ($_.Length / 1MB)}} | Format-Table -AutoSize

} catch {
    Write-Host "`n[FATAL ERROR] Pipeline failed at step: $($_.InvocationInfo.ScriptName)" -ForegroundColor Red
    Write-Host "Error Details: $($_.Exception.Message)" -ForegroundColor Red
    exit 1
}
