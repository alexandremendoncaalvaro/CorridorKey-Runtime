Set-StrictMode -Version Latest

function Test-CorridorKeyWindowsHost {
    return [System.Environment]::OSVersion.Platform -eq [System.PlatformID]::Win32NT
}

function Get-CorridorKeyProjectVersion {
    param([string]$RepoRoot)

    $cmakePath = Join-Path $RepoRoot "CMakeLists.txt"
    if (-not (Test-Path $cmakePath)) {
        throw "Could not determine project version because CMakeLists.txt was not found at $cmakePath"
    }

    $versionLine = Select-String -Path $cmakePath -Pattern '^\s*VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)\s*$'
    if ($null -ne $versionLine) {
        return $versionLine.Matches[0].Groups[1].Value
    }

    throw "Could not determine project version from $cmakePath"
}

function Get-CorridorKeyWindowsOrtRootPath {
    param(
        [string]$RepoRoot,
        [ValidateSet("rtx", "dml")]
        [string]$Track
    )

    $directoryName = if ($Track -eq "rtx") {
        "onnxruntime-windows-rtx"
    } else {
        "onnxruntime-windows-dml"
    }

    return [System.IO.Path]::GetFullPath((Join-Path $RepoRoot ("vendor\" + $directoryName)))
}

function Get-CorridorKeyWindowsTrackFromReleaseSuffix {
    param(
        [string]$ReleaseSuffix,
        [ValidateSet("rtx", "dml", "any")]
        [string]$DefaultTrack = "rtx"
    )

    if ([string]::IsNullOrWhiteSpace($ReleaseSuffix)) {
        return $DefaultTrack
    }

    if ($ReleaseSuffix -match "DirectML" -or $ReleaseSuffix -match "DML") {
        return "dml"
    }

    if ($ReleaseSuffix -match "RTX") {
        return "rtx"
    }

    return $DefaultTrack
}

function Resolve-CorridorKeyWindowsOrtRoot {
    param(
        [string]$RepoRoot,
        [string]$ExplicitRoot = "",
        [ValidateSet("rtx", "dml", "any")]
        [string]$PreferredTrack = "any",
        [switch]$AllowEnvironmentOverride
    )

    if (-not [string]::IsNullOrWhiteSpace($ExplicitRoot)) {
        if (-not (Test-Path $ExplicitRoot)) {
            throw "Configured ONNX Runtime root does not exist: $ExplicitRoot"
        }
        return [System.IO.Path]::GetFullPath($ExplicitRoot)
    }

    if ($AllowEnvironmentOverride.IsPresent -and -not [string]::IsNullOrWhiteSpace($env:CORRIDORKEY_WINDOWS_ORT_ROOT)) {
        if (-not (Test-Path $env:CORRIDORKEY_WINDOWS_ORT_ROOT)) {
            throw "CORRIDORKEY_WINDOWS_ORT_ROOT does not exist: $env:CORRIDORKEY_WINDOWS_ORT_ROOT"
        }
        return [System.IO.Path]::GetFullPath($env:CORRIDORKEY_WINDOWS_ORT_ROOT)
    }

    $tracksToCheck = switch ($PreferredTrack) {
        "rtx" { @("rtx") }
        "dml" { @("dml") }
        default { @("rtx", "dml") }
    }

    foreach ($track in $tracksToCheck) {
        $candidate = Get-CorridorKeyWindowsOrtRootPath -RepoRoot $RepoRoot -Track $track
        if (Test-Path $candidate) {
            return $candidate
        }
    }

    if ($PreferredTrack -eq "rtx") {
        throw "Curated RTX runtime not found. Stage vendor\onnxruntime-windows-rtx or pass -OrtRoot explicitly."
    }

    if ($PreferredTrack -eq "dml") {
        throw "Curated DirectML runtime not found. Stage vendor\onnxruntime-windows-dml or pass -OrtRoot explicitly."
    }

    throw "Windows builds require a curated ONNX Runtime root. Stage vendor\onnxruntime-windows-rtx or vendor\onnxruntime-windows-dml, or set CORRIDORKEY_WINDOWS_ORT_ROOT."
}

function Get-CorridorKeyPreparedModelList {
    return @(
        "corridorkey_fp16_512.onnx",
        "corridorkey_fp16_768.onnx",
        "corridorkey_fp16_1024.onnx",
        "corridorkey_fp16_1536.onnx",
        "corridorkey_fp16_2048.onnx",
        "corridorkey_int8_512.onnx",
        "corridorkey_int8_768.onnx",
        "corridorkey_int8_1024.onnx"
    )
}

function Get-CorridorKeyIntermediateModelList {
    return @(
        "corridorkey_fp32_512.onnx",
        "corridorkey_fp32_768.onnx",
        "corridorkey_fp32_1024.onnx",
        "corridorkey_fp32_1536.onnx",
        "corridorkey_fp32_2048.onnx"
    )
}

function Get-CorridorKeyOfxBundleTargetModels {
    param([switch]$Include2048)

    $models = @(
        "corridorkey_fp16_512.onnx",
        "corridorkey_fp16_768.onnx",
        "corridorkey_fp16_1024.onnx",
        "corridorkey_fp16_1536.onnx",
        "corridorkey_int8_512.onnx",
        "corridorkey_int8_768.onnx",
        "corridorkey_int8_1024.onnx"
    )
    if ($Include2048.IsPresent) {
        $models += "corridorkey_fp16_2048.onnx"
    }

    return $models
}

function Get-CorridorKeyPortableRuntimeTargetModels {
    return @(
        "corridorkey_fp16_768.onnx",
        "corridorkey_fp16_1024.onnx",
        "corridorkey_int8_512.onnx"
    )
}

function Get-CorridorKeyModelInventory {
    param(
        [string]$ModelsDir,
        [string[]]$ExpectedModels
    )

    $presentModels = @()
    $missingModels = @()
    foreach ($model in $ExpectedModels) {
        $sourcePath = Join-Path $ModelsDir $model
        if (Test-Path $sourcePath) {
            $presentModels += $model
        } else {
            $missingModels += $model
        }
    }

    return [ordered]@{
        expected_models = @($ExpectedModels)
        present_models = @($presentModels)
        missing_models = @($missingModels)
        present_count = @($presentModels).Count
        missing_count = @($missingModels).Count
    }
}

function Write-CorridorKeyJsonFile {
    param(
        [string]$Path,
        [object]$Payload
    )

    $directory = Split-Path -Parent $Path
    if (-not [string]::IsNullOrWhiteSpace($directory) -and -not (Test-Path $directory)) {
        New-Item -ItemType Directory -Path $directory -Force | Out-Null
    }

    $json = $Payload | ConvertTo-Json -Depth 8
    $utf8NoBom = [System.Text.UTF8Encoding]::new($false)
    [System.IO.File]::WriteAllText($Path, $json, $utf8NoBom)
}
