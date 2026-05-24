param(
    [Parameter(Mandatory = $true)]
    [string]$PackagePath,
    [string]$CommonPluginInstallPath = "",
    [ValidateSet("clean", "upgrade")]
    [string]$Mode = "clean",
    [string]$ReportPath = "",
    [switch]$AllowSystemPluginPath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot

function Resolve-FullPath {
    param([string]$Path)

    if ([string]::IsNullOrWhiteSpace($Path)) {
        throw "Path must not be empty."
    }
    if ([System.IO.Path]::IsPathRooted($Path)) {
        return [System.IO.Path]::GetFullPath($Path)
    }
    return [System.IO.Path]::GetFullPath((Join-Path (Get-Location) $Path))
}

function Test-PathUnderRoot {
    param([string]$Path, [string]$Root)

    $pathSeparators = [char[]]@([System.IO.Path]::DirectorySeparatorChar, [System.IO.Path]::AltDirectorySeparatorChar)
    $normalizedPath = ([System.IO.Path]::GetFullPath($Path)).TrimEnd($pathSeparators)
    $normalizedRoot = ([System.IO.Path]::GetFullPath($Root)).TrimEnd($pathSeparators)
    $rootPrefix = $normalizedRoot + [System.IO.Path]::DirectorySeparatorChar
    return $normalizedPath.StartsWith($rootPrefix, [System.StringComparison]::OrdinalIgnoreCase)
}

function Get-AdobeCommonPluginInstallPath {
    foreach ($registryRoot in @("HKLM:\SOFTWARE\Adobe\After Effects", "HKLM:\SOFTWARE\WOW6432Node\Adobe\After Effects")) {
        if (-not (Test-Path $registryRoot)) {
            continue
        }
        $keys = @(Get-ChildItem -Path $registryRoot -ErrorAction SilentlyContinue)
        foreach ($key in ($keys | Sort-Object -Property PSChildName -Descending)) {
            try {
                $value = $key.GetValue("CommonPluginInstallPath", $null, "DoNotExpandEnvironmentNames")
                if ($null -ne $value -and -not [string]::IsNullOrWhiteSpace([string]$value)) {
                    return ([string]$value).Trim()
                }
            } catch {
            }
        }
    }
    return Join-Path $env:ProgramFiles "Adobe\Common\Plug-ins\7.0\MediaCore"
}

function Assert-PathExists {
    param([string]$Path, [string]$Message)

    if (-not (Test-Path -LiteralPath $Path)) {
        throw $Message
    }
}

function Assert-CommonPluginPathAllowed {
    param(
        [string]$Path,
        [switch]$AllowSystemPluginPath
    )

    if ($AllowSystemPluginPath.IsPresent) {
        return
    }

    $tempRoot = [System.IO.Path]::GetTempPath()
    $buildRoot = Join-Path $repoRoot "build"
    $distRoot = Join-Path $repoRoot "dist"
    if ((Test-PathUnderRoot -Path $Path -Root $tempRoot) -or
        (Test-PathUnderRoot -Path $Path -Root $buildRoot) -or
        (Test-PathUnderRoot -Path $Path -Root $distRoot)) {
        return
    }

    throw "Refusing to mutate non-temporary Adobe Common Plug-ins path without -AllowSystemPluginPath: $Path"
}

function Assert-SafeAdobeInstallDestination {
    param(
        [string]$Destination,
        [string]$Root
    )

    $resolvedDestination = [System.IO.Path]::GetFullPath($Destination)
    $resolvedRoot = [System.IO.Path]::GetFullPath($Root)
    $pathSeparators = [char[]]@([System.IO.Path]::DirectorySeparatorChar, [System.IO.Path]::AltDirectorySeparatorChar)
    $normalizedDestination = $resolvedDestination.TrimEnd($pathSeparators)
    $normalizedRoot = $resolvedRoot.TrimEnd($pathSeparators)
    $rootPrefix = $normalizedRoot + [System.IO.Path]::DirectorySeparatorChar

    if ($normalizedDestination -eq $normalizedRoot -or
        -not $normalizedDestination.StartsWith($rootPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Unsafe Adobe install destination: $resolvedDestination"
    }

    return $resolvedDestination
}

function New-StaleUpgradePayload {
    param([string]$TargetPath)

    $staleWin64Dir = Join-Path $TargetPath "Contents\Win64"
    [System.IO.Directory]::CreateDirectory($staleWin64Dir) | Out-Null
    [System.IO.File]::WriteAllText(
        (Join-Path $staleWin64Dir "corridorkey_adobe.aex"),
        "retired",
        [System.Text.Encoding]::ASCII
    )
}

function Assert-InstalledAdobePayload {
    param([string]$TargetPath)

    $win64Dir = Join-Path $TargetPath "Contents\Win64"
    foreach ($requiredFile in @(
            "corridorkey_adobe_green.aex",
            "corridorkey_adobe_blue.aex",
            "corridorkey.exe",
            "corridorkey_host_plugin_runtime_server.exe"
        )) {
        Assert-PathExists -Path (Join-Path $win64Dir $requiredFile) `
            -Message "Installed Adobe payload is missing discoverable file: $requiredFile"
    }

    $retiredEffectPath = Join-Path $win64Dir "corridorkey_adobe.aex"
    if (Test-Path -LiteralPath $retiredEffectPath) {
        throw "Installed Adobe payload still contains retired single effect binary: $retiredEffectPath"
    }
}

$packageRoot = Resolve-FullPath -Path $PackagePath
$payloadDir = Join-Path $packageRoot "Adobe\Common\Plug-ins\7.0\MediaCore\CorridorKey"
Assert-PathExists -Path $payloadDir -Message "Missing Adobe package payload directory: $payloadDir"

if ([string]::IsNullOrWhiteSpace($CommonPluginInstallPath)) {
    $CommonPluginInstallPath = Get-AdobeCommonPluginInstallPath
}
$commonPluginRoot = Resolve-FullPath -Path $CommonPluginInstallPath
Assert-CommonPluginPathAllowed -Path $commonPluginRoot -AllowSystemPluginPath:$AllowSystemPluginPath.IsPresent

$targetPath = Assert-SafeAdobeInstallDestination `
    -Destination (Join-Path $commonPluginRoot "CorridorKey") `
    -Root $commonPluginRoot

if ($Mode -eq "clean" -and (Test-Path -LiteralPath $targetPath)) {
    Remove-Item -LiteralPath $targetPath -Recurse -Force
}

if ($Mode -eq "upgrade" -and $AllowSystemPluginPath.IsPresent -and -not (Test-Path -LiteralPath $targetPath)) {
    throw "Upgrade smoke against a system Adobe path requires an existing CorridorKey installation."
}

$seededRetiredEffect = $false
if ($Mode -eq "upgrade" -and -not $AllowSystemPluginPath.IsPresent) {
    New-StaleUpgradePayload -TargetPath $targetPath
    $seededRetiredEffect = $true
}

[System.IO.Directory]::CreateDirectory($commonPluginRoot) | Out-Null
if (Test-Path -LiteralPath $targetPath) {
    Remove-Item -LiteralPath $targetPath -Recurse -Force
}
Copy-Item -LiteralPath $payloadDir -Destination $targetPath -Recurse -Force

Assert-InstalledAdobePayload -TargetPath $targetPath

if ([string]::IsNullOrWhiteSpace($ReportPath)) {
    $ReportPath = Join-Path $packageRoot "adobe_install_smoke_$Mode.json"
}

$report = [ordered]@{
    mode = $Mode
    package_path = $packageRoot
    common_plugin_install_path = $commonPluginRoot
    target_path = $targetPath
    validation_passed = $true
    retired_effect_removed = if ($Mode -eq "upgrade") { $seededRetiredEffect } else { $false }
    host_discovery = [ordered]@{
        after_effects = "adobe_common_mediacore_payload_present"
        premiere = "adobe_common_mediacore_payload_present"
        host_restart_required = $true
    }
    effects = @(
        [ordered]@{
            component = "green"
            plugin_binary = Join-Path $targetPath "Contents\Win64\corridorkey_adobe_green.aex"
            match_name = "com.corridorkey.effect"
        },
        [ordered]@{
            component = "blue"
            plugin_binary = Join-Path $targetPath "Contents\Win64\corridorkey_adobe_blue.aex"
            match_name = "com.corridorkey.effect.blue"
        }
    )
}

$reportDir = Split-Path -Parent $ReportPath
if (-not [string]::IsNullOrWhiteSpace($reportDir)) {
    [System.IO.Directory]::CreateDirectory($reportDir) | Out-Null
}
$report | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $ReportPath -Encoding UTF8
Write-Host "[PASS] Adobe $Mode install smoke report: $ReportPath" -ForegroundColor Green
