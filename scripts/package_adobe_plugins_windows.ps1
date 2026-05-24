param(
    [string]$Version = "",
    [string]$BuildDir = "",
    [string]$OrtRoot = "",
    [string]$ModelsDir = "",
    [string]$OutputDir = "",
    [string]$ArtifactManifestPath = "",
    [string]$ReleaseSuffix = "RTX",
    [ValidateSet("online", "offline")]
    [string]$Flavor = "online",
    [ValidateSet("windows-rtx", "windows-universal")]
    [string]$ModelProfile = "windows-rtx",
    [string]$DisplayVersionLabel = "",
    [switch]$Skip2048,
    [switch]$AllowUncertifiedTensorRtContexts
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot "windows_runtime_helpers.ps1")

function Assert-FileExists {
    param([string]$Path, [string]$Message)
    if (-not (Test-Path $Path)) {
        throw $Message
    }
}

function Resolve-OrtDllPath {
    param([string]$Root, [string]$Name)

    foreach ($candidate in @(
            (Join-Path $Root $Name),
            (Join-Path (Join-Path $Root "bin") $Name),
            (Join-Path (Join-Path $Root "lib") $Name)
        )) {
        if (Test-Path $candidate) {
            return $candidate
        }
    }

    return $null
}

function Copy-OrtDll {
    param([string]$Root, [string]$Name, [string]$DestinationDir)

    $resolved = Resolve-OrtDllPath -Root $Root -Name $Name
    if ($null -eq $resolved) {
        throw "Required runtime DLL not found: $Name (searched under $Root)."
    }
    Copy-Item $resolved $DestinationDir -Force
}

function Copy-OrtDllIfPresent {
    param([string]$Root, [string]$Name, [string]$DestinationDir)

    $resolved = Resolve-OrtDllPath -Root $Root -Name $Name
    if ($null -eq $resolved) {
        return $false
    }
    Copy-Item $resolved $DestinationDir -Force
    return $true
}

function Resolve-OrtDllByPattern {
    param([string]$Root, [string]$Pattern)

    $matches = @()
    foreach ($searchRoot in @($Root, (Join-Path $Root "bin"), (Join-Path $Root "lib"))) {
        if (Test-Path $searchRoot) {
            $matches += Get-ChildItem -Path $searchRoot -Filter $Pattern -File -ErrorAction SilentlyContinue
        }
    }

    return $matches | Sort-Object -Property Name -Descending | Select-Object -First 1
}

function Assert-SafeAdobePackageOutputPath {
    param(
        [string]$Path,
        [string]$RepoRoot
    )

    if ([string]::IsNullOrWhiteSpace($Path)) {
        throw "Adobe package output directory must not be empty."
    }

    $resolvedPath = [System.IO.Path]::GetFullPath($Path)
    if (-not [System.IO.Path]::IsPathRooted($Path)) {
        $resolvedPath = [System.IO.Path]::GetFullPath((Join-Path $RepoRoot $Path))
    }
    $resolvedRepoRoot = [System.IO.Path]::GetFullPath($RepoRoot)
    $resolvedDistRoot = [System.IO.Path]::GetFullPath((Join-Path $resolvedRepoRoot "dist"))
    $pathSeparators = [char[]]@([System.IO.Path]::DirectorySeparatorChar, [System.IO.Path]::AltDirectorySeparatorChar)
    $normalizedPath = $resolvedPath.TrimEnd($pathSeparators)
    $normalizedRepoRoot = $resolvedRepoRoot.TrimEnd($pathSeparators)
    $normalizedDistRoot = $resolvedDistRoot.TrimEnd($pathSeparators)
    $pathRoot = [System.IO.Path]::GetPathRoot($resolvedPath).TrimEnd($pathSeparators)

    if ($normalizedPath -eq $pathRoot -or
        $normalizedPath -eq $normalizedRepoRoot -or
        $normalizedPath -eq $normalizedDistRoot) {
        throw "Adobe package output directory is too broad for recursive cleanup: $resolvedPath"
    }

    $distPrefix = $normalizedDistRoot + [System.IO.Path]::DirectorySeparatorChar
    if (-not $normalizedPath.StartsWith($distPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Adobe package output directory must be under $normalizedDistRoot; found $resolvedPath"
    }

    return $resolvedPath
}

function Copy-OrtDllByPattern {
    param([string]$Root, [string]$Pattern, [string]$DestinationDir)

    $resolved = Resolve-OrtDllByPattern -Root $Root -Pattern $Pattern
    if ($null -eq $resolved) {
        throw "Required runtime DLL not found matching pattern '$Pattern' (searched under $Root)."
    }
    Copy-Item $resolved.FullName $DestinationDir -Force
    return $resolved.Name
}

function Copy-RuntimeSidecarDependencies {
    param(
        [string]$BuildDir,
        [string]$OrtRoot,
        [string]$DestinationDir,
        [switch]$RequireRtxProviders
    )

    $runtimeServerDir = Join-Path $BuildDir "src\app"
    $runtimeServerBinary = Join-Path $runtimeServerDir "corridorkey_host_plugin_runtime_server.exe"
    Assert-FileExists -Path $runtimeServerBinary `
        -Message "Runtime server binary not found at $runtimeServerBinary."

    Copy-Item $runtimeServerBinary $DestinationDir -Force
    Copy-Item (Join-Path $BuildDir "src\cli\corridorkey.exe") $DestinationDir -Force

    foreach ($runtimeDll in Get-ChildItem -Path $runtimeServerDir -Filter "*.dll" -File -ErrorAction SilentlyContinue) {
        Copy-Item $runtimeDll.FullName $DestinationDir -Force
    }

    Copy-OrtDll -Root $OrtRoot -Name "onnxruntime.dll" -DestinationDir $DestinationDir
    Copy-OrtDll -Root $OrtRoot -Name "onnxruntime_providers_shared.dll" -DestinationDir $DestinationDir
    Copy-OrtDllIfPresent -Root $OrtRoot -Name "DirectML.dll" -DestinationDir $DestinationDir | Out-Null
    Copy-OrtDllIfPresent -Root $OrtRoot -Name "onnxruntime_providers_dml.dll" -DestinationDir $DestinationDir | Out-Null
    Copy-OrtDllIfPresent -Root $OrtRoot -Name "onnxruntime_providers_winml.dll" -DestinationDir $DestinationDir | Out-Null
    Copy-OrtDllIfPresent -Root $OrtRoot -Name "onnxruntime_providers_openvino.dll" -DestinationDir $DestinationDir | Out-Null

    $tensorrtProvider = Resolve-OrtDllPath -Root $OrtRoot -Name "onnxruntime_providers_nv_tensorrt_rtx.dll"
    if ($null -eq $tensorrtProvider) {
        $tensorrtProvider = Resolve-OrtDllPath -Root $OrtRoot -Name "onnxruntime_providers_nvtensorrtrtx.dll"
    }
    if ($RequireRtxProviders.IsPresent -and $null -eq $tensorrtProvider) {
        throw "Required RTX ONNX Runtime TensorRT provider DLL not found under $OrtRoot."
    }
    if ($null -ne $tensorrtProvider) {
        Copy-Item $tensorrtProvider $DestinationDir -Force
        Copy-OrtDllByPattern -Root $OrtRoot -Pattern "tensorrt_onnxparser_rtx_*.dll" `
            -DestinationDir $DestinationDir | Out-Null
        Copy-OrtDllByPattern -Root $OrtRoot -Pattern "tensorrt_rtx_*.dll" `
            -DestinationDir $DestinationDir | Out-Null
    }
    $cudaProvider = Resolve-OrtDllPath -Root $OrtRoot -Name "onnxruntime_providers_cuda.dll"
    if ($null -ne $cudaProvider) {
        Copy-Item $cudaProvider $DestinationDir -Force
    }

    if ($null -ne $tensorrtProvider -or $null -ne $cudaProvider) {
        $cudartCandidates = @()
        foreach ($candidateDir in @($OrtRoot, (Join-Path $OrtRoot "bin"), (Join-Path $OrtRoot "lib"))) {
            if (Test-Path $candidateDir) {
                $cudartCandidates += Get-ChildItem -Path $candidateDir -Filter "cudart64_*.dll" -File -ErrorAction SilentlyContinue
            }
        }
        if ($cudartCandidates.Count -eq 0) {
            throw "Required CUDA runtime DLL not found (cudart64_*.dll)."
        }
        foreach ($candidate in $cudartCandidates) {
            Copy-Item $candidate.FullName $DestinationDir -Force
        }

        $cudaContract = Get-CorridorKeyWindowsRtxBuildContract
        $cudaVersion = $cudaContract.required_cuda_version
        $cudaRoot = $env:CUDA_PATH
        $cudaVersionEnvVar = "CUDA_PATH_V" + ($cudaVersion -replace '\.', '_')
        if ([string]::IsNullOrWhiteSpace($cudaRoot)) {
            $cudaRoot = [System.Environment]::GetEnvironmentVariable($cudaVersionEnvVar)
        }
        if ([string]::IsNullOrWhiteSpace($cudaRoot)) {
            $cudaRoot = "C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v$cudaVersion"
        }
        $cudaBin = Join-Path $cudaRoot "bin"
        foreach ($nppName in @("nppc64_12.dll", "nppial64_12.dll", "nppidei64_12.dll", "nppif64_12.dll", "nppig64_12.dll")) {
            $nppPath = Join-Path $cudaBin $nppName
            if (-not (Test-Path $nppPath)) {
                throw "Required CUDA NPP DLL not found: $nppPath."
            }
            Copy-Item $nppPath $DestinationDir -Force
        }
    }
}

function Get-AdobeTargetModels {
    param(
        [ValidateSet("windows-rtx", "windows-universal")]
        [string]$ModelProfile,
        [switch]$Skip2048
    )

    $targetModels = switch ($ModelProfile) {
        "windows-rtx" { @(Get-CorridorKeyWindowsRtxPromotedModelList -Variant all) }
        "windows-universal" { @(Get-CorridorKeyOfxBundleTargetModels -ModelProfile "windows-universal") }
    }
    if ($Skip2048.IsPresent) {
        return @($targetModels | Where-Object { $_ -ne "corridorkey_fp16_2048.onnx" })
    }
    return @($targetModels)
}

function Write-InstallScript {
    param([string]$Path)

    $content = @'
param(
    [string]$CommonPluginInstallPath = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Get-AdobeCommonPluginPath {
    foreach ($registryRoot in @("HKLM:\SOFTWARE\Adobe\After Effects", "HKLM:\SOFTWARE\WOW6432Node\Adobe\After Effects")) {
        if (-not (Test-Path $registryRoot)) {
            continue
        }
        $candidate = Get-ChildItem -Path $registryRoot -ErrorAction SilentlyContinue |
            Sort-Object -Property PSChildName -Descending |
            ForEach-Object {
                try {
                    $value = $_.GetValue("CommonPluginInstallPath", $null, "DoNotExpandEnvironmentNames")
                    if ($null -ne $value) { return ($value | Out-String).Trim() }
                } catch {
                }
            } |
            Where-Object { -not [string]::IsNullOrWhiteSpace($_) } |
            Select-Object -First 1
        if (-not [string]::IsNullOrWhiteSpace($candidate)) {
            return $candidate
        }
    }
    return Join-Path $env:ProgramFiles "Adobe\Common\Plug-ins\7.0\MediaCore"
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

if ([string]::IsNullOrWhiteSpace($CommonPluginInstallPath)) {
    $CommonPluginInstallPath = Get-AdobeCommonPluginPath
}

$source = Join-Path $PSScriptRoot "Adobe\Common\Plug-ins\7.0\MediaCore\CorridorKey"
$destination = Assert-SafeAdobeInstallDestination `
    -Destination (Join-Path $CommonPluginInstallPath "CorridorKey") `
    -Root $CommonPluginInstallPath

if (-not (Test-Path $source)) {
    throw "Adobe package payload not found: $source"
}

New-Item -ItemType Directory -Path $CommonPluginInstallPath -Force | Out-Null
if (Test-Path -LiteralPath $destination) {
    Remove-Item -LiteralPath $destination -Recurse -Force
}
Copy-Item -LiteralPath $source -Destination $destination -Recurse -Force
Write-Host "CorridorKey Adobe plugin installed to: $destination"
'@

    Set-Content -Path $Path -Value $content -Encoding ASCII
}

function Write-ReleaseReadme {
    param(
        [string]$Path,
        [string]$Version,
        [string]$DisplayVersionLabel,
        [string]$ReleaseLabel,
        [string]$ReleaseBasename,
        [string]$InstallerFileName,
        [string]$InstallerFlavor
    )

    $packFlowText = if ($InstallerFlavor -eq "online") {
        "- The online installer downloads the selected model packs from the distribution manifest with SHA256 verification. The available pack choices are Green only, Blue only, and Recommended (Green + Blue)."
    } else {
        "- The offline installer bundles every distribution-manifest model pack and installs Green + Blue as a fixed complete payload."
    }

@"
CorridorKey Adobe $DisplayVersionLabel - $ReleaseLabel
===========================================

Files in this release:
- Adobe\Common\Plug-ins\7.0\MediaCore\CorridorKey: installer payload for Adobe's shared MediaCore plug-in location
- ${InstallerFileName}: administrator $InstallerFlavor installer for the Adobe payload
- install_adobe_plugin.ps1: administrator install helper for the staged payload
- adobe_package_inventory.json: package inventory
- adobe_package_validation.json: packaging-time validation report
- doctor_report.json: packaged runtime diagnostic output when available

Recommended install path:
1. Run $InstallerFileName as Administrator.
2. Restart After Effects and Premiere Pro.

Installer behavior:
- The payload is staged for Adobe's shared Common Plug-ins MediaCore location.
- The runtime server and app-local DLLs are staged beside the Adobe Green and Blue .aex files; models and runtime resources resolve under Contents\Resources.
$packFlowText
- Missing packaged models are listed in the generated reports; present but invalid model artifacts block validation.

Manual fallback path:
1. Run PowerShell as Administrator.
2. Run .\install_adobe_plugin.ps1 from this folder.
3. Restart the Adobe host before testing the effect.
"@ | Set-Content -Path $Path -Encoding ASCII
}

$Version = Initialize-CorridorKeyVersion -RepoRoot $repoRoot -Version $Version
if ([string]::IsNullOrWhiteSpace($BuildDir)) {
    $BuildDir = Join-Path $repoRoot "build\release"
}
if ([string]::IsNullOrWhiteSpace($ModelsDir)) {
    $ModelsDir = Join-Path $repoRoot "models"
}
$preferredTrack = Get-CorridorKeyWindowsTrackFromReleaseSuffix -ReleaseSuffix $ReleaseSuffix -DefaultTrack "rtx"
$OrtRoot = Resolve-CorridorKeyWindowsOrtRoot -RepoRoot $repoRoot -ExplicitRoot $OrtRoot -PreferredTrack $preferredTrack
$releaseLabel = Get-CorridorKeyWindowsReleaseLabelFromSuffix -ReleaseSuffix $ReleaseSuffix
$artifactVersionTag = if ([string]::IsNullOrWhiteSpace($DisplayVersionLabel)) { $Version } else { $DisplayVersionLabel }
$normalizedSuffix = if ([string]::IsNullOrWhiteSpace($ReleaseSuffix)) { "" } else { "_" + $ReleaseSuffix.Trim("_") }
$releaseBasename = "CorridorKey_Adobe_v${artifactVersionTag}_Windows${normalizedSuffix}"
$installerFlavor = $Flavor.ToLowerInvariant()
$installerBaseName = "${releaseBasename}_${installerFlavor}_Setup"
if ([string]::IsNullOrWhiteSpace($OutputDir)) {
    $OutputDir = Join-Path $repoRoot ("dist\" + $releaseBasename)
}
$OutputDir = Assert-SafeAdobePackageOutputPath -Path $OutputDir -RepoRoot $repoRoot
$installerPath = Join-Path $repoRoot ("dist\" + $installerBaseName + ".exe")

$payloadDir = Join-Path $OutputDir "Adobe\Common\Plug-ins\7.0\MediaCore\CorridorKey"
$win64Dir = Join-Path $payloadDir "Contents\Win64"
$modelsPayloadDir = Join-Path $payloadDir "Contents\Resources\models"
$torchTrtRuntimeDir = Join-Path $payloadDir "Contents\Resources\torchtrt-runtime\bin"
$greenPluginBinary = Join-Path $BuildDir "src\plugins\adobe\corridorkey_adobe_green.aex"
$bluePluginBinary = Join-Path $BuildDir "src\plugins\adobe\corridorkey_adobe_blue.aex"
$modelInventoryPath = Join-Path $payloadDir "model_inventory.json"
$adobeInventoryPath = Join-Path $OutputDir "adobe_package_inventory.json"
$artifactManifestOutputPath = Join-Path $payloadDir "artifact_manifest.json"
$greenEffectDisplayName = "CorridorKey Green"
$blueEffectDisplayName = "CorridorKey Blue"
if (-not [string]::IsNullOrWhiteSpace($DisplayVersionLabel)) {
    $greenEffectDisplayName = "CorridorKey Green v$DisplayVersionLabel"
    $blueEffectDisplayName = "CorridorKey Blue v$DisplayVersionLabel"
}

Write-Host "[1/5] Preparing Adobe package directory..." -ForegroundColor Cyan
if (Test-Path $OutputDir) {
    Remove-Item -LiteralPath $OutputDir -Recurse -Force
}
if (Test-Path $installerPath) {
    Remove-Item -LiteralPath $installerPath -Force
}
New-Item -ItemType Directory -Path $payloadDir -Force | Out-Null
New-Item -ItemType Directory -Path $win64Dir -Force | Out-Null
New-Item -ItemType Directory -Path $modelsPayloadDir -Force | Out-Null
New-Item -ItemType Directory -Path $torchTrtRuntimeDir -Force | Out-Null

Assert-FileExists -Path $greenPluginBinary -Message "Adobe Green plugin binary not found at $greenPluginBinary. Run scripts\windows.ps1 -Task build -Preset release -EnableAdobePlugin first."
Assert-FileExists -Path $bluePluginBinary -Message "Adobe Blue plugin binary not found at $bluePluginBinary. Run scripts\windows.ps1 -Task build -Preset release -EnableAdobePlugin first."
Assert-FileExists -Path (Join-Path $BuildDir "src\cli\corridorkey.exe") -Message "CLI binary not found in $BuildDir."
Assert-FileExists -Path (Join-Path $BuildDir "src\app\corridorkey_host_plugin_runtime_server.exe") -Message "Runtime server binary not found in $BuildDir."

Write-Host "[2/5] Staging Adobe plugin and runtime payload..." -ForegroundColor Cyan
Copy-Item $greenPluginBinary $win64Dir -Force
Copy-Item $bluePluginBinary $win64Dir -Force
Copy-RuntimeSidecarDependencies -BuildDir $BuildDir -OrtRoot $OrtRoot -DestinationDir $win64Dir -RequireRtxProviders:($ModelProfile -eq "windows-rtx")

$cmakeTorchTrtWrapper = Join-Path $BuildDir "CorridorKey.ofx.bundle\Contents\Resources\torchtrt-runtime\bin\corridorkey_torchtrt.dll"
if (Test-Path $cmakeTorchTrtWrapper) {
    Copy-Item $cmakeTorchTrtWrapper $torchTrtRuntimeDir -Force
}

Write-Host "[3/5] Staging model inventory..." -ForegroundColor Cyan
$targetModels = Get-AdobeTargetModels -ModelProfile $ModelProfile -Skip2048:$Skip2048.IsPresent
$modelInventory = Get-CorridorKeyModelInventory -ModelsDir $ModelsDir -ExpectedModels $targetModels
$compiledContextModels = @()
$expectedCompiledContextModels = Get-CorridorKeyExpectedCompiledContextModels `
    -PresentModels $modelInventory.present_models `
    -ModelProfile $ModelProfile

foreach ($model in $modelInventory.present_models) {
    Copy-Item (Join-Path $ModelsDir $model) $modelsPayloadDir -Force
    $compiledContextName = ([System.IO.Path]::GetFileNameWithoutExtension($model)) + "_ctx.onnx"
    $compiledContextPath = Join-Path $ModelsDir $compiledContextName
    if (Test-Path $compiledContextPath) {
        Copy-Item $compiledContextPath $modelsPayloadDir -Force
        $compiledContextModels += $compiledContextName
    }
}

$missingCompiledContextModels = @(
    $expectedCompiledContextModels |
        Where-Object { $compiledContextModels -notcontains $_ }
)

$certificationContractIssues = @()
$certificationContractComplete = $false
$profileContract = Get-CorridorKeyModelProfileContract -ModelProfile $ModelProfile
$strictCertifiedRtxPackaging = $profileContract.expects_compiled_context_models -and
    (-not $AllowUncertifiedTensorRtContexts.IsPresent)
if ($strictCertifiedRtxPackaging) {
    $certifiedExpectedModels = @(
        $modelInventory.present_models |
            Where-Object { $_ -match '^corridorkey_fp16_[0-9]+\.onnx$' }
    )
    $certifiedExpectedContexts = Get-CorridorKeyExpectedCompiledContextModels `
        -PresentModels $certifiedExpectedModels `
        -ModelProfile $ModelProfile
    if ([string]::IsNullOrWhiteSpace($ArtifactManifestPath)) {
        $ArtifactManifestPath = Get-CorridorKeyWindowsRtxArtifactManifestPath -ModelsDir $ModelsDir
    }
    Assert-CorridorKeyWindowsRtxArtifactManifestHealthy `
        -ArtifactsDir $ModelsDir `
        -ExpectedModels $certifiedExpectedModels `
        -ExpectedCompiledContextModels $certifiedExpectedContexts `
        -ArtifactManifestPath $ArtifactManifestPath `
        -Label "$ModelProfile Adobe package source" | Out-Null
    Copy-Item $ArtifactManifestPath $artifactManifestOutputPath -Force
    Assert-CorridorKeyWindowsRtxArtifactManifestHealthy `
        -ArtifactsDir $modelsPayloadDir `
        -ExpectedModels $certifiedExpectedModels `
        -ExpectedCompiledContextModels $certifiedExpectedContexts `
        -ArtifactManifestPath $artifactManifestOutputPath `
        -Label "$ModelProfile staged Adobe package" | Out-Null
    $certificationContractComplete = $true
} elseif ($profileContract.expects_compiled_context_models) {
    $certificationContractIssues += "RTX packaging did not use a certified artifact manifest."
}

$inventoryPayload = [ordered]@{
    package_type = "adobe_mediacore_package"
    model_profile = $profileContract.model_profile
    bundle_track = $profileContract.bundle_track
    release_label = $profileContract.release_label
    optimization_profile_id = $profileContract.optimization_profile_id
    optimization_profile_label = $profileContract.optimization_profile_label
    backend_intent = $profileContract.backend_intent
    fallback_policy = $profileContract.fallback_policy
    warmup_policy = $profileContract.warmup_policy
    certification_tier = $profileContract.certification_tier
    unrestricted_quality_attempt = $profileContract.unrestricted_quality_attempt
    models_dir = [System.IO.Path]::GetFullPath($ModelsDir)
    expected_models = @($modelInventory.expected_models)
    present_models = @($modelInventory.present_models)
    missing_models = @($modelInventory.missing_models)
    present_count = $modelInventory.present_count
    missing_count = $modelInventory.missing_count
    compiled_context_models = @($compiledContextModels)
    expected_compiled_context_models = @($expectedCompiledContextModels)
    missing_compiled_context_models = @($missingCompiledContextModels)
    compiled_context_complete = @($missingCompiledContextModels).Count -eq 0
    certification_manifest_present = $strictCertifiedRtxPackaging
    certification_contract_complete = $certificationContractComplete
    certification_contract_issues = @($certificationContractIssues)
}
Write-CorridorKeyJsonFile -Path $modelInventoryPath -Payload $inventoryPayload

$adobePackageInventory = [ordered]@{
    package_type = "adobe_mediacore_package"
    package_path = [System.IO.Path]::GetFullPath($OutputDir)
    payload_path = [System.IO.Path]::GetFullPath($payloadDir)
    install_scope = "adobe_common_mediacore"
    install_payload_relative_path = "Adobe\Common\Plug-ins\7.0\MediaCore\CorridorKey"
    effects = @(
        [ordered]@{
            name = $greenEffectDisplayName
            match_name = "com.corridorkey.effect"
            category = "CorridorKey"
            component = "green"
            plugin_binary = "Contents\Win64\corridorkey_adobe_green.aex"
            pipl_capabilities = @("PF_OutFlag2_SUPPORTS_SMART_RENDER", "PF_OutFlag2_FLOAT_COLOR_AWARE")
        },
        [ordered]@{
            name = $blueEffectDisplayName
            match_name = "com.corridorkey.effect.blue"
            category = "CorridorKey"
            component = "blue"
            plugin_binary = "Contents\Win64\corridorkey_adobe_blue.aex"
            pipl_capabilities = @("PF_OutFlag2_SUPPORTS_SMART_RENDER", "PF_OutFlag2_FLOAT_COLOR_AWARE")
        }
    )
    runtime = [ordered]@{
        cli_binary = "Contents\Win64\corridorkey.exe"
        host_plugin_runtime_server = "Contents\Win64\corridorkey_host_plugin_runtime_server.exe"
        models_root = "Contents\Resources\models"
        torchtrt_runtime = "Contents\Resources\torchtrt-runtime\bin"
    }
    models = $inventoryPayload
}
Write-CorridorKeyJsonFile -Path $adobeInventoryPath -Payload $adobePackageInventory

Write-InstallScript -Path (Join-Path $OutputDir "install_adobe_plugin.ps1")
Write-ReleaseReadme -Path (Join-Path $OutputDir "README.txt") `
    -Version $Version `
    -DisplayVersionLabel $DisplayVersionLabel `
    -ReleaseLabel $releaseLabel `
    -ReleaseBasename $releaseBasename `
    -InstallerFileName ([System.IO.Path]::GetFileName($installerPath)) `
    -InstallerFlavor $installerFlavor

Write-Host "[4/5] Validating Adobe package..." -ForegroundColor Cyan
$validateArgs = @(
    "-PackagePath", $OutputDir,
    "-ExpectedDisplayVersionLabel", $DisplayVersionLabel
)
& powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot "validate_adobe_package_win.ps1") @validateArgs
if ($LASTEXITCODE -ne 0) {
    throw "Adobe package validation failed."
}

Write-Host "[5/5] Building Adobe installer..." -ForegroundColor Cyan
$innoArgs = @(
    "-Flavor", $installerFlavor,
    "-Version", $Version,
    "-DisplayVersionLabel", $artifactVersionTag,
    "-PluginPayloadDir", $payloadDir,
    "-InstallerSurface", "adobe",
    "-OutputBaseFilename", $installerBaseName
)
if ($installerFlavor -eq "offline") {
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot "installer\stage_offline_payload.ps1")
    if ($LASTEXITCODE -ne 0) {
        throw "Offline payload staging failed."
    }
    $offlineRoot = Join-Path $repoRoot "dist\_offline_payload"
    $innoArgs += @("-ModelPayloadDir", $offlineRoot)
}

& powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot "installer\build_installer.ps1") @innoArgs
if ($LASTEXITCODE -ne 0) {
    throw "Inno Setup installer build failed for Adobe flavor '$installerFlavor'."
}

Write-Host "Adobe package ready at: $OutputDir" -ForegroundColor Green
Write-Host "Adobe installer ready at: $installerPath" -ForegroundColor Green
