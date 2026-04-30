<#
.SYNOPSIS
    Headless Foundry Nuke smoke test for the CorridorKey OFX plugin.

.DESCRIPTION
    Spawns Nuke in terminal mode (-t) with OFX_PLUGIN_PATH pointed at the
    staged plugin bundle's parent directory, runs tests/e2e/test_ofx_nuke_smoke.py
    inside it, and validates that a frame was rendered.

    The script clears Nuke's OFX cache before launch so the freshly staged
    bundle is rescanned every run (per Foundry support article Q100024).

    Exit code is the Nuke exit code on failure, or 0 on full success.

.PARAMETER BundlePath
    Path to the CorridorKey.ofx.bundle directory to test. Defaults to the
    build/release output. The directory's parent is added to OFX_PLUGIN_PATH.

.PARAMETER NukeExe
    Full path to the Nuke executable. When omitted, the script auto-discovers
    Nuke under "$env:ProgramFiles\Nuke*\Nuke*.exe" and picks the highest
    version. The CORRIDORKEY_NUKE_EXE env variable also overrides discovery.

.PARAMETER OutputPath
    Path where Nuke should write the rendered .exr. When omitted a
    timestamped file is written under the system temp directory.

.PARAMETER ResultJsonPath
    Path where the script writes a structured JSON result. Defaults to a
    timestamped file under TEMP. The validator (validate_ofx_win.ps1) reads
    this when invoked through the Phase 4 hook.
#>
param(
    [string]$BundlePath = "",
    [string]$NukeExe = "",
    [string]$OutputPath = "",
    [string]$ResultJsonPath = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot

function Resolve-NukeExecutable {
    param([string]$Explicit)

    if (-not [string]::IsNullOrWhiteSpace($Explicit)) {
        if (-not (Test-Path $Explicit)) {
            throw "Nuke executable not found at -NukeExe path: $Explicit"
        }
        return $Explicit
    }

    if (-not [string]::IsNullOrWhiteSpace($env:CORRIDORKEY_NUKE_EXE)) {
        if (-not (Test-Path $env:CORRIDORKEY_NUKE_EXE)) {
            throw "CORRIDORKEY_NUKE_EXE points to non-existent path: $env:CORRIDORKEY_NUKE_EXE"
        }
        return $env:CORRIDORKEY_NUKE_EXE
    }

    $programFiles = $env:ProgramFiles
    if ([string]::IsNullOrWhiteSpace($programFiles)) {
        $programFiles = "C:\Program Files"
    }

    $nukeRoots = @(Get-ChildItem -Path $programFiles -Directory -Filter "Nuke*" -ErrorAction SilentlyContinue)
    if ($nukeRoots.Count -eq 0) {
        return $null
    }

    # Pick highest version directory and the matching Nuke<ver>.exe inside.
    $sorted = $nukeRoots | Sort-Object Name -Descending
    foreach ($root in $sorted) {
        $candidate = Get-ChildItem -Path $root.FullName -Filter "Nuke*.exe" -File -ErrorAction SilentlyContinue |
                     Where-Object { $_.Name -notmatch "Crash|Studio|Indie" } |
                     Sort-Object Name -Descending |
                     Select-Object -First 1
        if ($null -ne $candidate) {
            return $candidate.FullName
        }
    }

    return $null
}

function Resolve-NukeOfxCacheDirectory {
    return Join-Path $env:LOCALAPPDATA "Temp\nuke\ofxplugincache"
}

function Clear-NukeOfxCache {
    $cacheDir = Resolve-NukeOfxCacheDirectory
    if (-not (Test-Path $cacheDir)) {
        Write-Host "[smoke] Nuke OFX cache directory does not exist yet: $cacheDir" -ForegroundColor Gray
        return
    }
    $cacheFiles = Get-ChildItem -Path $cacheDir -Filter "ofxplugincache_Nuke*-64.xml" -File -ErrorAction SilentlyContinue
    foreach ($file in $cacheFiles) {
        Remove-Item $file.FullName -Force -ErrorAction SilentlyContinue
        Write-Host "[smoke] Cleared Nuke OFX cache: $($file.FullName)" -ForegroundColor Gray
    }
}

function Save-ResultJson {
    param(
        [string]$Path,
        [hashtable]$Payload
    )
    $json = $Payload | ConvertTo-Json -Depth 6
    Set-Content -Path $Path -Value $json -Encoding UTF8
    Write-Host "[smoke] Result JSON: $Path" -ForegroundColor Cyan
}

if ([string]::IsNullOrWhiteSpace($BundlePath)) {
    $BundlePath = Join-Path $repoRoot "build\release\CorridorKey.ofx.bundle"
}
$BundlePath = [System.IO.Path]::GetFullPath($BundlePath)

if (-not (Test-Path $BundlePath)) {
    throw "Bundle path not found: $BundlePath. Build the OFX bundle first."
}

$resolvedNuke = Resolve-NukeExecutable -Explicit $NukeExe
if ([string]::IsNullOrWhiteSpace($resolvedNuke)) {
    $message = "Nuke executable not found. Set -NukeExe or CORRIDORKEY_NUKE_EXE."
    if (-not [string]::IsNullOrWhiteSpace($ResultJsonPath)) {
        Save-ResultJson -Path $ResultJsonPath -Payload @{
            attempted = $true
            succeeded = $false
            failure_reason = $message
            nuke_exe = ""
            nuke_version = ""
            bundle_path = $BundlePath
            output_path = ""
            exit_code = -1
        }
    }
    throw $message
}

$pluginPathParent = Split-Path -Parent $BundlePath
Write-Host "[smoke] Nuke executable: $resolvedNuke" -ForegroundColor Cyan
Write-Host "[smoke] Bundle path:     $BundlePath" -ForegroundColor Cyan
Write-Host "[smoke] OFX_PLUGIN_PATH: $pluginPathParent" -ForegroundColor Cyan

if ([string]::IsNullOrWhiteSpace($OutputPath)) {
    $stamp = (Get-Date -Format "yyyyMMddHHmmss")
    $OutputPath = Join-Path $env:TEMP "corridorkey_nuke_smoke_${stamp}.exr"
}
if ([string]::IsNullOrWhiteSpace($ResultJsonPath)) {
    $stamp = (Get-Date -Format "yyyyMMddHHmmss")
    $ResultJsonPath = Join-Path $env:TEMP "corridorkey_nuke_smoke_${stamp}.json"
}

if (Test-Path $OutputPath) {
    Remove-Item $OutputPath -Force -ErrorAction SilentlyContinue
}

Clear-NukeOfxCache

$smokeScript = Join-Path $repoRoot "tests\e2e\test_ofx_nuke_smoke.py"
if (-not (Test-Path $smokeScript)) {
    throw "Smoke script missing: $smokeScript"
}

$previousOfxPath = $env:OFX_PLUGIN_PATH
$env:OFX_PLUGIN_PATH = if ([string]::IsNullOrWhiteSpace($previousOfxPath)) {
    $pluginPathParent
} else {
    "$pluginPathParent;$previousOfxPath"
}

$nukeStdoutPath = Join-Path $env:TEMP ("corridorkey_nuke_smoke_stdout_" + [System.Guid]::NewGuid().ToString("N") + ".txt")
$nukeStderrPath = Join-Path $env:TEMP ("corridorkey_nuke_smoke_stderr_" + [System.Guid]::NewGuid().ToString("N") + ".txt")

# Nuke -t treats trailing args as sys.argv for the script. We pass the output
# path through so the Python test writes to the location the runner expects.
$nukeArgs = @("-t", $smokeScript, "--output", $OutputPath)

$exitCode = -1
$failureReason = ""
try {
    $process = Start-Process -FilePath $resolvedNuke -ArgumentList $nukeArgs `
        -RedirectStandardOutput $nukeStdoutPath -RedirectStandardError $nukeStderrPath `
        -PassThru -Wait -NoNewWindow
    $exitCode = $process.ExitCode
} catch {
    $failureReason = "Failed to spawn Nuke: $_"
} finally {
    if ([string]::IsNullOrWhiteSpace($previousOfxPath)) {
        Remove-Item Env:OFX_PLUGIN_PATH -ErrorAction SilentlyContinue
    } else {
        $env:OFX_PLUGIN_PATH = $previousOfxPath
    }
}

$nukeStdout = if (Test-Path $nukeStdoutPath) { Get-Content $nukeStdoutPath -Raw } else { "" }
$nukeStderr = if (Test-Path $nukeStderrPath) { Get-Content $nukeStderrPath -Raw } else { "" }

if (-not [string]::IsNullOrWhiteSpace($nukeStdout)) {
    Write-Host "[smoke] Nuke stdout:" -ForegroundColor Gray
    Write-Host $nukeStdout
}
if (-not [string]::IsNullOrWhiteSpace($nukeStderr)) {
    Write-Host "[smoke] Nuke stderr:" -ForegroundColor Yellow
    Write-Host $nukeStderr
}

Remove-Item $nukeStdoutPath -Force -ErrorAction SilentlyContinue
Remove-Item $nukeStderrPath -Force -ErrorAction SilentlyContinue

$nukeVersion = ""
if ($nukeStdout -match "Nuke version:\s*(\S+)") {
    $nukeVersion = $Matches[1]
}

$succeeded = ($exitCode -eq 0) -and (Test-Path $OutputPath)
if (-not $succeeded -and [string]::IsNullOrWhiteSpace($failureReason)) {
    if ($exitCode -ne 0) {
        $failureReason = "Nuke exited with code $exitCode"
    } elseif (-not (Test-Path $OutputPath)) {
        $failureReason = "Render output not found at $OutputPath"
    }
}

Save-ResultJson -Path $ResultJsonPath -Payload @{
    attempted = $true
    succeeded = $succeeded
    failure_reason = $failureReason
    nuke_exe = $resolvedNuke
    nuke_version = $nukeVersion
    bundle_path = $BundlePath
    output_path = $OutputPath
    exit_code = $exitCode
}

if ($succeeded) {
    Write-Host "[smoke] PASS" -ForegroundColor Green
    exit 0
} else {
    Write-Host "[smoke] FAIL: $failureReason" -ForegroundColor Red
    if ($exitCode -ne 0) {
        exit $exitCode
    }
    exit 1
}
