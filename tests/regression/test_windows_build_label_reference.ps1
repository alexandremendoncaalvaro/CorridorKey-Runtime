Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
. (Join-Path $repoRoot "scripts\windows_runtime_helpers.ps1")

function Assert-True {
    param(
        [bool]$Condition,
        [string]$Message
    )
    if (-not $Condition) {
        throw $Message
    }
}

function Assert-Equal {
    param(
        [string]$Actual,
        [string]$Expected,
        [string]$Message
    )
    if ($Actual -ne $Expected) {
        throw "$Message Expected '$Expected', got '$Actual'."
    }
}

$buildReferenceA = "b20260522T010203004Z"
$buildReferenceB = "b20260522T010203005Z"

Assert-True `
    -Condition (Test-CorridorKeyBuildReference -BuildReference $buildReferenceA) `
    -Message "Known-good build reference should validate."

Assert-Equal `
    -Actual (Add-CorridorKeyBuildReferenceToLabel `
        -DisplayLabel "0.8.4-win.1-28-ge5e88bb-dirty" `
        -BuildReference $buildReferenceA) `
    -Expected "0.8.4-win.1-28-ge5e88bb-dirty-$buildReferenceA" `
    -Message "Local labels must append the build reference."

$tempRoot = Join-Path ([System.IO.Path]::GetTempPath()) (
    "corridorkey-label-test-" + [System.Guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $tempRoot -Force | Out-Null

function Invoke-TempGit {
    param(
        [string[]]$Arguments
    )
    & git -C $tempRoot @Arguments | Out-Null
    if ($LASTEXITCODE -ne 0) {
        throw "git $($Arguments -join ' ') failed in $tempRoot"
    }
}

try {
    Invoke-TempGit -Arguments @("init")
    Invoke-TempGit -Arguments @("config", "user.email", "label-test@example.invalid")
    Invoke-TempGit -Arguments @("config", "user.name", "Label Test")
    Invoke-TempGit -Arguments @("config", "commit.gpgsign", "false")

    Set-Content -LiteralPath (Join-Path $tempRoot "tracked.txt") -Value "one" -Encoding ASCII
    Invoke-TempGit -Arguments @("add", "tracked.txt")
    Invoke-TempGit -Arguments @("commit", "-m", "initial")
    Invoke-TempGit -Arguments @("tag", "v9.8.7-win.4")

    Set-Content -LiteralPath (Join-Path $tempRoot "tracked.txt") -Value "two" -Encoding ASCII
    Invoke-TempGit -Arguments @("commit", "-am", "second")
    Set-Content -LiteralPath (Join-Path $tempRoot "tracked.txt") -Value "dirty" -Encoding ASCII

    $labelA = Get-CorridorKeyDerivedDisplayLabel `
        -RepoRoot $tempRoot `
        -Version "9.8.7" `
        -BuildReference $buildReferenceA
    $labelB = Get-CorridorKeyDerivedDisplayLabel `
        -RepoRoot $tempRoot `
        -Version "9.8.7" `
        -BuildReference $buildReferenceB
    $nextCycleLabel = Get-CorridorKeyDerivedDisplayLabel `
        -RepoRoot $tempRoot `
        -Version "9.8.8" `
        -BuildReference $buildReferenceA

    Assert-True `
        -Condition ($labelA -match '^9\.8\.7-win\.4-1-g[0-9a-f]+-dirty-b20260522T010203004Z$') `
        -Message "Derived local label should include source, dirty state, and build reference. Got '$labelA'."
    Assert-True `
        -Condition ($labelB -match '^9\.8\.7-win\.4-1-g[0-9a-f]+-dirty-b20260522T010203005Z$') `
        -Message "Second derived local label should include the second build reference. Got '$labelB'."
    Assert-True `
        -Condition ($labelA -ne $labelB) `
        -Message "Two build references for the same dirty commit must produce distinct labels."
    Assert-True `
        -Condition ($nextCycleLabel -match '^9\.8\.8-win\.0-1-g[0-9a-f]+-dirty-b20260522T010203004Z$') `
        -Message "A local build for a project version without a published prerelease must not carry the previous version. Got '$nextCycleLabel'."

    Write-Host "[PASS] Windows build label reference regression checks passed." -ForegroundColor Green
} finally {
    if (Test-Path $tempRoot) {
        Remove-Item -LiteralPath $tempRoot -Recurse -Force
    }
}
