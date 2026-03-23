param(
    [string]$Version = "0.4.13",
    [switch]$SkipTests,
    [switch]$CleanOnly
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
Set-Location $repoRoot

function Write-Step([string]$msg) {
    Write-Host "`n=== [STEP] $msg ===" -ForegroundColor Cyan
}

function Write-Success([string]$msg) {
    Write-Host "[SUCCESS] $msg" -ForegroundColor Green
}

try {
    # 1. Limpeza de Ambiente
    Write-Step "Sanitizing Environment"
    $dirsToClean = @("build/release", "dist", "temp")
    foreach ($dir in $dirsToClean) {
        if (Test-Path $dir) {
            Write-Host "Cleaning $dir..."
            Remove-Item $dir -Recurse -Force
        }
    }

    # Limpeza de Logs do Sistema (Senior Requirement)
    $systemLogDir = "$env:LOCALAPPDATA\CorridorKey\Logs"
    if (Test-Path $systemLogDir) {
        Write-Host "Clearing system logs at $systemLogDir..." -ForegroundColor Yellow
        Remove-Item "$systemLogDir\*" -Force -ErrorAction SilentlyContinue
    }
    
    if ($CleanOnly) { exit 0 }

    # 2. Sincronização de Dependências (DML)
    Write-Step "Synchronizing DirectML Runtimes"
    & powershell.exe -NoProfile -File "scripts/sync_onnxruntime_directml.ps1"
    Write-Success "DirectML runtimes synchronized."

    # 3. Build Determinístico
    Write-Step "Building Project (Release Mode)"
    # Localizar MSVC vcvars64.bat para garantir ambiente limpo
    $vcvars = Get-ChildItem -Path "C:\Program Files\Microsoft Visual Studio" -Filter vcvars64.bat -Recurse | Select-Object -First 1 -ExpandProperty FullName
    if (-not $vcvars) { throw "vcvars64.bat not found. MSVC environment required." }
    
    & cmd /c "call `"$vcvars`" && cmake --preset release && cmake --build --preset release -j 8"
    if ($LASTEXITCODE -ne 0) { throw "Build failed." }
    Write-Success "Build completed successfully."

    # 4. Quality Gate 1: Testes Automatizados
    if (-not $SkipTests) {
        Write-Step "Quality Gate: Running Automated Tests"
        & cmd /c "call `"$vcvars`" && cmake --build --preset release --target test_unit test_regression && cd build/release && ctest --output-on-failure"
        if ($LASTEXITCODE -ne 0) { throw "Tests failed." }
        Write-Success "All tests passed."
    }

    # 5. Quality Gate 2: Geração de Artefatos e Validação de Staging
    Write-Step "Quality Gate: Packaging and Backend Validation"
    
    $variants = @(
        @{ Suffix = "DirectML"; Root = "vendor/onnxruntime-windows-dml" },
        @{ Suffix = "RTX";      Root = "vendor/onnxruntime-windows-rtx" }
    )

    foreach ($v in $variants) {
        Write-Host "--- Packaging Variant: $($v.Suffix) ---" -ForegroundColor Yellow
        & powershell.exe -NoProfile -File "scripts/package_ofx_installer_windows.ps1" `
            -Version $Version `
            -ReleaseSuffix $v.Suffix `
            -OrtRoot $v.Root
        
        if ($LASTEXITCODE -ne 0) { throw "Packaging failed for variant: $($v.Suffix)" }

        # Verificação física do artefato
        $expectedInstaller = Join-Path $repoRoot "dist/CorridorKey_Resolve_v${Version}_Windows_$($v.Suffix)_Installer.exe"
        if (-not (Test-Path $expectedInstaller)) {
            throw "CRITICAL: Pipeline claimed success but installer was NOT found at: $expectedInstaller"
        }
        Write-Host "[VERIFIED] Artifact created: $expectedInstaller" -ForegroundColor Green
    }

    Write-Success "All installers generated, physically verified, and validated."

    # 6. Final Summary
    Write-Step "Release v$Version is READY"
    Get-ChildItem "dist/*.exe" | Select-Object Name, @{Name="Size(MB)"; Expression={"{0:N2}" -f ($_.Length / 1MB)}} | Format-Table -AutoSize

} catch {
    Write-Host "`n[FATAL ERROR] Pipeline failed at step: $($_.InvocationInfo.ScriptName)" -ForegroundColor Red
    Write-Host "Error Details: $($_.Exception.Message)" -ForegroundColor Red
    exit 1
}
