Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$templatePath = Join-Path $repoRoot "scripts\installer\corridorkey.iss.template"
$builderPath = Join-Path $repoRoot "scripts\installer\build_installer.ps1"

function Get-IssTopLevelBlock {
    param(
        [string]$Content,
        [string]$SignaturePattern,
        [string]$BlockName
    )

    $match = [regex]::Match($Content, "(?ms)$SignaturePattern.*?^end;")
    if (-not $match.Success) {
        throw "Could not find Inno block: $BlockName."
    }

    return $match.Value
}

$template = Get-Content -Path $templatePath -Raw
$builder = Get-Content -Path $builderPath -Raw

$initializeWizard = Get-IssTopLevelBlock `
    -Content $template `
    -SignaturePattern "procedure\s+InitializeWizard;" `
    -BlockName "InitializeWizard"

if ($initializeWizard.Contains("CorridorKeyMigrateLegacyPackMarkers")) {
    throw "InitializeWizard must not migrate pack markers because {app} is not initialized yet."
}

$nextButtonClick = Get-IssTopLevelBlock `
    -Content $template `
    -SignaturePattern "function\s+NextButtonClick\(CurPageID:\s+Integer\):\s+Boolean;" `
    -BlockName "NextButtonClick"

$readyGateIndex = $nextButtonClick.IndexOf("if CurPageID = wpReady then begin", [StringComparison]::Ordinal)
$migrationIndex = $nextButtonClick.IndexOf("CorridorKeyMigrateLegacyPackMarkers;", [StringComparison]::Ordinal)
$enqueueIndex = $nextButtonClick.IndexOf("CorridorKeyEnqueueDownloads(DownloadPage);", [StringComparison]::Ordinal)

if ($readyGateIndex -lt 0) {
    throw "NextButtonClick must retain the wpReady gate used to plan online downloads."
}
if ($migrationIndex -lt 0) {
    throw "NextButtonClick(wpReady) must migrate legacy pack markers."
}
if ($enqueueIndex -lt 0) {
    throw "NextButtonClick(wpReady) must enqueue downloads."
}
if ($migrationIndex -lt $readyGateIndex -or $migrationIndex -gt $enqueueIndex) {
    throw "Pack marker migration must run inside wpReady before downloads are enqueued."
}

if ($builder.Contains("Runs from InitializeWizard")) {
    throw "Build-PackMigrationProcedure must not document InitializeWizard as the migration call site."
}

if ($builder.Contains('default_dir_name = "{commoncf64}\OFX\Plugins\{#MyAppName}.ofx.bundle"')) {
    throw "OFX DefaultDirName must not inject a preprocessor constant through another preprocessor constant."
}

if (-not $builder.Contains('default_dir_name = "{commoncf64}\OFX\Plugins\CorridorKey.ofx.bundle"')) {
    throw "OFX DefaultDirName must render to a literal OpenFX bundle path before ISCC runs."
}

Write-Host "[PASS] Inno {app} initialization regression checks passed." -ForegroundColor Green
