Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$scriptPath = Join-Path $repoRoot "scripts\release_pipeline_windows.ps1"

$tokens = $null
$parseErrors = $null
$ast = [System.Management.Automation.Language.Parser]::ParseFile($scriptPath, [ref]$tokens, [ref]$parseErrors)
if ($parseErrors.Count -gt 0) {
    throw "Failed to parse release_pipeline_windows.ps1: $($parseErrors[0].Message)"
}

$functionAst = $ast.Find({
        param($node)
        $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and
        $node.Name -eq "Assert-CorridorKeyWindowsReleaseLabel"
    }, $true)
if ($null -eq $functionAst) {
    throw "Assert-CorridorKeyWindowsReleaseLabel was not found in release_pipeline_windows.ps1."
}

. ([scriptblock]::Create($functionAst.Extent.Text))

function Assert-DoesNotThrow {
    param(
        [scriptblock]$ScriptBlock,
        [string]$Message
    )

    try {
        & $ScriptBlock
    } catch {
        throw "$Message Error: $($_.Exception.Message)"
    }
}

function Assert-ThrowsLike {
    param(
        [scriptblock]$ScriptBlock,
        [string]$Pattern,
        [string]$Message
    )

    $threw = $false
    try {
        & $ScriptBlock
    } catch {
        $threw = $true
        if ($_.Exception.Message -notmatch $Pattern) {
            throw "$Message Unexpected error: $($_.Exception.Message)"
        }
    }

    if (-not $threw) {
        throw $Message
    }
}

Assert-DoesNotThrow -Message "Expected the published prerelease label to be accepted." -ScriptBlock {
    Assert-CorridorKeyWindowsReleaseLabel `
        -Version "0.8.5" `
        -DisplayVersionLabel "0.8.5-win.2" `
        -Publishing $true
}

Assert-ThrowsLike -Pattern "does not match" -Message "Expected a published label with the wrong core version to fail." -ScriptBlock {
    Assert-CorridorKeyWindowsReleaseLabel `
        -Version "0.8.5" `
        -DisplayVersionLabel "0.8.4-win.2" `
        -Publishing $true
}

Assert-DoesNotThrow -Message "Expected a clean local git-describe label to be accepted for local release builds." -ScriptBlock {
    Assert-CorridorKeyWindowsReleaseLabel `
        -Version "0.8.5" `
        -DisplayVersionLabel "0.8.4-win.1-32-gf9fe45b" `
        -Publishing $false
}

Assert-DoesNotThrow -Message "Expected a dirty local git-describe label to be accepted for local release builds." -ScriptBlock {
    Assert-CorridorKeyWindowsReleaseLabel `
        -Version "0.8.5" `
        -DisplayVersionLabel "0.8.4-win.1-32-gf9fe45b-dirty" `
        -Publishing $false
}

Assert-ThrowsLike -Pattern "prerelease label" -Message "Expected local git-describe labels to be rejected for GitHub publication." -ScriptBlock {
    Assert-CorridorKeyWindowsReleaseLabel `
        -Version "0.8.5" `
        -DisplayVersionLabel "0.8.4-win.1-32-gf9fe45b" `
        -Publishing $true
}

Assert-ThrowsLike -Pattern "local or prerelease label" -Message "Expected malformed labels to fail." -ScriptBlock {
    Assert-CorridorKeyWindowsReleaseLabel `
        -Version "0.8.5" `
        -DisplayVersionLabel "local-test-build" `
        -Publishing $false
}

Write-Host "[PASS] Windows release label validation regression checks passed." -ForegroundColor Green
