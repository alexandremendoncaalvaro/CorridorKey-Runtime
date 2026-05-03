param(
    [string]$BundlePath = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot "windows_runtime_helpers.ps1")
if ([string]::IsNullOrWhiteSpace($BundlePath)) {
    $BundlePath = Join-Path $repoRoot "dist\CorridorKey.ofx.bundle"
}

function Test-CorridorKeyJsonProperty {
    param(
        [object]$Object,
        [string]$Name
    )

    if ($null -eq $Object) {
        return $false
    }

    return $Object.PSObject.Properties.Match($Name).Count -gt 0
}

function Resolve-CorridorKeyDumpbinPath {
    $dumpbinCommand = Get-Command "dumpbin.exe" -ErrorAction SilentlyContinue
    if ($null -ne $dumpbinCommand) {
        return $dumpbinCommand.Source
    }

    $toolRoots = @()
    if (-not [string]::IsNullOrWhiteSpace($env:VSINSTALLDIR)) {
        $toolRoots += Join-Path $env:VSINSTALLDIR "VC\Tools\MSVC"
    }

    $vswhereCommand = Get-Command "vswhere.exe" -ErrorAction SilentlyContinue
    if ($null -ne $vswhereCommand) {
        $installationPath = & $vswhereCommand.Source -latest -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2>$null
        if ($LASTEXITCODE -eq 0 -and -not [string]::IsNullOrWhiteSpace($installationPath)) {
            $toolRoots += Join-Path ($installationPath | Out-String).Trim() "VC\Tools\MSVC"
        }
    }

    foreach ($installationRoot in @(
            "C:\Program Files\Microsoft Visual Studio\2022\Community",
            "C:\Program Files\Microsoft Visual Studio\2022\BuildTools",
            "C:\Program Files\Microsoft Visual Studio\2022\Professional",
            "C:\Program Files\Microsoft Visual Studio\2022\Enterprise"
        )) {
        $toolRoots += Join-Path $installationRoot "VC\Tools\MSVC"
    }

    foreach ($toolRoot in ($toolRoots | Select-Object -Unique)) {
        if (-not (Test-Path $toolRoot)) {
            continue
        }
        $candidate = Get-ChildItem -Path $toolRoot -Directory | Sort-Object Name -Descending |
            ForEach-Object { Join-Path $_.FullName "bin\Hostx64\x64\dumpbin.exe" } |
            Where-Object { Test-Path $_ } | Select-Object -First 1
        if ($null -ne $candidate) {
            return $candidate
        }
    }

    return $null
}

function Get-CorridorKeyPeImports {
    param(
        [string]$DumpbinPath,
        [string]$ImagePath,
        [switch]$IncludeDelayLoaded
    )

    $output = & $DumpbinPath /DEPENDENTS $ImagePath 2>$null
    if ($LASTEXITCODE -ne 0) {
        return @()
    }

    # `dumpbin /DEPENDENTS` emits two DLL sections back-to-back when an
    # image uses delay-loading: first "Image has the following dependencies"
    # (normal imports the OS resolves at process start) and then "Image has
    # the following delay load dependencies" (resolved on first call by the
    # delay-load helper). Strategy C ships corridorkey_torchtrt.dll in the
    # blue model pack only and links it /DELAYLOAD; the base RTX bundle
    # must NOT contain it. Treating delay-load entries as required would
    # block the bundle on a DLL we deliberately omitted, so this parser
    # keeps the two sections separate and only the normal imports drive
    # the bundle-completeness check (callers opt into delay-loaded scanning
    # via -IncludeDelayLoaded).
    $imports = [System.Collections.Generic.List[string]]::new()
    $inDelayLoadSection = $false
    foreach ($rawLine in $output) {
        $line = $rawLine.Trim()
        if ($line -match '^Image has the following delay load dependencies:?$') {
            $inDelayLoadSection = $true
            continue
        }
        if ($line -match '^Image has the following dependencies:?$') {
            $inDelayLoadSection = $false
            continue
        }
        if ($line -notmatch '^[A-Za-z0-9_.-]+\.dll$') {
            continue
        }
        if ($inDelayLoadSection -and -not $IncludeDelayLoaded.IsPresent) {
            continue
        }
        [void]$imports.Add($line)
    }

    return $imports | Select-Object -Unique
}

# System DLLs that are always resolvable from Windows and must not be bundled.
# Networking / UI deps that cpr/libcurl pulls in (wintrust, crypt32, bcrypt) plus
# the Universal CRT, which Microsoft requires to come from the OS image and
# explicitly forbids redistributing app-local on modern Windows.
$script:CorridorKeySystemDllAllowlist = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
foreach ($dll in @(
        "KERNEL32.dll","KERNELBASE.dll","USER32.dll","GDI32.dll","GDI32FULL.dll",
        "ADVAPI32.dll","SHELL32.dll","SHLWAPI.dll","OLE32.dll","OLEAUT32.dll",
        "COMBASE.dll","RPCRT4.dll","WS2_32.dll","WLDAP32.dll","WININET.dll",
        "WINHTTP.dll","CRYPT32.dll","BCRYPT.dll","BCRYPTPRIMITIVES.dll",
        "SECUR32.dll","NCRYPT.dll","WINTRUST.dll","IPHLPAPI.dll",
        "NTDLL.dll","PSAPI.DLL","COMDLG32.dll","DXGI.dll","SETUPAPI.dll",
        "IMM32.dll","USERENV.dll","POWRPROF.dll",
        "UCRTBASE.dll","MSVCRT.dll",
        "api-ms-win-crt-runtime-l1-1-0.dll","api-ms-win-crt-heap-l1-1-0.dll",
        "api-ms-win-crt-stdio-l1-1-0.dll","api-ms-win-crt-string-l1-1-0.dll",
        "api-ms-win-crt-convert-l1-1-0.dll","api-ms-win-crt-math-l1-1-0.dll",
        "api-ms-win-crt-filesystem-l1-1-0.dll","api-ms-win-crt-environment-l1-1-0.dll",
        "api-ms-win-crt-time-l1-1-0.dll","api-ms-win-crt-locale-l1-1-0.dll",
        "api-ms-win-crt-utility-l1-1-0.dll","api-ms-win-crt-conio-l1-1-0.dll",
        "api-ms-win-crt-process-l1-1-0.dll"
    )) {
    [void]$script:CorridorKeySystemDllAllowlist.Add($dll)
}

# Visual C++ Redistributable DLLs that the OFX bundle ships APP-LOCAL.
# They were previously on the system allowlist (treated as "must not be
# bundled") on the assumption that the default Win32 search order would
# always resolve them from %WINDIR%\System32. Issue #56 disproved that
# assumption: Foundry Nuke calls SetDllDirectory on its own process and
# Microsoft documents
# (https://learn.microsoft.com/en-us/windows/win32/dlls/dynamic-link-library-search-order)
# that this alters the search order INHERITED BY CHILD PROCESSES, placing
# the host install dir ahead of System32. Nuke 17.0v2 ships an older
# MSVCP140 v14.36 that is ABI-incompatible with what TensorRT-RTX / ORT
# build against today, and the spawned sidecar crashed at MSVCP140!0x12f58
# with EXCEPTION_ACCESS_VIOLATION before reaching wWinMain.
#
# The fix follows Microsoft's documented "Install individual redistributable
# files" path
# (https://learn.microsoft.com/en-us/cpp/windows/redistributing-visual-cpp-files):
# CMake's InstallRequiredSystemLibraries module discovers the active
# toolchain's redist set and src/plugins/ofx/CMakeLists.txt copies it into
# the bundle's Contents/Win64/. Win32 search-order step #1 ("the folder from
# which the application loaded") is evaluated BEFORE any SetDllDirectory-
# altered step, so the bundled copy wins regardless of host behavior.
#
# Each entry in this list is therefore EXPECTED inside the bundle, and any
# import resolving to one of them must find the bundle copy — not the
# system copy. Missing-from-bundle is now a release blocker; the allowlist
# above intentionally no longer covers them.
$script:CorridorKeyExpectedBundledRuntimeList = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
foreach ($dll in @(
        "VCRUNTIME140.dll","VCRUNTIME140_1.dll",
        "MSVCP140.dll","MSVCP140_1.dll","MSVCP140_2.dll",
        "MSVCP140_atomic_wait.dll","MSVCP140_codecvt_ids.dll"
    )) {
    [void]$script:CorridorKeyExpectedBundledRuntimeList.Add($dll)
}

function Test-CorridorKeyPeImportsResolvable {
    param(
        [string]$ImagePath,
        [string]$BundleDir,
        [string]$DumpbinPath
    )

    $missing = @()
    $imports = Get-CorridorKeyPeImports -DumpbinPath $DumpbinPath -ImagePath $ImagePath
    foreach ($import in $imports) {
        if ($script:CorridorKeySystemDllAllowlist.Contains($import)) {
            continue
        }
        # api-ms-win-*.dll family is large; match by prefix.
        if ($import -like "api-ms-win-*.dll" -or $import -like "ext-ms-*.dll") {
            continue
        }
        # Visual C++ Redistributable: must be present app-local in the bundle
        # (see CorridorKeyExpectedBundledRuntimeList above for the rationale
        # and the Issue #56 backstory). Falls through to the same bundled-path
        # check below; not finding it surfaces the same "missing" report.
        $bundledPath = Join-Path $BundleDir $import
        if (-not (Test-Path $bundledPath)) {
            $missing += $import
        }
    }
    return ,$missing
}

function Test-CorridorKeyExpectedRuntimeFilesPresent {
    param(
        [string]$BundleDir
    )

    # Bundle layout requires every entry in the expected app-local Visual
    # C++ Redistributable list to exist next to corridorkey_ofx_runtime_server.exe
    # before the bundle is considered releasable. See
    # CorridorKeyExpectedBundledRuntimeList for the per-DLL rationale.
    $missing = @()
    foreach ($name in $script:CorridorKeyExpectedBundledRuntimeList) {
        $candidate = Join-Path $BundleDir $name
        if (-not (Test-Path $candidate)) {
            $missing += $name
        }
    }
    return ,$missing
}

function Get-CorridorKeyInventoryContractIssues {
    param(
        [object]$Inventory,
        [object]$ExpectedContract
    )

    $issues = @()

    $stringFields = @(
        "package_type",
        "model_profile",
        "bundle_track",
        "release_label",
        "optimization_profile_id",
        "optimization_profile_label",
        "backend_intent",
        "fallback_policy",
        "warmup_policy",
        "certification_tier"
    )

    foreach ($field in $stringFields) {
        if (-not (Test-CorridorKeyJsonProperty -Object $Inventory -Name $field)) {
            $issues += "Missing inventory field '$field'."
            continue
        }

        $actualValue = [string]$Inventory.$field
        $expectedValue = [string]$ExpectedContract.$field
        if ($actualValue -ne $expectedValue) {
            $issues += "Inventory field '$field' expected '$expectedValue' but found '$actualValue'."
        }
    }

    foreach ($field in @("expected_compiled_context_models", "compiled_context_models", "missing_compiled_context_models")) {
        if (-not (Test-CorridorKeyJsonProperty -Object $Inventory -Name $field)) {
            $issues += "Missing inventory field '$field'."
            continue
        }

        if ($null -eq $Inventory.$field -or $Inventory.$field -isnot [System.Array]) {
            $issues += "Inventory field '$field' must be an array."
        }
    }

    foreach ($field in @("compiled_context_complete", "unrestricted_quality_attempt")) {
        if (-not (Test-CorridorKeyJsonProperty -Object $Inventory -Name $field)) {
            $issues += "Missing inventory field '$field'."
        }
    }

    if ($ExpectedContract.expects_compiled_context_models) {
        $compiledContextComplete = $false
        if (Test-CorridorKeyJsonProperty -Object $Inventory -Name "compiled_context_complete") {
            $compiledContextComplete = [bool]$Inventory.compiled_context_complete
        }
        if (-not $compiledContextComplete) {
            $issues += "Inventory requires precompiled TensorRT context models, but the packaged set is incomplete."
        }
    }

    return @($issues)
}

Write-Host "Validating OFX bundle: $BundlePath" -ForegroundColor Cyan
Write-Host ""

$bundleDescriptor = [System.IO.Path]::GetFullPath($BundlePath)
$bundleRoot = Split-Path -Parent $bundleDescriptor
$win64Dir = Join-Path $bundleDescriptor "Contents\Win64"
$resourcesDir = Join-Path $bundleDescriptor "Contents\Resources\models"
$modelInventoryPath = Join-Path $bundleDescriptor "model_inventory.json"
$artifactManifestPath = Join-Path $bundleDescriptor "artifact_manifest.json"
$bundleValidationPath = Join-Path $bundleRoot "bundle_validation.json"
$defaultModelProfile = Get-CorridorKeyOfxModelProfileFromReleaseSuffix -ReleaseSuffix (Split-Path $bundleRoot -Leaf)
$expectsUniversalGpuPath = $bundleDescriptor -match 'Universal'
$expectsDirectMlPath = $bundleDescriptor -match 'DirectML'

# Check bundle structure
if (-not (Test-Path $BundlePath)) {
    throw "Bundle directory not found: $BundlePath"
}

if (-not (Test-Path $win64Dir)) {
    throw "Missing Contents\Win64 directory"
}

if (-not (Test-Path $resourcesDir)) {
    throw "Missing Contents\Resources\models directory"
}

Write-Host "[PASS] Bundle directory structure exists" -ForegroundColor Green

# CRITICAL: Check for correct ONNX Runtime DLL name
$onnxDll = Join-Path $win64Dir "onnxruntime.dll"
if (-not (Test-Path $onnxDll)) {
    Write-Host "[FAIL] onnxruntime.dll not found!" -ForegroundColor Red
    throw "ERROR: onnxruntime.dll not found in Win64 directory"
}

Write-Host "[PASS] onnxruntime.dll exists" -ForegroundColor Green

# Check all required DLLs
$requiredDlls = @(
    "onnxruntime.dll",
    "onnxruntime_providers_shared.dll"
)

foreach ($dll in $requiredDlls) {
    $path = Join-Path $win64Dir $dll
    if (-not (Test-Path $path)) {
        Write-Host "[FAIL] Missing required DLL: $dll" -ForegroundColor Red
        throw "Missing required DLL: $dll"
    }
    Write-Host "[PASS] Found $dll" -ForegroundColor Green
}

# Check plugin binary
$plugin = Join-Path $win64Dir "CorridorKey.ofx"
if (-not (Test-Path $plugin)) {
    Write-Host "[FAIL] Plugin binary not found" -ForegroundColor Red
    throw "Plugin binary not found: CorridorKey.ofx"
}

$pluginSize = (Get-Item $plugin).Length
Write-Host "[PASS] Found plugin binary ($([math]::Round($pluginSize / 1MB, 2)) MB)" -ForegroundColor Green

$cliBinary = Join-Path $win64Dir "corridorkey.exe"
if (-not (Test-Path $cliBinary)) {
    Write-Host "[FAIL] CLI binary not found" -ForegroundColor Red
    throw "CLI binary not found: corridorkey.exe"
}

$runtimeServer = Join-Path $win64Dir "corridorkey_ofx_runtime_server.exe"
if (-not (Test-Path $runtimeServer)) {
    Write-Host "[FAIL] Runtime server binary not found" -ForegroundColor Red
    throw "Runtime server binary not found: corridorkey_ofx_runtime_server.exe"
}

$directmlDll = Join-Path $win64Dir "DirectML.dll"
if (Test-Path $directmlDll) {
    Write-Host "[PASS] Found DirectML.dll" -ForegroundColor Green
} elseif ($expectsDirectMlPath) {
    Write-Host "[FAIL] DirectML.dll not found in DirectML bundle" -ForegroundColor Red
    throw "DirectML.dll is required for the DirectML bundle."
} else {
    Write-Host "[INFO] DirectML.dll not found (RTX bundle)" -ForegroundColor Cyan
}

$cliBinarySize = (Get-Item $cliBinary).Length
Write-Host "[PASS] Found CLI binary ($([math]::Round($cliBinarySize / 1MB, 2)) MB)" -ForegroundColor Green

$runtimeServerSize = (Get-Item $runtimeServer).Length
Write-Host "[PASS] Found runtime server binary ($([math]::Round($runtimeServerSize / 1MB, 2)) MB)" -ForegroundColor Green

# Regression guard: any import beyond the system allowlist must be packaged
# in Contents\Win64\. Catches the failure mode where a new target_link_libraries
# (e.g. cpr pulling libcurl+OpenSSL for the update check) lands without the
# corresponding POST_BUILD copy, shipping a bundle that cannot load in Resolve.
$dumpbinPath = Resolve-CorridorKeyDumpbinPath
if ($null -eq $dumpbinPath) {
    Write-Host "[WARN] dumpbin.exe not found; skipping PE import scan. Run from a VS developer shell to enable." -ForegroundColor Yellow
} else {
    $importScanTargets = @($plugin, $cliBinary, $runtimeServer)
    $importScanFailures = @()
    foreach ($imageTarget in $importScanTargets) {
        $missing = Test-CorridorKeyPeImportsResolvable -ImagePath $imageTarget -BundleDir $win64Dir -DumpbinPath $dumpbinPath
        $imageName = Split-Path $imageTarget -Leaf
        if ($missing.Count -eq 0) {
            Write-Host "[PASS] PE imports for $imageName all resolvable within bundle" -ForegroundColor Green
        } else {
            Write-Host "[FAIL] $imageName depends on DLL(s) missing from bundle: $($missing -join ', ')" -ForegroundColor Red
            $importScanFailures += [PSCustomObject]@{ image = $imageName; missing = $missing }
        }
    }
    if ($importScanFailures.Count -gt 0) {
        $summary = ($importScanFailures | ForEach-Object { "$($_.image) -> $($_.missing -join ', ')" }) -join '; '
        throw "PE import scan failed. Unbundled dependencies would prevent Resolve from loading the plugin: $summary"
    }
}

# Regression guard for Issue #56: the OFX bundle MUST ship the Visual C++
# Redistributable DLLs app-local. Without them, the spawned sidecar inherits
# the host's altered DLL search order (Foundry Nuke's SetDllDirectory call
# propagates per
# https://learn.microsoft.com/en-us/windows/win32/dlls/dynamic-link-library-search-order)
# and can load an ABI-incompatible MSVCP140 from the host install dir,
# crashing pre-wWinMain. CMake's InstallRequiredSystemLibraries module
# stages them via src/plugins/ofx/CMakeLists.txt; if a future refactor
# breaks that path, this validation step fails the release before
# anyone ships a bundle that reproduces #56.
$missingExpectedRuntime = Test-CorridorKeyExpectedRuntimeFilesPresent -BundleDir $win64Dir
if ($missingExpectedRuntime.Count -eq 0) {
    Write-Host "[PASS] Visual C++ Redistributable DLLs are bundled app-local" -ForegroundColor Green
} else {
    Write-Host "[FAIL] OFX bundle is missing app-local Visual C++ Redistributable DLLs: $($missingExpectedRuntime -join ', ')" -ForegroundColor Red
    throw "Bundle missing required app-local Visual C++ Redistributable DLLs ($($missingExpectedRuntime -join ', ')). See Issue #56; verify InstallRequiredSystemLibraries discovered the redist set during CMake configure."
}

$supportedBackends = @()
Push-Location $win64Dir
try {
    $runtimeInfoJson = & ".\corridorkey.exe" info --json 2>$null
    if ($LASTEXITCODE -eq 0 -and -not [string]::IsNullOrWhiteSpace($runtimeInfoJson)) {
        $runtimeInfo = $runtimeInfoJson | ConvertFrom-Json
        if ($null -ne $runtimeInfo.capabilities -and
            $null -ne $runtimeInfo.capabilities.supported_backends) {
            $supportedBackends = @($runtimeInfo.capabilities.supported_backends)
            Write-Host "[PASS] Runtime probe succeeded: $($supportedBackends -join ', ')" -ForegroundColor Green
        } else {
            Write-Host "[WARN] Runtime probe returned no supported_backends payload" -ForegroundColor Yellow
        }
    } else {
        Write-Host "[WARN] Runtime probe failed; falling back to DLL inspection" -ForegroundColor Yellow
    }
} catch {
    Write-Host "[WARN] Runtime probe failed; falling back to DLL inspection" -ForegroundColor Yellow
} finally {
    Pop-Location
}

# Check CUDA runtime (optional but should be present for NVIDIA systems)
$cudartFiles = @(Get-ChildItem -Path $win64Dir -Filter "cudart64_*.dll" -File -ErrorAction SilentlyContinue)
if ($cudartFiles.Count -eq 0) {
    if ($expectsDirectMlPath) {
        Write-Host "[INFO] No CUDA runtime DLL found (expected for DirectML bundle)" -ForegroundColor Cyan
    } else {
        Write-Host "[WARN] No CUDA runtime DLL found (cudart64_*.dll)" -ForegroundColor Yellow
    }
} else {
    foreach ($cudart in $cudartFiles) {
        Write-Host "[PASS] Found $($cudart.Name)" -ForegroundColor Green
    }
}

# Check TensorRT provider (optional)
$tensorrtProvider = @(Get-ChildItem -Path $win64Dir -Filter "onnxruntime_providers_*tensorrt*.dll" -File -ErrorAction SilentlyContinue)
if ($tensorrtProvider.Count -eq 0) {
    Write-Host "[INFO] No TensorRT provider found (DirectML will be used)" -ForegroundColor Cyan
} else {
    foreach ($provider in $tensorrtProvider) {
        Write-Host "[PASS] Found $($provider.Name)" -ForegroundColor Green
    }
}

# Check Windows universal GPU providers
$universalProviderDlls = @(
    "onnxruntime_providers_dml.dll",
    "onnxruntime_providers_winml.dll",
    "onnxruntime_providers_openvino.dll"
)
$foundUniversalProviders = @()
foreach ($provider in $universalProviderDlls) {
    $path = Join-Path $win64Dir $provider
    if (Test-Path $path) {
        $foundUniversalProviders += $provider
        Write-Host "[PASS] Found $provider" -ForegroundColor Green
    }
}
if ($foundUniversalProviders.Count -eq 0) {
    $message = "No Windows universal GPU provider DLL found; AMD/Intel systems will fall back to CPU."
    $hasUniversalGpuBackend = $supportedBackends -contains "dml" -or
        $supportedBackends -contains "winml" -or
        $supportedBackends -contains "openvino"
    if ($expectsUniversalGpuPath -and -not $hasUniversalGpuBackend) {
        Write-Host "[FAIL] $message" -ForegroundColor Red
        throw $message
    }
    if (-not $hasUniversalGpuBackend) {
        if ($expectsDirectMlPath) {
            Write-Host "[FAIL] $message" -ForegroundColor Red
            throw $message
        }
        Write-Host "[INFO] $message" -ForegroundColor Cyan
    }
}

if ($expectsDirectMlPath -and ($supportedBackends -notcontains "dml")) {
    Write-Host "[FAIL] DirectML bundle did not report DML support in runtime probe." -ForegroundColor Red
    throw "DirectML bundle missing DML runtime support."
}

# Check models
$bundleModelInventory = if (Test-Path $modelInventoryPath) {
    Get-Content -Path $modelInventoryPath -Raw | ConvertFrom-Json
} else {
    $expectedModels = Get-CorridorKeyOfxBundleTargetModels -ModelProfile $defaultModelProfile
    $fallbackInventory = [ordered]@{}
    foreach ($property in (Get-CorridorKeyModelInventory -ModelsDir $resourcesDir -ExpectedModels $expectedModels).GetEnumerator()) {
        $fallbackInventory[$property.Key] = $property.Value
    }
    $fallbackInventory["model_profile"] = $defaultModelProfile
    [pscustomobject]$fallbackInventory
}

$presentModels = @($bundleModelInventory.present_models)
$missingModels = @($bundleModelInventory.missing_models)
$expectedModels = @($bundleModelInventory.expected_models)
$modelProfile = if (Test-CorridorKeyJsonProperty -Object $bundleModelInventory -Name "model_profile") {
    $bundleModelInventory.model_profile
} else {
    $defaultModelProfile
}
$expectedProfileContract = Get-CorridorKeyModelProfileContract -ModelProfile $modelProfile
$compiledContextComplete = if (Test-CorridorKeyJsonProperty -Object $bundleModelInventory -Name "compiled_context_complete") {
    [bool]$bundleModelInventory.compiled_context_complete
} else {
    $false
}
$expectedCompiledContextModels = if (Test-CorridorKeyJsonProperty -Object $bundleModelInventory -Name "expected_compiled_context_models") {
    @($bundleModelInventory.expected_compiled_context_models)
} else {
    @()
}
$missingCompiledContextModels = if (Test-CorridorKeyJsonProperty -Object $bundleModelInventory -Name "missing_compiled_context_models") {
    @($bundleModelInventory.missing_compiled_context_models)
} else {
    @()
}
$inventoryContractIssues = Get-CorridorKeyInventoryContractIssues `
    -Inventory $bundleModelInventory `
    -ExpectedContract $expectedProfileContract
$inventoryContractComplete = @($inventoryContractIssues).Count -eq 0
if (-not $inventoryContractComplete) {
    throw "Bundle model inventory contract is invalid. Issues: $($inventoryContractIssues -join ' | ')"
}

$certificationContractIssues = @()
$certificationContractComplete = $false
$certificationManifestPresent = Test-Path $artifactManifestPath
if ($expectedProfileContract.expects_compiled_context_models) {
    if (-not $certificationManifestPresent) {
        $certificationContractIssues += "Packaged RTX bundle is missing artifact_manifest.json."
    } else {
        $certificationManifest = Read-CorridorKeyWindowsRtxArtifactManifest -ArtifactManifestPath $artifactManifestPath
        $certificationContractIssues = Get-CorridorKeyWindowsRtxArtifactManifestIssues `
            -Manifest $certificationManifest `
            -ArtifactsDir $resourcesDir `
            -ExpectedModels $presentModels `
            -ExpectedCompiledContextModels $expectedCompiledContextModels
        $certificationContractComplete = @($certificationContractIssues).Count -eq 0
        if (-not $certificationContractComplete) {
            throw "Bundle RTX certification contract is invalid. Issues: $($certificationContractIssues -join ' | ')"
        }
    }
}

foreach ($model in $presentModels) {
    $path = Join-Path $resourcesDir $model
    $modelSize = (Get-Item $path).Length
    Write-Host "[PASS] Found $model ($([math]::Round($modelSize / 1MB, 2)) MB)" -ForegroundColor Green
}
foreach ($model in $missingModels) {
    Write-Host "[INFO] Packaged bundle omits model: $model" -ForegroundColor Cyan
}

$doctorReportPath = Join-Path $bundleRoot "doctor_report.json"
$previousModelsDir = if (Test-Path Env:CORRIDORKEY_MODELS_DIR) {
    $env:CORRIDORKEY_MODELS_DIR
} else {
    $null
}
$doctorSucceeded = $false
$doctorHealthy = $false
$doctorModelContractsAvailable = $false
$doctorModelContractsHealthy = $null
$doctorFailureTolerated = $false
$doctorFailureReason = ""
$doctorModelContractIssues = @()

Write-Host "[DOCTOR] Running packaged runtime doctor..." -ForegroundColor Cyan
Push-Location $win64Dir
try {
    $doctorStdoutPath = Join-Path $env:TEMP ("corridorkey_validate_stdout_" + [System.Guid]::NewGuid().ToString("N") + ".txt")
    $doctorStderrPath = Join-Path $env:TEMP ("corridorkey_validate_stderr_" + [System.Guid]::NewGuid().ToString("N") + ".txt")
    try {
        $doctorCommand = 'set "CORRIDORKEY_MODELS_DIR={0}" && cd /d "{1}" && .\corridorkey.exe doctor --json > "{2}" 2> "{3}"' -f `
            $resourcesDir, $win64Dir, $doctorStdoutPath, $doctorStderrPath
        & $env:ComSpec /v:on /d /c $doctorCommand | Out-Null
        $doctorExitCode = $LASTEXITCODE
        $doctorJson = if (Test-Path $doctorStdoutPath) {
            Get-Content -Path $doctorStdoutPath -Raw -ErrorAction SilentlyContinue
        } else {
            ""
        }
        $doctorStderr = if (Test-Path $doctorStderrPath) {
            Get-Content -Path $doctorStderrPath -Raw -ErrorAction SilentlyContinue
        } else {
            ""
        }
    } finally {
        Remove-Item $doctorStdoutPath -Force -ErrorAction SilentlyContinue
        Remove-Item $doctorStderrPath -Force -ErrorAction SilentlyContinue
    }

    if ($doctorExitCode -ne 0 -or [string]::IsNullOrWhiteSpace($doctorJson)) {
        if (-not [string]::IsNullOrWhiteSpace($doctorStderr)) {
            Write-Host "[INFO] Packaged runtime doctor stderr:" -ForegroundColor Cyan
            Write-Host $doctorStderr -ForegroundColor Cyan
        }
        if ($missingModels.Count -gt 0) {
            $doctorFailureTolerated = $true
            $doctorFailureReason = "Packaged runtime doctor failed while the bundle is missing model(s)."
            Write-Host "[INFO] $doctorFailureReason" -ForegroundColor Cyan
        } else {
            throw "Packaged runtime doctor failed."
        }
    }

    $doctor = $null
    if (-not [string]::IsNullOrWhiteSpace($doctorJson)) {
        $doctorJson | Set-Content -Path $doctorReportPath -Encoding UTF8
        $doctor = $doctorJson | ConvertFrom-Json
        if (-not (Test-CorridorKeyJsonProperty -Object $doctor -Name "summary") -or $null -eq $doctor.summary) {
            throw "Packaged runtime doctor report is missing the summary payload."
        }

        $doctorSucceeded = $true
        $doctorHealthy = [bool]$doctor.summary.healthy
        $doctorModelContractsAvailable = Test-CorridorKeyJsonProperty -Object $doctor -Name "model_contracts"
        if (Test-CorridorKeyJsonProperty -Object $doctor.summary -Name "model_contracts_healthy") {
            $doctorModelContractsHealthy = [bool]$doctor.summary.model_contracts_healthy
            $doctorModelContractsAvailable = $true
        } elseif (Test-CorridorKeyJsonProperty -Object $doctor.summary -Name "bundle_inventory_contract_healthy") {
            $doctorModelContractsHealthy = [bool]$doctor.summary.bundle_inventory_contract_healthy
            $doctorModelContractsAvailable = $true
        }

        Write-Host "[PASS] Wrote doctor report: $doctorReportPath" -ForegroundColor Green
        $summaryModelContractsHealthy = if ($doctorModelContractsAvailable -and $null -ne $doctorModelContractsHealthy) {
            $doctorModelContractsHealthy
        } else {
            "n/a"
        }
        Write-Host "[INFO] Doctor summary healthy=$($doctor.summary.healthy) model_contracts_healthy=$summaryModelContractsHealthy windows_universal_healthy=$($doctor.summary.windows_universal_healthy)" -ForegroundColor Cyan
        if ((Test-CorridorKeyJsonProperty -Object $doctor -Name "model_contracts") -and
            $null -ne $doctor.model_contracts) {
            $contractGroups = @($doctor.model_contracts.groups)
            foreach ($group in $contractGroups) {
                Write-Host "[INFO] Model contract group '$($group.group)': healthy=$($group.healthy) loadable=$($group.all_models_loadable) consistent=$($group.contract_consistent) baseline=$($group.baseline_model)" -ForegroundColor Cyan
            }
            $unhealthyContractGroups = @($contractGroups | Where-Object { -not $_.healthy })
            foreach ($group in $unhealthyContractGroups) {
                $firstIssue = $group.models | Where-Object {
                    (-not $_.load_ok) -or (-not $_.contract_match_baseline)
                } | Select-Object -First 1
                if ($null -ne $firstIssue) {
                    $doctorModelContractIssues += @($group.models | Where-Object {
                        (-not $_.load_ok) -or (-not $_.contract_match_baseline)
                    })
                    $reason = if ([string]::IsNullOrWhiteSpace($firstIssue.error)) {
                        "Contract mismatch relative to baseline."
                    } else {
                        $firstIssue.error
                    }
                    Write-Host "[INFO] Model contract group '$($group.group)' first issue: $($firstIssue.filename) -> $reason" -ForegroundColor Cyan
                }
            }
        } else {
            Write-Host "[INFO] Doctor schema does not expose model contract groups; skipping that validation layer." -ForegroundColor Cyan
        }
    }
} finally {
    if ($null -ne $previousModelsDir) {
        $env:CORRIDORKEY_MODELS_DIR = $previousModelsDir
    } else {
        Remove-Item Env:CORRIDORKEY_MODELS_DIR -ErrorAction SilentlyContinue
    }
    Pop-Location
}

if ($doctorSucceeded -and $doctorModelContractsAvailable -and -not $doctorModelContractsHealthy) {
    $nonMissingIssues = @($doctorModelContractIssues | Where-Object {
        ($missingModels -notcontains $_.filename) -or $_.error -ne "Model not found"
    })
    if ($nonMissingIssues.Count -eq 0 -and $missingModels.Count -gt 0) {
        $doctorFailureTolerated = $true
        if ([string]::IsNullOrWhiteSpace($doctorFailureReason)) {
            $doctorFailureReason = "Packaged runtime doctor reported unhealthy model contracts only because model(s) are absent from this bundle."
        }
        Write-Host "[INFO] Packaged runtime doctor reported unhealthy model contracts only because model(s) are absent from this bundle." -ForegroundColor Cyan
    } else {
        throw "Packaged runtime doctor reported unhealthy model contracts. See $doctorReportPath."
    }
}

if ($doctorSucceeded -and -not $doctorHealthy -and -not $doctorFailureTolerated) {
    $missingModelProbeFailuresOnly = Test-CorridorKeyDoctorMissingModelProbeFailuresOnly `
        -Doctor $doctor `
        -MissingModels $missingModels
    if ($missingModelProbeFailuresOnly) {
        $doctorFailureTolerated = $true
        if ([string]::IsNullOrWhiteSpace($doctorFailureReason)) {
            $doctorFailureReason = "Packaged runtime doctor reported unhealthy execution probes only because model(s) are absent from this bundle."
        }
        Write-Host "[INFO] Packaged runtime doctor reported unhealthy execution probes only because model(s) are absent from this bundle." -ForegroundColor Cyan
    } else {
        throw "Packaged runtime doctor reported unhealthy status. See $doctorReportPath."
    }
}

$nukeSmokePayload = [ordered]@{
    attempted = $false
    succeeded = $null
    failure_reason = ""
    nuke_exe = ""
    nuke_version = ""
    output_path = ""
    exit_code = $null
    skipped_reason = ""
}

# Nuke smoke is opt-in: CI runners do not have a licensed Nuke install,
# so the validator stays silent unless the operator explicitly asks for the
# probe via CORRIDORKEY_VALIDATE_NUKE=1. The smoke runner emits its own
# JSON which we ingest verbatim into the bundle validation payload.
$validateNukeRequested = ($env:CORRIDORKEY_VALIDATE_NUKE -eq "1")
if ($validateNukeRequested) {
    Write-Host "[NUKE] CORRIDORKEY_VALIDATE_NUKE=1 -- running headless Nuke smoke against staged bundle..." -ForegroundColor Cyan
    $smokeRunner = Join-Path $repoRoot "scripts\test_nuke_smoke.ps1"
    if (-not (Test-Path $smokeRunner)) {
        $nukeSmokePayload.attempted = $true
        $nukeSmokePayload.succeeded = $false
        $nukeSmokePayload.failure_reason = "Smoke runner not found at $smokeRunner."
        Write-Host "[NUKE] Skipping: $($nukeSmokePayload.failure_reason)" -ForegroundColor Yellow
    } else {
        $smokeResultPath = Join-Path $env:TEMP ("corridorkey_nuke_smoke_" + [System.Guid]::NewGuid().ToString("N") + ".json")
        try {
            & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $smokeRunner `
                -BundlePath $bundleDescriptor `
                -ResultJsonPath $smokeResultPath
            $smokeExit = $LASTEXITCODE
            if (Test-Path $smokeResultPath) {
                $smokeReport = Get-Content -Path $smokeResultPath -Raw | ConvertFrom-Json
                $nukeSmokePayload.attempted = [bool]$smokeReport.attempted
                $nukeSmokePayload.succeeded = [bool]$smokeReport.succeeded
                $nukeSmokePayload.failure_reason = [string]$smokeReport.failure_reason
                $nukeSmokePayload.nuke_exe = [string]$smokeReport.nuke_exe
                $nukeSmokePayload.nuke_version = [string]$smokeReport.nuke_version
                $nukeSmokePayload.output_path = [string]$smokeReport.output_path
                $nukeSmokePayload.exit_code = [int]$smokeReport.exit_code
            } else {
                $nukeSmokePayload.attempted = $true
                $nukeSmokePayload.succeeded = $false
                $nukeSmokePayload.failure_reason = "Smoke runner did not produce a result JSON (exit=$smokeExit)."
                $nukeSmokePayload.exit_code = $smokeExit
            }
        } catch {
            $nukeSmokePayload.attempted = $true
            $nukeSmokePayload.succeeded = $false
            $nukeSmokePayload.failure_reason = "Smoke runner threw: $_"
        } finally {
            Remove-Item $smokeResultPath -Force -ErrorAction SilentlyContinue
        }

        if ($nukeSmokePayload.succeeded) {
            Write-Host "[NUKE] Smoke PASS (Nuke $($nukeSmokePayload.nuke_version))" -ForegroundColor Green
        } else {
            Write-Host "[NUKE] Smoke FAIL: $($nukeSmokePayload.failure_reason)" -ForegroundColor Red
        }
    }
} else {
    $nukeSmokePayload.skipped_reason = "CORRIDORKEY_VALIDATE_NUKE not set; opt in by exporting CORRIDORKEY_VALIDATE_NUKE=1."
}

$validationPayload = [ordered]@{
    bundle_path = $bundleDescriptor
    validation_passed = $true
    runtime_probe = [ordered]@{
        supported_backends = @($supportedBackends)
    }
    models = [ordered]@{
        model_profile = $modelProfile
        expected_models = @($expectedModels)
        present_models = @($presentModels)
        missing_models = @($missingModels)
        present_count = @($presentModels).Count
        missing_count = @($missingModels).Count
        inventory_contract = [ordered]@{
            complete = $inventoryContractComplete
            issues = @($inventoryContractIssues)
            expected_contract = [ordered]@{
                package_type = $expectedProfileContract.package_type
                model_profile = $expectedProfileContract.model_profile
                bundle_track = $expectedProfileContract.bundle_track
                release_label = $expectedProfileContract.release_label
                optimization_profile_id = $expectedProfileContract.optimization_profile_id
                optimization_profile_label = $expectedProfileContract.optimization_profile_label
                backend_intent = $expectedProfileContract.backend_intent
                fallback_policy = $expectedProfileContract.fallback_policy
                warmup_policy = $expectedProfileContract.warmup_policy
                certification_tier = $expectedProfileContract.certification_tier
                unrestricted_quality_attempt = $expectedProfileContract.unrestricted_quality_attempt
            }
            compiled_context_complete = $compiledContextComplete
            expected_compiled_context_models = @($expectedCompiledContextModels)
            missing_compiled_context_models = @($missingCompiledContextModels)
        }
        certification_contract = [ordered]@{
            complete = $certificationContractComplete
            issues = @($certificationContractIssues)
            manifest_present = $certificationManifestPresent
        }
    }
    doctor = [ordered]@{
        attempted = $true
        succeeded = $doctorSucceeded
        healthy = $doctorHealthy
        model_contracts_available = $doctorModelContractsAvailable
        model_contracts_healthy = $doctorModelContractsHealthy
        failure_tolerated = $doctorFailureTolerated
        failure_reason = $doctorFailureReason
        report_path = if ($doctorSucceeded) { $doctorReportPath } else { "" }
    }
    nuke_smoke = $nukeSmokePayload
}
Write-CorridorKeyJsonFile -Path $bundleValidationPath -Payload $validationPayload
Write-Host "[PASS] Wrote bundle validation report: $bundleValidationPath" -ForegroundColor Green

Write-Host ""
Write-Host "================================" -ForegroundColor Green
Write-Host "Bundle validation PASSED" -ForegroundColor Green
Write-Host "================================" -ForegroundColor Green
Write-Host ""
if ($missingModels.Count -gt 0) {
    Write-Host "Bundle is ready for installation with partial model coverage. Missing models are listed in bundle_validation.json and model_inventory.json." -ForegroundColor Cyan
} else {
    Write-Host "Bundle is ready for installation and should work with DaVinci Resolve."
}
