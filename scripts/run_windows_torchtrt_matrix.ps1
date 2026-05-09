param(
    [ValidateSet("smoke", "readiness", "full")]
    [string]$Profile = "readiness",
    [ValidateSet("debug", "release", "release-lto")]
    [string]$Preset = "release",
    [string]$OutputRoot = "",
    [string]$DisplayVersionLabel = "",
    [switch]$SkipBuild,
    [string]$BaselineReport = "",
    [double]$MaxRegressionPercent = 10.0,
    [int]$Iterations = 4,
    [int]$ModelIterations = 5,
    [int]$Warmup = 2,
    [int]$StartPort = 46200,
    [int]$PrepareTimeoutMs = 120000,
    [int]$RequestTimeoutMs = 180000,
    [int]$FrameWidth = 3840,
    [int]$FrameHeight = 2160,
    [int[]]$ModelResolutions = @(512, 1024, 2048),
    [int[]]$RpcResolutions = @(2048),
    [string]$Device = "auto",
    [string]$GreenModel = "",
    [string]$BlueModel = "",
    [string]$RunnerExe = "",
    [string]$RpcHarnessExe = "",
    [string]$ServerBinary = "",
    [string]$TorchTrtBinDir = "",
    [switch]$ListCases
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$buildRoot = Join-Path $repoRoot "build\$Preset"
if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $OutputRoot = Join-Path $buildRoot "torchtrt_matrix"
}
if ([System.IO.Path]::IsPathRooted($OutputRoot)) {
    $OutputRoot = [System.IO.Path]::GetFullPath($OutputRoot)
} else {
    $OutputRoot = [System.IO.Path]::GetFullPath((Join-Path $repoRoot $OutputRoot))
}
$caseRoot = Join-Path $OutputRoot "cases"

function Resolve-CorridorKeyPath {
    param([string]$PathValue)
    if ([System.IO.Path]::IsPathRooted($PathValue)) {
        return [System.IO.Path]::GetFullPath($PathValue)
    }
    return [System.IO.Path]::GetFullPath((Join-Path $repoRoot $PathValue))
}

function Get-GitScalar {
    param([string[]]$Arguments)
    $value = & git -C $repoRoot @Arguments 2>$null
    if ($LASTEXITCODE -ne 0) {
        return ""
    }
    return (($value -join "`n").Trim())
}

function Get-FileSha256 {
    param([string]$PathValue)
    if (-not (Test-Path $PathValue)) {
        return ""
    }
    return (Get-FileHash -Algorithm SHA256 -Path $PathValue).Hash.ToLowerInvariant()
}

function Get-JsonPropertyOrDefault {
    param(
        $Json,
        [string]$Name,
        $DefaultValue = $null
    )
    if ($null -eq $Json -or $null -eq $Json.PSObject.Properties[$Name]) {
        return $DefaultValue
    }
    return $Json.$Name
}

function Invoke-CapturedProcess {
    param(
        [string]$ExePath,
        [string[]]$Arguments,
        [string]$CaseDirectory
    )

    New-Item -ItemType Directory -Force -Path $CaseDirectory | Out-Null
    $stdoutPath = Join-Path $CaseDirectory "stdout.txt"
    $stderrPath = Join-Path $CaseDirectory "stderr.txt"
    $commandPath = Join-Path $CaseDirectory "command.txt"
    $commandParts = @($ExePath) + $Arguments
    Set-Content -Encoding UTF8 -Path $commandPath -Value ($commandParts -join " ")

    $stdoutLines = & $ExePath @Arguments 2> $stderrPath
    $exitCode = $LASTEXITCODE
    $stdoutText = $stdoutLines -join [Environment]::NewLine
    Set-Content -Encoding UTF8 -Path $stdoutPath -Value $stdoutText

    [pscustomobject]@{
        exit_code = $exitCode
        stdout = $stdoutText
        stderr = Get-Content -Raw -ErrorAction SilentlyContinue -Path $stderrPath
        stdout_path = $stdoutPath
        stderr_path = $stderrPath
        command_path = $commandPath
    }
}

function ConvertFrom-JsonText {
    param([string]$JsonText)
    try {
        return $JsonText | ConvertFrom-Json
    } catch {
        return $null
    }
}

function Get-DominantStage {
    param($StageTimings)
    $excluded = @("rpc_prepare_session", "rpc_render_roundtrip")
    $winner = $null
    foreach ($stage in @($StageTimings)) {
        if ($excluded -contains $stage.name) {
            continue
        }
        if ($null -eq $winner -or [double]$stage.total_ms -gt [double]$winner.total_ms) {
            $winner = $stage
        }
    }
    if ($null -eq $winner) {
        return $null
    }
    $name = [string]$winner.name
    $class = "other"
    if ($name -match "tile_infer|run_tiled|tiled") {
        $class = "tiling"
    } elseif ($name -match "torchtrt_forward|ort_run|mlx_run") {
        $class = "model_inference"
    } elseif ($name -match "output|extract|copy_sync|copy_enqueue|unpack") {
        $class = "output_materialization"
    } elseif ($name -match "source_passthrough") {
        $class = "source_passthrough"
    } elseif ($name -match "prepare|input|gpu_prep") {
        $class = "input_prepare"
    } elseif ($name -match "despill|despeckle|composite|premultiply|alpha") {
        $class = "post_process"
    } elseif ($name -match "writeback|transport|shared") {
        $class = "transport_writeback"
    }
    [pscustomobject]@{
        name = $name
        class = $class
        total_ms = [double]$winner.total_ms
        avg_ms = [double]$winner.avg_ms
        sample_count = [int]$winner.sample_count
    }
}

function Get-DominantRenderStage {
    param($ReportJson)
    if ($null -eq $ReportJson -or
        $null -eq $ReportJson.PSObject.Properties["per_frame_timings"] -or
        $null -eq $ReportJson.per_frame_timings) {
        return $null
    }
    $excluded = @(
        "ofx_client_render_rpc",
        "rpc_prepare_session",
        "rpc_render_roundtrip",
        "render_frame",
        "torchtrt_extract_outputs",
        "engine_warmup",
        "engine_warmup_first_run"
    )
    $totals = @{}
    $counts = @{}
    foreach ($frame in @($ReportJson.per_frame_timings)) {
        if ($null -eq $frame.stages) {
            continue
        }
        foreach ($property in $frame.stages.PSObject.Properties) {
            $name = [string]$property.Name
            if ($excluded -contains $name) {
                continue
            }
            if (-not $totals.ContainsKey($name)) {
                $totals[$name] = 0.0
                $counts[$name] = 0
            }
            $totals[$name] += [double]$property.Value
            $counts[$name] += 1
        }
    }
    $winner = $null
    foreach ($name in $totals.Keys) {
        if ($null -eq $winner -or [double]$totals[$name] -gt [double]$winner.total_ms) {
            $winner = [pscustomobject]@{
                name = $name
                total_ms = [double]$totals[$name]
                avg_ms = [double]$totals[$name] / [double]$counts[$name]
                sample_count = [int]$counts[$name]
            }
        }
    }
    if ($null -eq $winner) {
        return $null
    }
    return Get-DominantStage -StageTimings @($winner)
}

function Get-CudaGraphTelemetry {
    param($ReportJson)
    if ($null -eq $ReportJson -or
        $null -eq $ReportJson.PSObject.Properties["stage_timings"] -or
        $null -eq $ReportJson.stage_timings) {
        return $null
    }

    $configStage = ""
    $fallbackEvents = @()
    $replaySamples = 0
    $directForwardSamples = 0
    $warmupSamples = 0
    $captureSamples = 0

    foreach ($stage in @($ReportJson.stage_timings)) {
        $name = [string]$stage.name
        $sampleCount = [int]$stage.sample_count
        if ($name.StartsWith("torchtrt_cuda_graph_config_")) {
            $configStage = $name
        } elseif ($name -eq "torchtrt_cuda_graph_replay") {
            $replaySamples += $sampleCount
        } elseif ($name -eq "torchtrt_forward_direct") {
            $directForwardSamples += $sampleCount
        } elseif ($name -eq "torchtrt_cuda_graph_warmup") {
            $warmupSamples += $sampleCount
        } elseif ($name -eq "torchtrt_cuda_graph_capture") {
            $captureSamples += $sampleCount
        } elseif ($name.StartsWith("torchtrt_cuda_graph_fallback_")) {
            $fallbackEvents += [pscustomobject]@{
                name = $name
                sample_count = $sampleCount
            }
        }
    }

    [pscustomobject]@{
        config_stage = $configStage
        warmup_samples = $warmupSamples
        capture_samples = $captureSamples
        replay_samples = $replaySamples
        direct_forward_samples = $directForwardSamples
        fallback_events = $fallbackEvents
    }
}

function Parse-RunnerOutput {
    param([string]$Stdout)
    $pattern = "forward mean=(?<mean>[0-9.]+) ms p50=(?<p50>[0-9.]+) ms p99=(?<p99>[0-9.]+) ms\s+alpha=\[(?<amin>-?[0-9.]+), (?<amax>-?[0-9.]+)\]\s+nan=(?<nan>true|false) inf=(?<inf>true|false)\s+iters=(?<iters>[0-9]+)"
    $match = [regex]::Match($Stdout, $pattern)
    if (-not $match.Success) {
        return $null
    }
    [pscustomobject]@{
        mean_ms = [double]$match.Groups["mean"].Value
        p50_ms = [double]$match.Groups["p50"].Value
        p99_ms = [double]$match.Groups["p99"].Value
        alpha_min = [double]$match.Groups["amin"].Value
        alpha_max = [double]$match.Groups["amax"].Value
        has_nan = $match.Groups["nan"].Value -eq "true"
        has_inf = $match.Groups["inf"].Value -eq "true"
        iterations = [int]$match.Groups["iters"].Value
        external_positional_metadata = $Stdout.Contains("external positional embedding metadata loaded")
        tensorrt_marker = $Stdout.Contains("LoadLibrary torchtrt.dll succeeded")
    }
}

function New-RpcCase {
    param(
        [string]$Id,
        [string]$ModelColor,
        [string]$ScreenColor,
        [int]$Resolution,
        [bool]$SourcePassthrough = $false,
        [bool]$OutputAlphaOnly = $false,
        [string]$Upscale = "lanczos4",
        [bool]$Despeckle = $false,
        [int]$DespeckleSize = 400,
        [bool]$EnableTiling = $false,
        [int]$SpErode = 3,
        [int]$SpBlur = 7,
        [string]$InputMode = "constant",
        [uint32]$InputSeed = 3233259968
    )
    [pscustomobject]@{
        case_id = $Id
        model_color = $ModelColor
        screen_color_mode = $ScreenColor
        resolution = $Resolution
        source_passthrough = $SourcePassthrough
        output_alpha_only = $OutputAlphaOnly
        upscale = $Upscale
        despeckle = $Despeckle
        despeckle_size = $DespeckleSize
        enable_tiling = $EnableTiling
        sp_erode = $SpErode
        sp_blur = $SpBlur
        input_mode = $InputMode
        input_seed = $InputSeed
    }
}

function Get-RpcCases {
    param([string]$SelectedProfile, [int[]]$Resolutions)
    $cases = @()
    foreach ($resolution in $Resolutions) {
        $cases += New-RpcCase -Id "green_alpha_only_${resolution}" -ModelColor "green" `
            -ScreenColor "green" -Resolution $resolution -OutputAlphaOnly $true
        $cases += New-RpcCase -Id "green_processed_lanczos_${resolution}" -ModelColor "green" `
            -ScreenColor "green" -Resolution $resolution
        if ($SelectedProfile -ne "smoke") {
            $cases += New-RpcCase -Id "green_source_passthrough_${resolution}" `
                -ModelColor "green" -ScreenColor "green" -Resolution $resolution `
                -SourcePassthrough $true
            $cases += New-RpcCase -Id "green_source_passthrough_plate_${resolution}" `
                -ModelColor "green" -ScreenColor "green" -Resolution $resolution `
                -SourcePassthrough $true -InputMode "plate" -InputSeed 18002
            $cases += New-RpcCase -Id "green_source_passthrough_heavy_${resolution}" `
                -ModelColor "green" -ScreenColor "green" -Resolution $resolution `
                -SourcePassthrough $true -SpErode 6 -SpBlur 14
            $cases += New-RpcCase -Id "green_source_passthrough_heavy_plate_${resolution}" `
                -ModelColor "green" -ScreenColor "green" -Resolution $resolution `
                -SourcePassthrough $true -SpErode 6 -SpBlur 14 -InputMode "plate" `
                -InputSeed 18003
            $cases += New-RpcCase -Id "green_processed_bilinear_${resolution}" `
                -ModelColor "green" -ScreenColor "green" -Resolution $resolution -Upscale "bilinear"
            $cases += New-RpcCase -Id "green_despeckle_${resolution}" -ModelColor "green" `
                -ScreenColor "green" -Resolution $resolution -Despeckle $true
            $cases += New-RpcCase -Id "green_despeckle_plate_${resolution}" -ModelColor "green" `
                -ScreenColor "green" -Resolution $resolution -Despeckle $true `
                -InputMode "plate" -InputSeed 18004
            $cases += New-RpcCase -Id "green_despeckle_random_${resolution}" -ModelColor "green" `
                -ScreenColor "green" -Resolution $resolution -Despeckle $true `
                -InputMode "random" -InputSeed 18005
            $cases += New-RpcCase -Id "green_tiled_${resolution}" -ModelColor "green" `
                -ScreenColor "green" -Resolution $resolution -EnableTiling $true
            $cases += New-RpcCase -Id "blue_source_passthrough_${resolution}" `
                -ModelColor "blue" -ScreenColor "blue" -Resolution $resolution `
                -SourcePassthrough $true
            $cases += New-RpcCase -Id "blue_source_passthrough_plate_${resolution}" `
                -ModelColor "blue" -ScreenColor "blue" -Resolution $resolution `
                -SourcePassthrough $true -InputMode "plate" -InputSeed 18007
        }
        $cases += New-RpcCase -Id "blue_processed_lanczos_${resolution}" -ModelColor "blue" `
            -ScreenColor "blue" -Resolution $resolution
        if ($SelectedProfile -ne "smoke") {
            $cases += New-RpcCase -Id "green_processed_lanczos_plate_${resolution}" `
                -ModelColor "green" -ScreenColor "green" -Resolution $resolution `
                -InputMode "plate" -InputSeed 18001
            $cases += New-RpcCase -Id "blue_processed_lanczos_plate_${resolution}" `
                -ModelColor "blue" -ScreenColor "blue" -Resolution $resolution `
                -InputMode "plate" -InputSeed 18006
        }
        $cases += New-RpcCase -Id "blue_green_swap_${resolution}" -ModelColor "green" `
            -ScreenColor "blue_green" -Resolution $resolution
        if ($SelectedProfile -ne "smoke") {
            $cases += New-RpcCase -Id "blue_green_swap_plate_${resolution}" `
                -ModelColor "green" -ScreenColor "blue_green" -Resolution $resolution `
                -InputMode "plate" -InputSeed 18008
        }
        if ($SelectedProfile -eq "full") {
            $cases += New-RpcCase -Id "green_random_input_${resolution}" -ModelColor "green" `
                -ScreenColor "green" -Resolution $resolution -InputMode "random"
            $cases += New-RpcCase -Id "blue_random_input_${resolution}" -ModelColor "blue" `
                -ScreenColor "blue" -Resolution $resolution -InputMode "random"
        }
    }
    return $cases
}

$selectedRpcCases = @(Get-RpcCases -SelectedProfile $Profile -Resolutions $RpcResolutions)

if ($Profile -ne "smoke") {
    $nonConstantCases = @($selectedRpcCases | Where-Object { $_.input_mode -ne "constant" })
    $despeckleNonConstantCases = @(
        $selectedRpcCases | Where-Object { $_.despeckle -and $_.input_mode -ne "constant" }
    )
    $greenSourcePassthroughNonConstantCases = @(
        $selectedRpcCases | Where-Object {
            $_.model_color -eq "green" -and $_.source_passthrough -and
            $_.input_mode -ne "constant"
        }
    )
    $blueNonConstantCases = @(
        $selectedRpcCases | Where-Object {
            $_.model_color -eq "blue" -and $_.input_mode -ne "constant"
        }
    )
    $blueGreenNonConstantCases = @(
        $selectedRpcCases | Where-Object {
            $_.screen_color_mode -eq "blue_green" -and $_.input_mode -ne "constant"
        }
    )

    if ($nonConstantCases.Count -eq 0) {
        throw "Readiness RPC matrix must include non-constant input cases."
    }
    if ($despeckleNonConstantCases.Count -eq 0) {
        throw "Readiness RPC matrix must cover auto_despeckle with non-constant input."
    }
    if ($greenSourcePassthroughNonConstantCases.Count -eq 0) {
        throw "Readiness RPC matrix must cover green source_passthrough with non-constant input."
    }
    if ($blueNonConstantCases.Count -eq 0) {
        throw "Readiness RPC matrix must cover blue model cases with non-constant input."
    }
    if ($blueGreenNonConstantCases.Count -eq 0) {
        throw "Readiness RPC matrix must cover blue-green channel swap with non-constant input."
    }
}

if ($ListCases) {
    [pscustomobject]@{
        profile = $Profile
        rpc_resolutions = $RpcResolutions
        rpc_cases = $selectedRpcCases
    } | ConvertTo-Json -Depth 8
    exit 0
}

New-Item -ItemType Directory -Force -Path $caseRoot | Out-Null

if (-not $SkipBuild) {
    $buildArgs = @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File",
        (Join-Path $PSScriptRoot "windows.ps1"), "-Task", "build", "-Preset", $Preset)
    if (-not [string]::IsNullOrWhiteSpace($DisplayVersionLabel)) {
        $buildArgs += @("-DisplayVersionLabel", $DisplayVersionLabel)
    }
    & powershell.exe @buildArgs
    if ($LASTEXITCODE -ne 0) {
        throw "Canonical Windows build failed."
    }
}

if ([string]::IsNullOrWhiteSpace($GreenModel)) {
    $GreenModel = "models\corridorkey_dynamic_green_fp16.ts"
}
if ([string]::IsNullOrWhiteSpace($BlueModel)) {
    $BlueModel = "models\corridorkey_dynamic_blue_fp16.ts"
}
if ([string]::IsNullOrWhiteSpace($RunnerExe)) {
    $RunnerExe = Join-Path $buildRoot "tools\torchtrt_runner\corridorkey-torchtrt-runner.exe"
}
if ([string]::IsNullOrWhiteSpace($RpcHarnessExe)) {
    $RpcHarnessExe = Join-Path $buildRoot "tests\integration\ofx_rpc_benchmark_harness.exe"
}
if ([string]::IsNullOrWhiteSpace($ServerBinary)) {
    $ServerBinary = Join-Path $buildRoot "src\plugins\ofx\corridorkey_ofx_runtime_server.exe"
}
if ([string]::IsNullOrWhiteSpace($TorchTrtBinDir)) {
    $TorchTrtBinDir = "vendor\torchtrt-windows\bin"
}

$GreenModel = Resolve-CorridorKeyPath $GreenModel
$BlueModel = Resolve-CorridorKeyPath $BlueModel
$RunnerExe = Resolve-CorridorKeyPath $RunnerExe
$RpcHarnessExe = Resolve-CorridorKeyPath $RpcHarnessExe
$ServerBinary = Resolve-CorridorKeyPath $ServerBinary
$TorchTrtBinDir = Resolve-CorridorKeyPath $TorchTrtBinDir

$failures = @()
foreach ($required in @($RunnerExe, $RpcHarnessExe, $ServerBinary, $TorchTrtBinDir, $GreenModel, $BlueModel)) {
    if (-not (Test-Path $required)) {
        $failures += "missing path: $required"
    }
}

$commit = Get-GitScalar @("rev-parse", "HEAD")
$shortCommit = Get-GitScalar @("rev-parse", "--short", "HEAD")
$dirtyState = Get-GitScalar @("status", "--porcelain")
$modelCases = @()
$rpcCases = @()

foreach ($model in @(
        [pscustomobject]@{ color = "green"; path = $GreenModel; requires_external_pos = $true },
        [pscustomobject]@{ color = "blue"; path = $BlueModel; requires_external_pos = $false }
    )) {
    foreach ($resolution in $ModelResolutions) {
        $caseId = "model_$($model.color)_$resolution"
        $caseDirectory = Join-Path $caseRoot $caseId
        $result = Invoke-CapturedProcess -ExePath $RunnerExe -CaseDirectory $caseDirectory -Arguments @(
            "--ts", $model.path,
            "--resolution", "$resolution",
            "--iterations", "$ModelIterations",
            "--warmup", "$Warmup",
            "--bin-dir", $TorchTrtBinDir
        )
        $parsed = Parse-RunnerOutput $result.stdout
        $success = $result.exit_code -eq 0 -and $null -ne $parsed
        if ($success -and $model.requires_external_pos -and -not $parsed.external_positional_metadata) {
            $success = $false
        }
        if ($success -and ($parsed.has_nan -or $parsed.has_inf)) {
            $success = $false
        }
        if (-not $success) {
            $failures += "$caseId failed"
        }
        $modelCases += [pscustomobject]@{
            case_id = $caseId
            success = $success
            color = $model.color
            model_path = $model.path
            model_sha256 = Get-FileSha256 $model.path
            resolution = $resolution
            exit_code = $result.exit_code
            metrics = $parsed
            stdout_path = $result.stdout_path
            stderr_path = $result.stderr_path
            command_path = $result.command_path
        }
    }
}

$port = $StartPort
foreach ($case in $selectedRpcCases) {
    $modelPath = if ($case.model_color -eq "blue") { $BlueModel } else { $GreenModel }
    $caseDirectory = Join-Path $caseRoot $case.case_id
    $arguments = @(
        "--server-binary", $ServerBinary,
        "--model", $modelPath,
        "--device", $Device,
        "--resolution", "$($case.resolution)",
        "--frame-width", "$FrameWidth",
        "--frame-height", "$FrameHeight",
        "--iterations", "$Iterations",
        "--endpoint-port", "$port",
        "--prepare-timeout-ms", "$PrepareTimeoutMs",
        "--request-timeout-ms", "$RequestTimeoutMs",
        "--screen-color-mode", $case.screen_color_mode,
        "--input-mode", $case.input_mode,
        "--input-seed", "$($case.input_seed)",
        "--source-passthrough", "$($case.source_passthrough.ToString().ToLowerInvariant())",
        "--output-alpha-only", "$($case.output_alpha_only.ToString().ToLowerInvariant())",
        "--upscale", $case.upscale,
        "--despeckle", "$($case.despeckle.ToString().ToLowerInvariant())",
        "--despeckle-size", "$($case.despeckle_size)",
        "--enable-tiling", "$($case.enable_tiling.ToString().ToLowerInvariant())",
        "--sp-erode", "$($case.sp_erode)",
        "--sp-blur", "$($case.sp_blur)"
    )
    $port += 1
    $result = Invoke-CapturedProcess -ExePath $RpcHarnessExe -Arguments $arguments `
        -CaseDirectory $caseDirectory
    $json = ConvertFrom-JsonText $result.stdout
    $dominant = $null
    $cudaGraph = $null
    if ($null -ne $json) {
        $dominant = Get-DominantRenderStage $json
        $cudaGraph = Get-CudaGraphTelemetry $json
    }
    $success = $result.exit_code -eq 0 -and $null -ne $json -and $json.success -eq $true
    if ($success -and (Get-JsonPropertyOrDefault $json "render_session_identity_stable" $false) -ne $true) {
        $success = $false
    }
    if ($success -and $null -ne $cudaGraph -and
        $cudaGraph.config_stage -eq "torchtrt_cuda_graph_config_enabled" -and
        [int]$cudaGraph.replay_samples -eq 0 -and @($cudaGraph.fallback_events).Count -eq 0) {
        $success = $false
        $failures += "$($case.case_id) missing TorchTRT CUDA graph replay/fallback telemetry"
    }
    if (-not $success) {
        $failures += "$($case.case_id) failed"
    }
    $rpcCases += [pscustomobject]@{
        case_id = $case.case_id
        success = $success
        model_color = $case.model_color
        model_path = $modelPath
        model_sha256 = Get-FileSha256 $modelPath
        screen_color_mode = $case.screen_color_mode
        resolution = $case.resolution
        frame_width = $FrameWidth
        frame_height = $FrameHeight
        iterations = $Iterations
        exit_code = $result.exit_code
        avg_latency_ms = Get-JsonPropertyOrDefault $json "avg_latency_ms"
        fps = Get-JsonPropertyOrDefault $json "fps"
        render_session_identity_stable = Get-JsonPropertyOrDefault $json "render_session_identity_stable" $false
        input_seed = $case.input_seed
        params = [pscustomobject]@{
            source_passthrough = $case.source_passthrough
            output_alpha_only = $case.output_alpha_only
            upscale = $case.upscale
            despeckle = $case.despeckle
            despeckle_size = $case.despeckle_size
            enable_tiling = $case.enable_tiling
            sp_erode = $case.sp_erode
            sp_blur = $case.sp_blur
            input_mode = $case.input_mode
            input_seed = $case.input_seed
        }
        dominant_stage = $dominant
        cuda_graph = $cudaGraph
        raw_json_path = $result.stdout_path
        stderr_path = $result.stderr_path
        command_path = $result.command_path
    }
}

$regressions = @()
if (-not [string]::IsNullOrWhiteSpace($BaselineReport)) {
    $baselinePath = Resolve-CorridorKeyPath $BaselineReport
    if (-not (Test-Path $baselinePath)) {
        $failures += "baseline report not found: $baselinePath"
    } else {
        $baseline = Get-Content -Raw -Path $baselinePath | ConvertFrom-Json
        $baselineByCase = @{}
        foreach ($case in @($baseline.rpc_cases)) {
            $baselineByCase[$case.case_id] = $case
        }
        foreach ($case in $rpcCases) {
            if (-not $baselineByCase.ContainsKey($case.case_id)) {
                continue
            }
            $before = [double]$baselineByCase[$case.case_id].avg_latency_ms
            $after = [double]$case.avg_latency_ms
            if ($before -le 0.0 -or $after -le 0.0) {
                continue
            }
            $delta = (($after - $before) / $before) * 100.0
            if ($delta -gt $MaxRegressionPercent) {
                $regressions += [pscustomobject]@{
                    case_id = $case.case_id
                    before_avg_latency_ms = $before
                    after_avg_latency_ms = $after
                    regression_percent = $delta
                }
                $failures += "$($case.case_id) regressed by $([math]::Round($delta, 1))%"
            }
        }
    }
}

$bottleneckCounts = @{}
foreach ($case in $rpcCases) {
    if ($null -eq $case.dominant_stage) {
        continue
    }
    $key = $case.dominant_stage.class
    if (-not $bottleneckCounts.ContainsKey($key)) {
        $bottleneckCounts[$key] = 0
    }
    $bottleneckCounts[$key] += 1
}

$report = [pscustomobject]@{
    schema_version = 1
    profile = $Profile
    preset = $Preset
    commit = $commit
    short_commit = $shortCommit
    dirty = -not [string]::IsNullOrWhiteSpace($dirtyState)
    paths = [pscustomobject]@{
        output_root = $OutputRoot
        runner_exe = $RunnerExe
        rpc_harness_exe = $RpcHarnessExe
        server_binary = $ServerBinary
        torchtrt_bin_dir = $TorchTrtBinDir
    }
    coverage = [pscustomobject]@{
        runner = @("artifact_load", "forward_finiteness", "p50_p99", "external_positional_metadata")
        rpc_runtime_params = @(
            "screen_color_mode",
            "source_passthrough",
            "output_alpha_only",
            "upscale_method",
            "despill_strength",
            "despill_screen_channel",
            "auto_despeckle",
            "enable_tiling",
            "input_mode",
            "torchtrt_cuda_graph_replay_or_fallback"
        )
        host_layer_params = @(
            [pscustomobject]@{ name = "alpha_black_point"; coverage = "ofx_host_or_manual_uat" },
            [pscustomobject]@{ name = "alpha_white_point"; coverage = "ofx_host_or_manual_uat" },
            [pscustomobject]@{ name = "alpha_erode"; coverage = "ofx_host_or_manual_uat" },
            [pscustomobject]@{ name = "alpha_softness"; coverage = "ofx_host_or_manual_uat" },
            [pscustomobject]@{ name = "alpha_gamma"; coverage = "ofx_host_or_manual_uat" },
            [pscustomobject]@{ name = "temporal_smoothing"; coverage = "ofx_host_or_manual_uat" },
            [pscustomobject]@{ name = "output_mode_writeback"; coverage = "ofx_host_or_manual_uat" }
        )
    }
    thresholds = [pscustomobject]@{
        max_regression_percent = $MaxRegressionPercent
        baseline_report = $BaselineReport
    }
    summary = [pscustomobject]@{
        success = $failures.Count -eq 0
        failure_count = $failures.Count
        failures = $failures
        regression_count = $regressions.Count
        bottleneck_counts = $bottleneckCounts
    }
    model_cases = $modelCases
    rpc_cases = $rpcCases
    regressions = $regressions
}

$reportPath = Join-Path $OutputRoot "torchtrt_matrix_report.json"
$report | ConvertTo-Json -Depth 12 | Set-Content -Encoding UTF8 -Path $reportPath
Write-Host "TorchTRT matrix report: $reportPath" -ForegroundColor Green
if ($failures.Count -gt 0) {
    Write-Host "Failures:" -ForegroundColor Red
    foreach ($failure in $failures) {
        Write-Host "  $failure" -ForegroundColor Red
    }
    exit 1
}
exit 0
