param(
    [string]$VendorRoot = "",
    [switch]$Force
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot "windows_runtime_helpers.ps1")

# Strategy C (see .planning/.../federated-gathering-boole.md):
# The TorchTRT runtime payload is the *blue model pack* dependency, not
# part of the base Windows RTX bundle. This script stages a curated DLL
# subset under vendor/torchtrt-windows/ for use by:
#   - PR 2 dev/test build of TorchTrtSession (CMake imports DLLs as
#     SHARED IMPORTED targets from this layout)
#   - PR 4 blue-pack assembly (Hugging Face upload bundles
#     vendor/torchtrt-windows/{bin,lib} into the blue model pack)
#
# DLL set is curated against the empirical "minimum to load and run a
# blue corridorkey_torchtrt_fp16_<res>.ts on RTX 3080" measured in
# Sprint 0 (see temp/blue-diagnose/SPRINT0_RESULTS.md). The 1.8 GB
# nvinfer_builder_resource_10.dll is excluded - it is required only
# for engine compilation, never for runtime deserialization.

if ([string]::IsNullOrWhiteSpace($VendorRoot)) {
    $VendorRoot = Join-Path $repoRoot "vendor\torchtrt-windows"
}

$contract = Get-CorridorKeyWindowsTorchTrtBuildContract
$stagingRoot = Join-Path $repoRoot "temp\torchtrt-windows-download"
$wheelStaging = Join-Path $stagingRoot "wheels"
$extractStaging = Join-Path $stagingRoot "extracted"

function Resolve-PythonForWheelDownload {
    foreach ($candidate in @("python.exe", "python", "py.exe", "py")) {
        $cmd = Get-Command $candidate -ErrorAction SilentlyContinue
        if ($null -ne $cmd) {
            return $cmd.Source
        }
    }
    throw "python was not found on PATH. Install Python 3.12 (or any version with pip) to run prepare-torchtrt."
}

function Invoke-PipDownload {
    param(
        [string]$PythonExe,
        [string]$Spec,
        [string]$IndexUrl = "",
        [string]$ExtraIndexUrl = ""
    )

    $args = @(
        "-m", "pip", "download",
        "--no-deps",
        "--dest", $wheelStaging,
        "--platform", "win_amd64",
        "--python-version", $contract.required_python_version,
        "--implementation", "cp",
        "--abi", $contract.required_python_abi_tag,
        "--only-binary=:all:"
    )
    if (-not [string]::IsNullOrWhiteSpace($IndexUrl)) {
        $args += @("--index-url", $IndexUrl)
    }
    if (-not [string]::IsNullOrWhiteSpace($ExtraIndexUrl)) {
        $args += @("--extra-index-url", $ExtraIndexUrl)
    }
    $args += $Spec

    Write-Host ("  > " + $PythonExe + " " + ($args -join " ")) -ForegroundColor DarkGray
    & $PythonExe @args
    if ($LASTEXITCODE -ne 0) {
        throw "pip download failed for spec: $Spec"
    }
}

function Expand-WheelArchive {
    param(
        [string]$WheelPath,
        [string]$DestinationDir
    )

    # A .whl file is a zip with a manifest. Expand-Archive insists on a
    # .zip extension on PS5.1, so copy to a temporary .zip first.
    $tempZip = $WheelPath + ".zip"
    Copy-Item -Path $WheelPath -Destination $tempZip -Force
    try {
        Expand-CorridorKeyArchive -ArchivePath $tempZip -DestinationDir $DestinationDir
    } finally {
        Remove-Item $tempZip -Force -ErrorAction SilentlyContinue
    }
}

function Find-WheelByPrefix {
    param([string]$NamePrefix)

    # Wrap in @() because Get-ChildItem returns a scalar FileInfo (not an
    # array) when only one file matches; under StrictMode .Count then
    # explodes. Also avoid $matches (PowerShell automatic variable for
    # regex captures).
    $wheelMatches = @(Get-ChildItem -Path $wheelStaging -Filter ($NamePrefix + "*.whl") -File -ErrorAction SilentlyContinue)
    if ($wheelMatches.Count -eq 0) {
        throw "Expected wheel matching '$NamePrefix*.whl' was not downloaded into $wheelStaging."
    }
    return $wheelMatches | Select-Object -First 1
}

function Copy-DllSetIntoBin {
    param(
        [string]$SourceDir,
        [string[]]$ExcludedDlls,
        [string]$DestinationBin,
        [System.Collections.Generic.List[object]]$ManifestList,
        [string]$SourceLabel
    )

    # Inversion (see Get-CorridorKeyWindowsTorchTrtBuildContract): copy
    # everything in $SourceDir except the named exclusions. The dependency
    # graph in libtorch + torch_tensorrt + tensorrt is wide enough that an
    # allowlist gets brittle - every missed transitive dep manifests as
    # GetLastError=126 with no symbol info.
    $excluded = @{}
    foreach ($name in $ExcludedDlls) { $excluded[$name.ToLowerInvariant()] = $true }

    $dlls = @(Get-ChildItem -Path $SourceDir -Filter "*.dll" -File -ErrorAction Stop)
    foreach ($srcInfo in $dlls) {
        if ($excluded.ContainsKey($srcInfo.Name.ToLowerInvariant())) {
            continue
        }
        $dstPath = Join-Path $DestinationBin $srcInfo.Name
        Copy-Item -Path $srcInfo.FullName -Destination $dstPath -Force
        $info = Get-Item -Path $dstPath
        $hash = (Get-FileHash -Path $dstPath -Algorithm SHA256).Hash
        $ManifestList.Add([ordered]@{
            name = $srcInfo.Name
            source = $SourceLabel
            size_bytes = $info.Length
            sha256 = $hash
        })
    }
}

function Copy-ImportLibIfPresent {
    param(
        [string]$SourceDir,
        [string]$LibName,
        [string]$DestinationLib
    )

    $srcPath = Join-Path $SourceDir $LibName
    if (-not (Test-Path $srcPath)) {
        return $false
    }
    Copy-Item -Path $srcPath -Destination (Join-Path $DestinationLib $LibName) -Force
    return $true
}

function Copy-DirectoryTree {
    param(
        [string]$SourceDir,
        [string]$DestinationDir
    )

    if (-not (Test-Path $SourceDir)) {
        return $false
    }
    if (Test-Path $DestinationDir) {
        Remove-Item $DestinationDir -Recurse -Force
    }
    Copy-Item -Path $SourceDir -Destination $DestinationDir -Recurse -Force
    return $true
}

# ---- preflight ----

if ((Test-Path $VendorRoot) -and (-not $Force.IsPresent)) {
    $manifest = Join-Path $VendorRoot "torchtrt_manifest.json"
    if (Test-Path $manifest) {
        Write-Host "[prepare-torchtrt] Vendor root already populated at $VendorRoot." -ForegroundColor Green
        Write-Host "[prepare-torchtrt] Pass -Force to refresh." -ForegroundColor Cyan
        return
    }
}

$pythonExe = Resolve-PythonForWheelDownload
Write-Host "[prepare-torchtrt] Using Python: $pythonExe" -ForegroundColor Cyan
Write-Host "[prepare-torchtrt] Pinned versions:" -ForegroundColor Cyan
Write-Host ("    torch                = " + $contract.torch_version) -ForegroundColor Cyan
Write-Host ("    torch_tensorrt       = " + $contract.torch_tensorrt_version) -ForegroundColor Cyan
Write-Host ("    tensorrt-cu12        = " + $contract.tensorrt_cu12_version) -ForegroundColor Cyan
Write-Host ("    cuda (target)        = " + $contract.required_cuda_version) -ForegroundColor Cyan
Write-Host ("    python ABI (wheel)   = " + $contract.required_python_abi_tag) -ForegroundColor Cyan

if (Test-Path $stagingRoot) {
    Remove-Item $stagingRoot -Recurse -Force
}
New-Item -ItemType Directory -Path $wheelStaging -Force | Out-Null
New-Item -ItemType Directory -Path $extractStaging -Force | Out-Null

# ---- step 1: download wheels ----

Write-Host "[prepare-torchtrt] [1/4] Downloading wheels (~9 GB before curation)..." -ForegroundColor Cyan

Invoke-PipDownload -PythonExe $pythonExe `
    -Spec ("torch==" + $contract.torch_version) `
    -IndexUrl $contract.torch_index_url `
    -ExtraIndexUrl "https://pypi.org/simple"

Invoke-PipDownload -PythonExe $pythonExe `
    -Spec ("torch-tensorrt==" + $contract.torch_tensorrt_version) `
    -IndexUrl $contract.torch_index_url `
    -ExtraIndexUrl "https://pypi.org/simple"

Invoke-PipDownload -PythonExe $pythonExe `
    -Spec ("tensorrt-cu12-libs==" + $contract.tensorrt_cu12_version) `
    -IndexUrl "https://pypi.nvidia.com" `
    -ExtraIndexUrl "https://pypi.org/simple"

# ---- step 2: extract wheels ----

Write-Host "[prepare-torchtrt] [2/4] Extracting wheels..." -ForegroundColor Cyan

$torchWheel = Find-WheelByPrefix -NamePrefix ("torch-" + $contract.torch_version.Split("+")[0])
$torchTrtWheel = Find-WheelByPrefix -NamePrefix ("torch_tensorrt-" + $contract.torch_tensorrt_version)
$tensorRtWheel = Find-WheelByPrefix -NamePrefix ("tensorrt_cu12_libs-" + $contract.tensorrt_cu12_version)

$torchExtract = Join-Path $extractStaging "torch"
$torchTrtExtract = Join-Path $extractStaging "torch_tensorrt"
$tensorRtExtract = Join-Path $extractStaging "tensorrt_cu12"

Expand-WheelArchive -WheelPath $torchWheel.FullName -DestinationDir $torchExtract
Expand-WheelArchive -WheelPath $torchTrtWheel.FullName -DestinationDir $torchTrtExtract
Expand-WheelArchive -WheelPath $tensorRtWheel.FullName -DestinationDir $tensorRtExtract

$torchLibDir = Join-Path $torchExtract "torch\lib"
$torchIncludeDir = Join-Path $torchExtract "torch\include"
$torchTrtLibDir = Join-Path $torchTrtExtract "torch_tensorrt\lib"
$tensorRtLibDir = Join-Path $tensorRtExtract "tensorrt_libs"

foreach ($dir in @($torchLibDir, $torchIncludeDir, $torchTrtLibDir, $tensorRtLibDir)) {
    if (-not (Test-Path $dir)) {
        throw "Expected extracted wheel directory missing: $dir"
    }
}

# ---- step 3: curate into vendor root ----

Write-Host "[prepare-torchtrt] [3/4] Curating runtime DLLs into $VendorRoot..." -ForegroundColor Cyan

if (Test-Path $VendorRoot) {
    Remove-Item $VendorRoot -Recurse -Force
}
$vendorBin = Join-Path $VendorRoot "bin"
$vendorLib = Join-Path $VendorRoot "lib"
$vendorInclude = Join-Path $VendorRoot "include"
New-Item -ItemType Directory -Path $vendorBin -Force | Out-Null
New-Item -ItemType Directory -Path $vendorLib -Force | Out-Null
New-Item -ItemType Directory -Path $vendorInclude -Force | Out-Null

$dllManifest = New-Object 'System.Collections.Generic.List[object]'

Copy-DllSetIntoBin -SourceDir $torchLibDir `
    -ExcludedDlls $contract.torch_lib_exclusions `
    -DestinationBin $vendorBin `
    -ManifestList $dllManifest `
    -SourceLabel ("torch-" + $contract.torch_version)

Copy-DllSetIntoBin -SourceDir $torchTrtLibDir `
    -ExcludedDlls $contract.torch_tensorrt_lib_exclusions `
    -DestinationBin $vendorBin `
    -ManifestList $dllManifest `
    -SourceLabel ("torch-tensorrt-" + $contract.torch_tensorrt_version)

Copy-DllSetIntoBin -SourceDir $tensorRtLibDir `
    -ExcludedDlls $contract.tensorrt_lib_exclusions `
    -DestinationBin $vendorBin `
    -ManifestList $dllManifest `
    -SourceLabel ("tensorrt-cu12-" + $contract.tensorrt_cu12_version)

# Import libraries (.lib) needed for linking against the runtime in PR 2.
$importLibsCopied = New-Object 'System.Collections.Generic.List[string]'
$importLibCandidates = @(
    @{ Source = $torchLibDir; Names = @("c10.lib", "c10_cuda.lib", "torch.lib", "torch_cpu.lib", "torch_cuda.lib") },
    @{ Source = $torchTrtLibDir; Names = @("torchtrt.lib") }
)
foreach ($candidate in $importLibCandidates) {
    foreach ($name in $candidate.Names) {
        if (Copy-ImportLibIfPresent -SourceDir $candidate.Source -LibName $name -DestinationLib $vendorLib) {
            $importLibsCopied.Add($name) | Out-Null
        }
    }
}

# Headers for libtorch + torch_tensorrt (used by PR 2 to compile torch_trt_session.cpp).
[void](Copy-DirectoryTree -SourceDir $torchIncludeDir -DestinationDir (Join-Path $vendorInclude "torch"))

# ---- step 4: emit manifest ----

Write-Host "[prepare-torchtrt] [4/4] Writing manifest..." -ForegroundColor Cyan

$totalBytes = 0L
foreach ($entry in $dllManifest) { $totalBytes += [int64]$entry.size_bytes }
$manifestPayload = [ordered]@{
    contract = [ordered]@{
        torch_version = $contract.torch_version
        torch_index_url = $contract.torch_index_url
        torch_tensorrt_version = $contract.torch_tensorrt_version
        tensorrt_cu12_version = $contract.tensorrt_cu12_version
        required_cuda_version = $contract.required_cuda_version
        required_python_abi_tag = $contract.required_python_abi_tag
    }
    vendor_root = [System.IO.Path]::GetFullPath($VendorRoot)
    bin_total_bytes = $totalBytes
    bin_total_mib = [math]::Round($totalBytes / (1024 * 1024), 1)
    dll_count = $dllManifest.Count
    dlls = $dllManifest
    import_libs = $importLibsCopied
    notes = @(
        "Curated subset for blue-pack runtime; not the full wheel content.",
        "nvinfer_builder_resource_10.dll (1.8 GB) intentionally excluded - compile-only.",
        "Downstream consumer: PR 2 (CMake), PR 4 (blue Hugging Face pack)."
    )
}
$manifestPath = Join-Path $VendorRoot "torchtrt_manifest.json"
$manifestPayload | ConvertTo-Json -Depth 6 | Set-Content -Path $manifestPath -Encoding UTF8

Write-Host ("[prepare-torchtrt] DONE. Curated {0} DLLs ({1:N1} MiB) into {2}" -f
    $dllManifest.Count, ($totalBytes / (1024 * 1024)), $VendorRoot) -ForegroundColor Green
Write-Host ("[prepare-torchtrt] Manifest: {0}" -f $manifestPath) -ForegroundColor Green

# Cleanup the multi-GB staging tree once curation succeeded.
if (Test-Path $stagingRoot) {
    Write-Host "[prepare-torchtrt] Cleaning up wheel staging at $stagingRoot ..." -ForegroundColor Cyan
    Remove-Item $stagingRoot -Recurse -Force
}
