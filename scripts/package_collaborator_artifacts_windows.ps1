param(
    [string]$Version = "",
    [string]$OutputZip = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot "windows_runtime_helpers.ps1")

$resolvedVersion = if ([string]::IsNullOrWhiteSpace($Version)) {
    Get-CorridorKeyProjectVersion -RepoRoot $repoRoot
} else {
    $Version
}
$gitExecutable = Resolve-CorridorKeyGitPath
$distRoot = Join-Path $repoRoot "dist"
$stageRoot = Join-Path $repoRoot "temp\collaborator-artifacts"
$packageRoot = Join-Path $stageRoot ("CorridorKey_Collaborator_v" + $resolvedVersion + "_Windows_RTX")

if ([string]::IsNullOrWhiteSpace($OutputZip)) {
    $OutputZip = Join-Path $distRoot ("CorridorKey_Collaborator_v" + $resolvedVersion + "_Windows_RTX.zip")
}

if (Test-Path $stageRoot) {
    Remove-Item -Path $stageRoot -Recurse -Force
}

if (Test-Path $OutputZip) {
    Remove-Item -Path $OutputZip -Force
}

New-Item -ItemType Directory -Path $packageRoot -Force | Out-Null
$modelsDestination = Join-Path $packageRoot "models"
$reportsDestination = Join-Path $packageRoot "reports"
$installersDestination = Join-Path $packageRoot "installers"
New-Item -ItemType Directory -Path $modelsDestination -Force | Out-Null
New-Item -ItemType Directory -Path $reportsDestination -Force | Out-Null
New-Item -ItemType Directory -Path $installersDestination -Force | Out-Null

$gitBranch = ""
$gitCommit = ""
if (-not [string]::IsNullOrWhiteSpace($gitExecutable) -and (Test-Path (Join-Path $repoRoot ".git"))) {
    $gitBranch = (& $gitExecutable -C $repoRoot branch --show-current 2>$null | Out-String).Trim()
    $gitCommit = (& $gitExecutable -C $repoRoot rev-parse HEAD 2>$null | Out-String).Trim()
}

$includedModels = [System.Collections.Generic.List[string]]::new()
foreach ($modelPath in @(Get-ChildItem -Path (Join-Path $repoRoot "models") -Filter "corridorkey*.onnx" -File -ErrorAction SilentlyContinue)) {
    Copy-Item -Path $modelPath.FullName -Destination (Join-Path $modelsDestination $modelPath.Name) -Force
    [void]$includedModels.Add($modelPath.Name)
}

$includedReports = [System.Collections.Generic.List[string]]::new()
foreach ($reportPath in @(
        (Join-Path $repoRoot "build\release\windows_rtx_validation_report.json"),
        (Join-Path $repoRoot "build\debug\windows_rtx_validation_report.json")
    )) {
    if (Test-Path $reportPath) {
        $reportName = Split-Path $reportPath -Leaf
        Copy-Item -Path $reportPath -Destination (Join-Path $reportsDestination $reportName) -Force
        [void]$includedReports.Add(("reports\" + $reportName))
    }
}

foreach ($variant in Get-CorridorKeyWindowsOfxReleaseVariants -Track "rtx") {
    $bundleRoot = Join-Path $distRoot ("CorridorKey_OFX_v" + $resolvedVersion + "_Windows_" + $variant.Suffix)
    foreach ($bundleReportName in @("bundle_validation.json", "doctor_report.json", "model_inventory.json")) {
        $bundleReportPath = Join-Path $bundleRoot $bundleReportName
        if (Test-Path $bundleReportPath) {
            $destinationName = $variant.Suffix + "_" + $bundleReportName
            Copy-Item -Path $bundleReportPath -Destination (Join-Path $reportsDestination $destinationName) -Force
            [void]$includedReports.Add(("reports\" + $destinationName))
        }
    }
}

$includedInstallers = [System.Collections.Generic.List[string]]::new()
foreach ($variant in Get-CorridorKeyWindowsOfxReleaseVariants -Track "rtx") {
    $installerName = "CorridorKey_OFX_v" + $resolvedVersion + "_Windows_" + $variant.Suffix + "_Install.exe"
    $installerPath = Join-Path $distRoot $installerName
    if (Test-Path $installerPath) {
        Copy-Item -Path $installerPath -Destination (Join-Path $installersDestination $installerName) -Force
        [void]$includedInstallers.Add($installerName)
    }
}

$manifestPath = Join-Path $packageRoot "artifact_manifest.json"
$manifest = [ordered]@{
    version = $resolvedVersion
    generated_at_local = (Get-Date).ToString("s")
    git_branch = $gitBranch
    git_commit = $gitCommit
    models = @($includedModels)
    reports = @($includedReports)
    installers = @($includedInstallers)
}
Write-CorridorKeyJsonFile -Path $manifestPath -Payload $manifest

Compress-Archive -Path (Join-Path $packageRoot "*") -DestinationPath $OutputZip -CompressionLevel Optimal -Force

Write-Host "[collaborator-artifacts] Packaged collaborator artifacts: $OutputZip" -ForegroundColor Green
