param(
    [string]$Version = "0.5.2",
    [string]$RuntimeBranch = "codex/windows-proofed-validation",
    [string]$RuntimeRepoUrl = "https://github.com/alexandremendoncaalvaro/CorridorKey-Runtime.git",
    [string]$RuntimeRepoName = "CorridorKey-Runtime",
    [string]$SourceRepoName = "CorridorKey",
    [string]$CommitBranch = "",
    [switch]$SkipReleaseTests
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Invoke-BootstrapCommand {
    param(
        [string]$FilePath,
        [string[]]$Arguments = @(),
        [string]$WorkingDirectory = ""
    )

    Write-Host ("  > " + ((@($FilePath) + $Arguments) -join " ")) -ForegroundColor DarkGray

    if ([string]::IsNullOrWhiteSpace($WorkingDirectory)) {
        & $FilePath @Arguments
    } else {
        Push-Location $WorkingDirectory
        try {
            & $FilePath @Arguments
        } finally {
            Pop-Location
        }
    }

    if ($LASTEXITCODE -ne 0) {
        throw "Command failed: $FilePath"
    }
}

function Get-SafeBranchSuffix {
    param([string]$Value)

    $safeValue = ($Value -replace "[^A-Za-z0-9._-]", "-").Trim("-")
    if ([string]::IsNullOrWhiteSpace($safeValue)) {
        return "collaborator"
    }
    return $safeValue.ToLowerInvariant()
}

function Resolve-CommitBranch {
    param([string]$ExplicitBranch)

    if (-not [string]::IsNullOrWhiteSpace($ExplicitBranch)) {
        return $ExplicitBranch
    }

    return "codex/model-pack-" + (Get-SafeBranchSuffix -Value $env:USERNAME)
}

$parentRoot = (Get-Location).Path
$sourceRepoRoot = Join-Path $parentRoot $SourceRepoName
$runtimeRepoRoot = Join-Path $parentRoot $RuntimeRepoName
$commitBranchName = Resolve-CommitBranch -ExplicitBranch $CommitBranch

if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
    throw "git was not found on PATH."
}

if (-not (Test-Path (Join-Path $sourceRepoRoot "CorridorKeyModule"))) {
    throw "Expected sibling source repo at $sourceRepoRoot. Run this from the parent folder that contains CorridorKey."
}

if (-not (Test-Path $runtimeRepoRoot)) {
    Write-Host "[bootstrap] Cloning CorridorKey-Runtime into $runtimeRepoRoot" -ForegroundColor Cyan
    Invoke-BootstrapCommand -FilePath "git" -Arguments @(
        "clone",
        "--branch", $RuntimeBranch,
        $RuntimeRepoUrl,
        $runtimeRepoRoot
    )
} elseif (-not (Test-Path (Join-Path $runtimeRepoRoot ".git"))) {
    throw "Existing path is not a git repository: $runtimeRepoRoot"
} else {
    Write-Host "[bootstrap] Reusing existing CorridorKey-Runtime clone at $runtimeRepoRoot" -ForegroundColor Cyan
    Invoke-BootstrapCommand -FilePath "git" -WorkingDirectory $runtimeRepoRoot -Arguments @("fetch", "origin", $RuntimeBranch)
    Invoke-BootstrapCommand -FilePath "git" -WorkingDirectory $runtimeRepoRoot -Arguments @("checkout", $RuntimeBranch)
    Invoke-BootstrapCommand -FilePath "git" -WorkingDirectory $runtimeRepoRoot -Arguments @("pull", "--ff-only", "origin", $RuntimeBranch)
}

$collaboratorScript = Join-Path $runtimeRepoRoot "scripts\collaborator_prepare_windows_models.ps1"
if (-not (Test-Path $collaboratorScript)) {
    throw "Collaborator workflow script not found: $collaboratorScript"
}

$arguments = @(
    "-NoProfile",
    "-ExecutionPolicy", "Bypass",
    "-File", $collaboratorScript,
    "-Version", $Version,
    "-CorridorKeyRepo", $sourceRepoRoot,
    "-CreateCommit",
    "-Push",
    "-BuildRelease",
    "-CommitBranch", $commitBranchName
)

if ($SkipReleaseTests.IsPresent) {
    $arguments += "-SkipReleaseTests"
}

Write-Host "[bootstrap] Starting one-shot collaborator workflow" -ForegroundColor Cyan
Write-Host "[bootstrap] Parent folder: $parentRoot" -ForegroundColor Cyan
Write-Host "[bootstrap] CorridorKey source: $sourceRepoRoot" -ForegroundColor Cyan
Write-Host "[bootstrap] Runtime repo: $runtimeRepoRoot" -ForegroundColor Cyan
Write-Host "[bootstrap] Commit branch: $commitBranchName" -ForegroundColor Cyan

Invoke-BootstrapCommand -FilePath "powershell.exe" -Arguments $arguments

Write-Host "[bootstrap] Done." -ForegroundColor Green
Write-Host "[bootstrap] Release artifacts are under $runtimeRepoRoot\\dist" -ForegroundColor Green
