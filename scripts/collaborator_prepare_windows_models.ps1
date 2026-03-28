param(
    [string]$Version = "",
    [string]$Checkpoint = "",
    [string]$CorridorKeyRepo = "",
    [string]$CommitBranch = "",
    [string]$CommitMessage = "chore: regenerate windows rtx model pack",
    [string]$GitUserName = "",
    [string]$GitUserEmail = "",
    [switch]$CreateCommit,
    [switch]$Push,
    [switch]$BuildRelease,
    [switch]$SkipReleaseTests
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot

function Invoke-RepoCommand {
    param(
        [string]$FilePath,
        [string[]]$Arguments
    )

    Write-Host ("  > " + ((@($FilePath) + $Arguments) -join " ")) -ForegroundColor DarkGray
    & $FilePath @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed: $FilePath"
    }
}

function Assert-InGitRepository {
    if (-not (Test-Path (Join-Path $repoRoot ".git"))) {
        throw "This script must be run from a CorridorKey-Runtime git clone."
    }
}

function New-OrSwitchArgument {
    param(
        [switch]$Condition,
        [string]$Value
    )

    if ($Condition.IsPresent) {
        return @($Value)
    }
    return @()
}

function Get-GitConfigValue {
    param(
        [string[]]$Arguments
    )

    $value = & git @Arguments 2>$null
    if ($LASTEXITCODE -ne 0) {
        return ""
    }
    return ($value | Out-String).Trim()
}

function Read-RequiredValue {
    param(
        [string]$Prompt
    )

    while ($true) {
        $value = (Read-Host -Prompt $Prompt).Trim()
        if (-not [string]::IsNullOrWhiteSpace($value)) {
            return $value
        }
        Write-Host "[collaborator] A value is required to continue." -ForegroundColor Yellow
    }
}

function Ensure-GitIdentity {
    param(
        [string]$PreferredName,
        [string]$PreferredEmail
    )

    $localName = Get-GitConfigValue -Arguments @("config", "--local", "--get", "user.name")
    $localEmail = Get-GitConfigValue -Arguments @("config", "--local", "--get", "user.email")
    $globalName = Get-GitConfigValue -Arguments @("config", "--global", "--get", "user.name")
    $globalEmail = Get-GitConfigValue -Arguments @("config", "--global", "--get", "user.email")

    $resolvedName = $PreferredName
    if ([string]::IsNullOrWhiteSpace($resolvedName)) {
        $resolvedName = $localName
    }
    if ([string]::IsNullOrWhiteSpace($resolvedName)) {
        $resolvedName = $globalName
    }

    $resolvedEmail = $PreferredEmail
    if ([string]::IsNullOrWhiteSpace($resolvedEmail)) {
        $resolvedEmail = $localEmail
    }
    if ([string]::IsNullOrWhiteSpace($resolvedEmail)) {
        $resolvedEmail = $globalEmail
    }

    if ([string]::IsNullOrWhiteSpace($resolvedName)) {
        Write-Host "[collaborator] Git user.name is not configured. This script will set it for this CorridorKey-Runtime clone only." -ForegroundColor Yellow
        $resolvedName = Read-RequiredValue -Prompt "Enter your Git name"
    }

    if ([string]::IsNullOrWhiteSpace($resolvedEmail)) {
        Write-Host "[collaborator] Git user.email is not configured. This script will set it for this CorridorKey-Runtime clone only." -ForegroundColor Yellow
        $resolvedEmail = Read-RequiredValue -Prompt "Enter your Git email"
    }

    if ($localName -ne $resolvedName) {
        Invoke-RepoCommand -FilePath "git" -Arguments @("config", "--local", "user.name", $resolvedName)
    }
    if ($localEmail -ne $resolvedEmail) {
        Invoke-RepoCommand -FilePath "git" -Arguments @("config", "--local", "user.email", $resolvedEmail)
    }

    Write-Host "[collaborator] Git identity ready for local commit: $resolvedName <$resolvedEmail>" -ForegroundColor Cyan
}

Assert-InGitRepository
Set-Location $repoRoot

Write-Host "[collaborator] Repository: $repoRoot" -ForegroundColor Cyan
Write-Host "[collaborator] Step 1/5: Installing Git LFS hooks" -ForegroundColor Cyan
Invoke-RepoCommand -FilePath "git" -Arguments @("lfs", "install")

Write-Host "[collaborator] Step 2/5: Pulling Git LFS objects" -ForegroundColor Cyan
Invoke-RepoCommand -FilePath "git" -Arguments @("lfs", "pull")

if (-not [string]::IsNullOrWhiteSpace($CommitBranch)) {
    $currentBranch = (& git branch --show-current).Trim()
    if ([string]::IsNullOrWhiteSpace($currentBranch)) {
        throw "Could not determine the current branch."
    }
    if ($currentBranch -ne $CommitBranch) {
        Write-Host "[collaborator] Switching to branch: $CommitBranch" -ForegroundColor Cyan
        Invoke-RepoCommand -FilePath "git" -Arguments @("checkout", "-B", $CommitBranch)
    }
}

$prepareArguments = @(
    "-NoProfile",
    "-ExecutionPolicy", "Bypass",
    "-File", (Join-Path $repoRoot "scripts\windows.ps1"),
    "-Task", "regen-rtx-release"
)
if (-not [string]::IsNullOrWhiteSpace($Version)) {
    $prepareArguments += @("-Version", $Version)
}
if (-not [string]::IsNullOrWhiteSpace($Checkpoint)) {
    $prepareArguments += @("-Checkpoint", $Checkpoint)
}
if (-not [string]::IsNullOrWhiteSpace($CorridorKeyRepo)) {
    $prepareArguments += @("-CorridorKeyRepo", $CorridorKeyRepo)
}

Write-Host "[collaborator] Step 3/5: Regenerating and certifying the Windows RTX release" -ForegroundColor Cyan
Invoke-RepoCommand -FilePath "powershell.exe" -Arguments $prepareArguments

if ($CreateCommit.IsPresent) {
    Ensure-GitIdentity -PreferredName $GitUserName -PreferredEmail $GitUserEmail

    $commitPaths = @(
        "models",
        "CMakeLists.txt",
        "src/gui/package.json",
        "src/gui/src-tauri/Cargo.toml",
        "src/gui/src-tauri/tauri.conf.json"
    )

    Write-Host "[collaborator] Step 4/5: Committing generated models and release version metadata" -ForegroundColor Cyan
    Invoke-RepoCommand -FilePath "git" -Arguments (@("add", "--") + $commitPaths)

    & git diff --cached --quiet -- @commitPaths
    if ($LASTEXITCODE -ne 0) {
        Invoke-RepoCommand -FilePath "git" -Arguments @("commit", "-m", $CommitMessage)
    } else {
        Write-Host "[collaborator] No regenerated model artifacts or version metadata were staged; skipping commit." -ForegroundColor Yellow
    }

    if ($Push.IsPresent) {
        Write-Host "[collaborator] Step 5/5: Pushing branch" -ForegroundColor Cyan
        Invoke-RepoCommand -FilePath "git" -Arguments @("push", "-u", "origin", "HEAD")
    }
}

if ($BuildRelease.IsPresent) {
    if ($SkipReleaseTests.IsPresent) {
        Write-Host "[collaborator] SkipReleaseTests is ignored for the certified Windows RTX regeneration flow." -ForegroundColor Yellow
    }
    Write-Host "[collaborator] Release artifacts are available under dist\\" -ForegroundColor Green
}
