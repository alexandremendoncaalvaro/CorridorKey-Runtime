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

function Assert-CorridorKeySemVer {
    param([string]$Version)

    if ([string]::IsNullOrWhiteSpace($Version) -or
        $Version -notmatch '^[0-9]+\.[0-9]+\.[0-9]+$') {
        throw "Version must use SemVer MAJOR.MINOR.PATCH. Received: $Version"
    }
}

function Set-CorridorKeyProjectVersion {
    param(
        [string]$RepoRoot,
        [string]$Version
    )

    Assert-CorridorKeySemVer -Version $Version

    $cmakePath = Join-Path $RepoRoot "CMakeLists.txt"
    if (-not (Test-Path $cmakePath)) {
        throw "Could not update project version because CMakeLists.txt was not found at $cmakePath"
    }

    $pattern = '^(?<prefix>\s*VERSION\s+)(?<version>[0-9]+\.[0-9]+\.[0-9]+)(?<suffix>\s*)$'
    $regex = [System.Text.RegularExpressions.Regex]::new(
        $pattern,
        [System.Text.RegularExpressions.RegexOptions]::Multiline
    )
    $utf8NoBom = [System.Text.UTF8Encoding]::new($false)
    $content = [System.IO.File]::ReadAllText($cmakePath, $utf8NoBom)
    $updated = $regex.Replace(
        $content,
        [System.Text.RegularExpressions.MatchEvaluator]{
            param($match)
            return $match.Groups["prefix"].Value + $Version + $match.Groups["suffix"].Value
        },
        1
    )

    if ($content -eq $updated) {
        $currentVersion = Get-CorridorKeyProjectVersion -RepoRoot $RepoRoot
        if ($currentVersion -eq $Version) {
            return $Version
        }
        throw "Could not update VERSION in $cmakePath"
    }

    [System.IO.File]::WriteAllText($cmakePath, $updated, $utf8NoBom)
    return $Version
}

function Set-CorridorKeyTextVersionField {
    param(
        [string]$Path,
        [string]$Pattern,
        [string]$Version,
        [string]$Description
    )

    if (-not (Test-Path $Path)) {
        throw "Could not update $Description because the file was not found at $Path"
    }

    $regex = [System.Text.RegularExpressions.Regex]::new(
        $Pattern,
        [System.Text.RegularExpressions.RegexOptions]::Multiline
    )
    $utf8NoBom = [System.Text.UTF8Encoding]::new($false)
    $content = [System.IO.File]::ReadAllText($Path, $utf8NoBom)
    $updated = $regex.Replace(
        $content,
        [System.Text.RegularExpressions.MatchEvaluator]{
            param($match)
            return $match.Groups["prefix"].Value + $Version + $match.Groups["suffix"].Value
        },
        1
    )

    if ($content -eq $updated) {
        $match = $regex.Match($content)
        if ($match.Success -and $match.Groups["version"].Value -eq $Version) {
            return
        }
        throw "Could not update $Description in $Path"
    }

    [System.IO.File]::WriteAllText($Path, $updated, $utf8NoBom)
}

function Sync-CorridorKeyGuiVersionMetadata {
    param(
        [string]$RepoRoot,
        [string]$Version
    )

    Assert-CorridorKeySemVer -Version $Version

    $guiRoot = Join-Path $RepoRoot "src\gui"
    Set-CorridorKeyTextVersionField `
        -Path (Join-Path $guiRoot "package.json") `
        -Pattern '^(?<prefix>\s*"version"\s*:\s*")(?<version>[0-9]+\.[0-9]+\.[0-9]+)(?<suffix>".*)$' `
        -Version $Version `
        -Description "GUI package version"

    Set-CorridorKeyTextVersionField `
        -Path (Join-Path $guiRoot "src-tauri\tauri.conf.json") `
        -Pattern '^(?<prefix>\s*"version"\s*:\s*")(?<version>[0-9]+\.[0-9]+\.[0-9]+)(?<suffix>".*)$' `
        -Version $Version `
        -Description "Tauri app version"

    Set-CorridorKeyTextVersionField `
        -Path (Join-Path $guiRoot "src-tauri\Cargo.toml") `
        -Pattern '^(?<prefix>version\s*=\s*")(?<version>[0-9]+\.[0-9]+\.[0-9]+)(?<suffix>"\s*)$' `
        -Version $Version `
        -Description "Tauri Cargo package version"

    return $Version
}

function Initialize-CorridorKeyVersion {
    param(
        [string]$RepoRoot,
        [string]$Version = "",
        [switch]$SyncGuiMetadata
    )

    $resolvedVersion = $Version
    $shouldSyncGuiMetadata = $SyncGuiMetadata.IsPresent -or
        (-not [string]::IsNullOrWhiteSpace($Version))

    if ([string]::IsNullOrWhiteSpace($resolvedVersion)) {
        $resolvedVersion = Get-CorridorKeyProjectVersion -RepoRoot $RepoRoot
    } else {
        $resolvedVersion = Set-CorridorKeyProjectVersion -RepoRoot $RepoRoot -Version $resolvedVersion
    }

    if ($shouldSyncGuiMetadata) {
        Sync-CorridorKeyGuiVersionMetadata -RepoRoot $RepoRoot -Version $resolvedVersion | Out-Null
    }

    return $resolvedVersion
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

function Get-CorridorKeyWindowsOrtBinaryVersion {
    param(
        [string]$RepoRoot,
        [ValidateSet("rtx", "dml")]
        [string]$Track
    )

    $ortRoot = Get-CorridorKeyWindowsOrtRootPath -RepoRoot $RepoRoot -Track $Track
    $candidates = @(
        (Join-Path $ortRoot "bin\onnxruntime.dll"),
        (Join-Path $ortRoot "onnxruntime.dll")
    )

    foreach ($candidate in $candidates) {
        if (Test-Path $candidate) {
            $productVersion = (Get-Item $candidate).VersionInfo.ProductVersion
            if (-not [string]::IsNullOrWhiteSpace($productVersion)) {
                return $productVersion
            }
        }
    }

    throw "Unable to determine the curated ONNX Runtime version for the '$Track' track from $ortRoot. Stage the curated runtime first or pass -OrtVersion explicitly."
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
