param(
    [ValidateSet("status", "baseline", "graph-off", "host-roundtrip", "clear")]
    [string]$Mode = "status",
    [string]$ResolveExe = "",
    [switch]$LaunchResolve,
    [switch]$ApplyUserEnvironment
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$diagnosticVariables = @(
    "CORRIDORKEY_TRT_CUDA_GRAPH",
    "CORRIDORKEY_TORCHTRT_CUDA_GRAPH",
    "CORRIDORKEY_TORCHTRT_INPUT_BOUNDARY"
)

function Get-ResolveCandidatePath {
    $candidates = @()
    if (-not [string]::IsNullOrWhiteSpace($env:ProgramFiles)) {
        $candidates += Join-Path $env:ProgramFiles "Blackmagic Design\DaVinci Resolve\Resolve.exe"
    }
    $programFilesX86 = [Environment]::GetEnvironmentVariable("ProgramFiles(x86)")
    if (-not [string]::IsNullOrWhiteSpace($programFilesX86)) {
        $candidates += Join-Path $programFilesX86 "Blackmagic Design\DaVinci Resolve\Resolve.exe"
    }

    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            return $candidate
        }
    }
    return ""
}

function Get-ModeEnvironment {
    param([string]$SelectedMode)

    $values = [ordered]@{}
    foreach ($name in $diagnosticVariables) {
        $values[$name] = $null
    }

    switch ($SelectedMode) {
        "baseline" {
            $values["CORRIDORKEY_TRT_CUDA_GRAPH"] = "1"
        }
        "graph-off" {
            $values["CORRIDORKEY_TRT_CUDA_GRAPH"] = "0"
            $values["CORRIDORKEY_TORCHTRT_CUDA_GRAPH"] = "0"
        }
        "host-roundtrip" {
            $values["CORRIDORKEY_TRT_CUDA_GRAPH"] = "0"
            $values["CORRIDORKEY_TORCHTRT_CUDA_GRAPH"] = "0"
            $values["CORRIDORKEY_TORCHTRT_INPUT_BOUNDARY"] = "host_roundtrip"
        }
        "clear" {
        }
        "status" {
            return $null
        }
    }
    return $values
}

function Show-EnvironmentStatus {
    foreach ($name in $diagnosticVariables) {
        [pscustomobject]@{
            name = $name
            process = [Environment]::GetEnvironmentVariable($name, "Process")
            user = [Environment]::GetEnvironmentVariable($name, "User")
            machine = [Environment]::GetEnvironmentVariable($name, "Machine")
        }
    }
}

if ($Mode -eq "status") {
    Show-EnvironmentStatus | Format-Table -AutoSize
    return
}

$modeEnvironment = Get-ModeEnvironment -SelectedMode $Mode

if ($ApplyUserEnvironment) {
    foreach ($name in $diagnosticVariables) {
        [Environment]::SetEnvironmentVariable($name, $modeEnvironment[$name], "User")
    }
    Write-Output "Updated user environment for mode '$Mode'. Restart Resolve before measuring."
    Show-EnvironmentStatus | Format-Table -AutoSize
}

if ($LaunchResolve) {
    if ([string]::IsNullOrWhiteSpace($ResolveExe)) {
        $ResolveExe = Get-ResolveCandidatePath
    }
    if ([string]::IsNullOrWhiteSpace($ResolveExe) -or
        -not (Test-Path -LiteralPath $ResolveExe -PathType Leaf)) {
        throw "Resolve executable not found. Pass -ResolveExe with the full path to Resolve.exe."
    }

    $startInfo = [System.Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $ResolveExe
    $startInfo.UseShellExecute = $false
    foreach ($name in $diagnosticVariables) {
        if ($null -eq $modeEnvironment[$name]) {
            $startInfo.Environment.Remove($name) | Out-Null
        } else {
            $startInfo.Environment[$name] = $modeEnvironment[$name]
        }
    }

    $process = [System.Diagnostics.Process]::Start($startInfo)
    [pscustomobject]@{
        mode = $Mode
        resolve_exe = $ResolveExe
        started_pid = $process.Id
        CORRIDORKEY_TRT_CUDA_GRAPH = $modeEnvironment["CORRIDORKEY_TRT_CUDA_GRAPH"]
        CORRIDORKEY_TORCHTRT_CUDA_GRAPH = $modeEnvironment["CORRIDORKEY_TORCHTRT_CUDA_GRAPH"]
        CORRIDORKEY_TORCHTRT_INPUT_BOUNDARY = $modeEnvironment["CORRIDORKEY_TORCHTRT_INPUT_BOUNDARY"]
    } | Format-List
}

if (-not $ApplyUserEnvironment -and -not $LaunchResolve) {
    [pscustomobject]@{
        mode = $Mode
        CORRIDORKEY_TRT_CUDA_GRAPH = $modeEnvironment["CORRIDORKEY_TRT_CUDA_GRAPH"]
        CORRIDORKEY_TORCHTRT_CUDA_GRAPH = $modeEnvironment["CORRIDORKEY_TORCHTRT_CUDA_GRAPH"]
        CORRIDORKEY_TORCHTRT_INPUT_BOUNDARY = $modeEnvironment["CORRIDORKEY_TORCHTRT_INPUT_BOUNDARY"]
        note = "Use -LaunchResolve for a process-scoped diagnostic launch or -ApplyUserEnvironment before starting Resolve manually."
    } | Format-List
}
