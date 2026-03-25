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
