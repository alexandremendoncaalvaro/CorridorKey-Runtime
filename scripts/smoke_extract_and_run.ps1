<#
.SYNOPSIS
    Extract a packaged Windows release artifact in a clean directory and run
    the CLI against it.

.DESCRIPTION
    Tier 2 / Tier 3 gate: the pre-release v0.7.4 named installers in release
    notes that were never uploaded as assets, and CI never touched the
    packaged artifact — only the build tree. This script proves the artifact
    a user would actually download is launchable on a clean filesystem.

    Behavior:
      1. Resolve the artifact (.zip or .exe installer) given on the command line.
      2. Extract / install into a freshly-created temp directory outside the
         source tree.
      3. Locate corridorkey.exe inside the extracted layout.
      4. Run `corridorkey.exe --version --json` and assert:
          - exit code 0
          - stdout parses as JSON
          - the `version` field matches -ExpectedVersion (when provided)
      5. Clean up the temp directory unless -Keep is given.

    .exe installer mode: invokes the Inno Setup generated installer with
    `/VERYSILENT /SUPPRESSMSGBOXES /DIR=<temp>` and uses the resulting
    install directory. After the smoke runs, the installer is uninstalled
    (`unins000.exe /VERYSILENT`) so the runner is left clean.

.PARAMETER Artifact
    Path to the artifact (.zip or installer .exe).

.PARAMETER ExpectedVersion
    Optional version string; smoke fails if `corridorkey --version --json`
    reports a different version. Use this in CI to prove the artifact was
    built from the tag you think it was.

.PARAMETER Keep
    Skip cleanup. Useful for local debugging.

.EXAMPLE
    scripts\smoke_extract_and_run.ps1 dist\CorridorKey_Resolve_v0.7.5_Windows_RTX.zip

.EXAMPLE
    scripts\smoke_extract_and_run.ps1 dist\CorridorKey_Resolve_v0.7.5_Windows_RTX_Installer.exe `
        -ExpectedVersion 0.7.5
#>
param(
    [Parameter(Mandatory = $true, Position = 0)]
    [string]$Artifact,

    [string]$ExpectedVersion = "",

    [switch]$Keep
)

$ErrorActionPreference = "Stop"

function Write-Smoke([string]$msg) {
    Write-Host "[smoke] $msg"
}

function Fail([string]$msg) {
    Write-Host "error: $msg" -ForegroundColor Red
    exit 1
}

if (-not (Test-Path -LiteralPath $Artifact -PathType Leaf)) {
    Fail "artifact not found: $Artifact"
}

$artifactAbs = (Resolve-Path -LiteralPath $Artifact).ProviderPath
$tempRoot = Join-Path $env:TEMP ("ck-smoke-" + [System.Guid]::NewGuid().ToString("N").Substring(0, 8))
New-Item -ItemType Directory -Path $tempRoot -Force | Out-Null

# Track install state so cleanup runs even on early failure paths.
$script:installerUninstallerPath = $null
$script:installDir = $null

function Invoke-Cleanup {
    if ($Keep) {
        Write-Smoke "keeping temp dir: $tempRoot"
        return
    }
    if ($script:installerUninstallerPath -and (Test-Path -LiteralPath $script:installerUninstallerPath)) {
        Write-Smoke "uninstalling: $script:installerUninstallerPath"
        # Inno Setup uninstaller. /VERYSILENT suppresses UI; /NORESTART is a
        # no-op for plugins but harmless and keeps the runner from rebooting.
        Start-Process -FilePath $script:installerUninstallerPath `
            -ArgumentList "/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART" `
            -Wait -PassThru | Out-Null
        Start-Sleep -Seconds 1
    }
    if (Test-Path -LiteralPath $tempRoot) {
        Remove-Item -LiteralPath $tempRoot -Recurse -Force -ErrorAction SilentlyContinue
    }
    if ($script:installDir -and (Test-Path -LiteralPath $script:installDir)) {
        Remove-Item -LiteralPath $script:installDir -Recurse -Force -ErrorAction SilentlyContinue
    }
}

try {
    Write-Smoke "artifact: $artifactAbs"
    Write-Smoke "temp:     $tempRoot"

    $extension = [System.IO.Path]::GetExtension($artifactAbs).ToLowerInvariant()
    $extractDir = Join-Path $tempRoot "extract"
    New-Item -ItemType Directory -Path $extractDir -Force | Out-Null

    switch ($extension) {
        ".zip" {
            Write-Smoke "extracting zip"
            Expand-Archive -LiteralPath $artifactAbs -DestinationPath $extractDir -Force
        }
        ".exe" {
            # Treat as an Inno Setup installer. /DIR forces the install root
            # into our temp tree so we don't pollute Program Files and so the
            # runner doesn't need an admin token (Inno user-mode install when
            # the installer was built without RequiresAdmin).
            $installDir = Join-Path $tempRoot "install"
            New-Item -ItemType Directory -Path $installDir -Force | Out-Null
            $script:installDir = $installDir
            Write-Smoke "running installer silently into $installDir"
            $proc = Start-Process -FilePath $artifactAbs `
                -ArgumentList "/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART", "/DIR=`"$installDir`"" `
                -Wait -PassThru
            if ($proc.ExitCode -ne 0) {
                Fail "installer exited with code $($proc.ExitCode)"
            }
            $extractDir = $installDir
            $script:installerUninstallerPath = Join-Path $installDir "unins000.exe"
        }
        default {
            Fail "unsupported artifact extension: $extension (supported: .zip, .exe)"
        }
    }

    # Locate corridorkey.exe inside the extracted / installed tree.
    $cli = Get-ChildItem -Path $extractDir -Recurse -Filter "corridorkey.exe" -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if (-not $cli) {
        Write-Smoke "artifact contents:"
        Get-ChildItem -Path $extractDir -Recurse -Depth 4 | ForEach-Object { Write-Host "  $($_.FullName)" }
        Fail "no corridorkey.exe found inside $extractDir"
    }
    $cliPath = $cli.FullName
    Write-Smoke "cli path: $cliPath"
    Write-Smoke "running: $cliPath --version --json"

    # Run from the temp root so we exercise the same code path as a user
    # launching the binary outside the source tree. A regression where the
    # CLI implicitly resolved $REPO_ROOT/models would surface here.
    Push-Location $tempRoot
    try {
        $output = & $cliPath --version --json 2>&1
        $exitCode = $LASTEXITCODE
    } finally {
        Pop-Location
    }

    if ($exitCode -ne 0) {
        Write-Host "[smoke] output:" -ForegroundColor Yellow
        $output | ForEach-Object { Write-Host "  $_" }
        Fail "corridorkey --version exited with code $exitCode"
    }

    $outputText = ($output | Out-String).Trim()
    Write-Smoke "output: $outputText"

    try {
        $parsed = $outputText | ConvertFrom-Json -ErrorAction Stop
    } catch {
        Fail "--version output is not valid JSON: $($_.Exception.Message)"
    }

    $reportedVersion = $parsed.version
    if (-not $reportedVersion) {
        Fail "missing .version field in --version output"
    }
    Write-Smoke "reported version: $reportedVersion"

    if ($ExpectedVersion -and ($reportedVersion -ne $ExpectedVersion)) {
        Fail @"
version mismatch — artifact reports '$reportedVersion', expected '$ExpectedVersion'.
This means the artifact was built from the wrong source ref or with the
wrong CORRIDORKEY_DISPLAY_VERSION_LABEL.
"@
    }

    Write-Smoke "PASS"
    exit 0
} finally {
    Invoke-Cleanup
}
