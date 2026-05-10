param(
    [string]$LogDir = (Join-Path $env:LOCALAPPDATA "CorridorKey\Logs"),
    [int]$TailSummaries = 20,
    [string]$SinceLocalTime = "",
    [Alias("Pid")]
    [string]$PluginPid = "",
    [string]$WorkOrigin = "",
    [double]$InputReadyWaitBudgetMs = 5.0,
    [double]$InputCopyQueueWaitBudgetMs = 5.0,
    [double]$GpuPrepareWaitBudgetMs = 5.0
)

$ErrorActionPreference = "Stop"

function Parse-KeyValueLine {
    param([string]$Line)

    $values = @{}
    $timestamp = Get-LogTimestamp -Line $Line
    if ($timestamp -ne $null) {
        $values["timestamp"] = $timestamp.ToString("yyyy-MM-dd HH:mm:ss", [System.Globalization.CultureInfo]::InvariantCulture)
    }
    $pidMatch = [regex]::Match($Line, "pid=(?<pid>[0-9]+)")
    if ($pidMatch.Success) {
        $values["pid"] = $pidMatch.Groups["pid"].Value
    }
    $tidMatch = [regex]::Match($Line, "tid=(?<tid>[0-9]+)")
    if ($tidMatch.Success) {
        $values["tid"] = $tidMatch.Groups["tid"].Value
    }
    foreach ($part in ($Line -split "\s+")) {
        $match = [regex]::Match($part, "^(?<key>[A-Za-z0-9_]+)=(?<value>[-+0-9.eE]+|[^ ]+)$")
        if (-not $match.Success) {
            continue
        }
        $key = $match.Groups["key"].Value
        $rawValue = $match.Groups["value"].Value
        $numeric = 0.0
        if ([double]::TryParse(
                $rawValue,
                [System.Globalization.NumberStyles]::Float,
                [System.Globalization.CultureInfo]::InvariantCulture,
                [ref]$numeric)) {
            $values[$key] = $numeric
        } else {
            $values[$key] = $rawValue
        }
    }
    return $values
}

function Get-LogTimestamp {
    param([string]$Line)

    $match = [regex]::Match($Line, "^(?<timestamp>[0-9]{4}-[0-9]{2}-[0-9]{2} [0-9]{2}:[0-9]{2}:[0-9]{2})")
    if (-not $match.Success) {
        return $null
    }
    $parsed = [datetime]::MinValue
    if ([datetime]::TryParseExact(
            $match.Groups["timestamp"].Value,
            "yyyy-MM-dd HH:mm:ss",
            [System.Globalization.CultureInfo]::InvariantCulture,
            [System.Globalization.DateTimeStyles]::AssumeLocal,
            [ref]$parsed)) {
        return $parsed
    }
    return $null
}

function Count-Field {
    param(
        [object[]]$Rows,
        [string]$Name
    )

    $counts = [ordered]@{}
    foreach ($row in $Rows) {
        if (-not $row.ContainsKey($Name)) {
            continue
        }
        $key = [string]$row[$Name]
        if ($counts.Contains($key)) {
            $counts[$key] = $counts[$key] + 1
        } else {
            $counts[$key] = 1
        }
    }
    return $counts
}

function Count-FieldPresence {
    param(
        [object[]]$Rows,
        [string]$Name
    )

    $count = 0
    foreach ($row in $Rows) {
        if ($row.ContainsKey($Name)) {
            $count += 1
        }
    }
    return $count
}

function Count-FlagSet {
    param(
        [object[]]$Rows,
        [string]$Name
    )

    $count = 0
    foreach ($row in $Rows) {
        if (-not $row.ContainsKey($Name)) {
            continue
        }
        if ($row[$Name] -is [double] -and $row[$Name] -ne 0.0) {
            $count += 1
        } elseif ($row[$Name] -is [string] -and $row[$Name] -eq "1") {
            $count += 1
        }
    }
    return $count
}

function Average-Field {
    param(
        [object[]]$Rows,
        [string]$Name
    )

    $values = @(
        foreach ($row in $Rows) {
            if ($row.ContainsKey($Name) -and $row[$Name] -is [double]) {
                $row[$Name]
            }
        }
    )
    if ($values.Count -eq 0) {
        return 0.0
    }
    return (($values | Measure-Object -Average).Average)
}

function Max-Field {
    param(
        [object[]]$Rows,
        [string]$Name
    )

    $values = @(
        foreach ($row in $Rows) {
            if ($row.ContainsKey($Name) -and $row[$Name] -is [double]) {
                $row[$Name]
            }
        }
    )
    if ($values.Count -eq 0) {
        return 0.0
    }
    return (($values | Measure-Object -Maximum).Maximum)
}

function Stats-Field {
    param(
        [object[]]$Rows,
        [string]$Name
    )

    $values = @(
        foreach ($row in $Rows) {
            if ($row.ContainsKey($Name) -and $row[$Name] -is [double]) {
                $row[$Name]
            }
        }
    )
    if ($values.Count -eq 0) {
        return [ordered]@{
            count = 0
            average_ms = 0.0
            maximum_ms = 0.0
        }
    }
    return [ordered]@{
        count = $values.Count
        average_ms = (($values | Measure-Object -Average).Average)
        maximum_ms = (($values | Measure-Object -Maximum).Maximum)
    }
}

function Parse-RuntimeDetailLine {
    param([string]$Line)

    $values = Parse-KeyValueLine -Line $Line
    $stagesMatch = [regex]::Match($Line, "stages=(?<stages>.+)$")
    if (-not $stagesMatch.Success) {
        return $values
    }

    foreach ($part in ($stagesMatch.Groups["stages"].Value -split ",")) {
        $stageMatch = [regex]::Match($part.Trim(), "^(?<key>[A-Za-z0-9_]+):(?<value>[-+0-9.eE]+)$")
        if (-not $stageMatch.Success) {
            continue
        }
        $numeric = 0.0
        if (-not [double]::TryParse(
                $stageMatch.Groups["value"].Value,
                [System.Globalization.NumberStyles]::Float,
                [System.Globalization.CultureInfo]::InvariantCulture,
                [ref]$numeric)) {
            continue
        }
        $key = $stageMatch.Groups["key"].Value
        if ($values.ContainsKey($key) -and $values[$key] -is [double]) {
            $values[$key] = $values[$key] + $numeric
        } else {
            $values[$key] = $numeric
        }
    }
    return $values
}

if (-not (Test-Path -LiteralPath $LogDir -PathType Container)) {
    throw "Log directory not found: $LogDir"
}

$ofxLog = Join-Path $LogDir "ofx.log"
if (-not (Test-Path -LiteralPath $ofxLog -PathType Leaf)) {
    throw "OFX log not found: $ofxLog"
}

$sinceFilterActive = $false
[datetime]$sinceTimestamp = [datetime]::MinValue
if (-not [string]::IsNullOrWhiteSpace($SinceLocalTime)) {
    $sinceFilterActive = $true
    if (-not [datetime]::TryParseExact(
            $SinceLocalTime,
            "yyyy-MM-dd HH:mm:ss",
            [System.Globalization.CultureInfo]::InvariantCulture,
            [System.Globalization.DateTimeStyles]::AssumeLocal,
            [ref]$sinceTimestamp)) {
        throw "SinceLocalTime must use format yyyy-MM-dd HH:mm:ss"
    }
}

$serverLog = Get-ChildItem -LiteralPath $LogDir -Filter "ofx_runtime_server*.log" |
    Sort-Object LastWriteTime -Descending |
    Select-Object -First 1

$runtimeDisplayVersion = ""
$runtimeEnvironment = [ordered]@{}
if ($serverLog -ne $null) {
    $serverStart = Select-String -LiteralPath $serverLog.FullName -Pattern "event=server_start" |
        Select-Object -Last 1
    if ($serverStart -ne $null) {
        $serverStartValues = Parse-KeyValueLine -Line $serverStart.Line
        if ($serverStartValues.ContainsKey("display_version")) {
            $runtimeDisplayVersion = [string]$serverStartValues["display_version"]
        }
        foreach ($name in @("pid", "cuda_graph_env", "torchtrt_cuda_graph_env", "io_binding_env", "torchtrt_input_boundary", "torchtrt_forward_sync_timing_env")) {
            if ($serverStartValues.ContainsKey($name)) {
                $runtimeEnvironment[$name] = $serverStartValues[$name]
            }
        }
    }
}

$allSummaryMatches = Select-String -LiteralPath $ofxLog -Pattern "event=ofx_render_summary"
if ($sinceFilterActive) {
    $allSummaryMatches = @(
        $allSummaryMatches | Where-Object {
            $lineTimestamp = Get-LogTimestamp -Line $_.Line
            $lineTimestamp -ne $null -and $lineTimestamp -ge $sinceTimestamp
        }
    )
}
if (-not [string]::IsNullOrWhiteSpace($PluginPid)) {
    $pidPattern = "pid=$([regex]::Escape($PluginPid))([^0-9]|$)"
    $allSummaryMatches = @(
        $allSummaryMatches | Where-Object { $_.Line -match $pidPattern }
    )
}
if (-not [string]::IsNullOrWhiteSpace($WorkOrigin)) {
    $workOriginPattern = "work_origin=$([regex]::Escape($WorkOrigin))(\s|$)"
    $allSummaryMatches = @(
        $allSummaryMatches | Where-Object { $_.Line -match $workOriginPattern }
    )
}
$summaryMatches = $allSummaryMatches | Select-Object -Last $TailSummaries
$summaries = @(
    foreach ($match in $summaryMatches) {
        Parse-KeyValueLine -Line $match.Line
    }
)

$runtimeDetails = @()
if ($serverLog -ne $null -and $runtimeEnvironment.Contains("pid")) {
    $runtimePid = [string]$runtimeEnvironment["pid"]
    $serverStartPattern = "event=server_start .*pid=$([regex]::Escape($runtimePid))(\s|$)"
    $selectedServerStart = Select-String -LiteralPath $serverLog.FullName -Pattern $serverStartPattern |
        Select-Object -Last 1
    if ($selectedServerStart -ne $null) {
        $runtimeDetailMatches = Select-String -LiteralPath $serverLog.FullName -Pattern "event=render_frame_details" |
            Where-Object { $_.LineNumber -gt $selectedServerStart.LineNumber } |
            Select-Object -Last $TailSummaries
        $runtimeDetails = @(
            foreach ($match in $runtimeDetailMatches) {
                Parse-RuntimeDetailLine -Line $match.Line
            }
        )
    }
}

$firstSummaryLineNumber = if ($summaryMatches.Count -gt 0) {
    ($summaryMatches | Select-Object -First 1).LineNumber
} else {
    0
}
$fallbackMatches = @(
    Select-String -LiteralPath $ofxLog -Pattern "GPU TorchScript prep failed" |
        Where-Object { $_.LineNumber -ge $firstSummaryLineNumber } |
        Select-Object -Last 5
)

$averages = [ordered]@{
    total_ms = Average-Field $summaries "total_ms"
    ofx_client_render_rpc_ms = Average-Field $summaries "ofx_client_render_rpc_ms"
    frame_prepare_inputs_ms = Average-Field $summaries "frame_prepare_inputs_ms"
    gpu_prepare_wait_over_device_ms = Average-Field $summaries "gpu_prepare_wait_over_device_ms"
    torchtrt_work_stream_guard_ms = Average-Field $summaries "torchtrt_work_stream_guard_ms"
    torchtrt_input_boundary_host_roundtrip_ms = Average-Field $summaries "torchtrt_input_boundary_host_roundtrip_ms"
    torchtrt_input_ready_wait_ms = Average-Field $summaries "torchtrt_input_ready_wait_ms"
    torchtrt_input_copy_queue_wait_ms = Average-Field $summaries "torchtrt_input_copy_queue_wait_ms"
    torchtrt_forward_ms = Average-Field $summaries "torchtrt_forward_ms"
    torchtrt_forward_direct_ms = Average-Field $summaries "torchtrt_forward_direct_ms"
    torchtrt_forward_direct_gpu_ms = Average-Field $summaries "torchtrt_forward_direct_gpu_ms"
    torchtrt_forward_direct_queue_wait_ms = Average-Field $summaries "torchtrt_forward_direct_queue_wait_ms"
    torchtrt_forward_direct_enqueue_wall_ms = Average-Field $summaries "torchtrt_forward_direct_enqueue_wall_ms"
    torchtrt_forward_direct_event_sync_wait_ms = Average-Field $summaries "torchtrt_forward_direct_event_sync_wait_ms"
    torchtrt_forward_direct_event_sync_over_gpu_ms = Average-Field $summaries "torchtrt_forward_direct_event_sync_over_gpu_ms"
    torchtrt_cuda_graph_fallback_not_enabled_ms = Average-Field $summaries "torchtrt_cuda_graph_fallback_not_enabled_ms"
    torchtrt_replay_gpu_ms = Average-Field $summaries "torchtrt_replay_gpu_ms"
    post_gpu_prepare_ms = Average-Field $summaries "post_gpu_prepare_ms"
    torchtrt_output_d2h_direct_ms = Average-Field $summaries "torchtrt_output_d2h_direct_ms"
    torchtrt_output_host_register_ms = Average-Field $summaries "torchtrt_output_host_register_ms"
    torchtrt_output_copy_enqueue_ms = Average-Field $summaries "torchtrt_output_copy_enqueue_ms"
    torchtrt_output_copy_sync_ms = Average-Field $summaries "torchtrt_output_copy_sync_ms"
    torchtrt_output_host_unregister_ms = Average-Field $summaries "torchtrt_output_host_unregister_ms"
    ofx_client_readback_ms = Average-Field $summaries "ofx_client_readback_ms"
    ofx_write_output_ms = Average-Field $summaries "ofx_write_output_ms"
}

$maximums = [ordered]@{
    total_ms = Max-Field $summaries "total_ms"
    gpu_prepare_wait_over_device_ms = Max-Field $summaries "gpu_prepare_wait_over_device_ms"
    torchtrt_input_boundary_host_roundtrip_ms = Max-Field $summaries "torchtrt_input_boundary_host_roundtrip_ms"
    torchtrt_input_ready_wait_ms = Max-Field $summaries "torchtrt_input_ready_wait_ms"
    torchtrt_input_copy_queue_wait_ms = Max-Field $summaries "torchtrt_input_copy_queue_wait_ms"
    torchtrt_forward_direct_ms = Max-Field $summaries "torchtrt_forward_direct_ms"
    torchtrt_forward_direct_gpu_ms = Max-Field $summaries "torchtrt_forward_direct_gpu_ms"
    torchtrt_forward_direct_queue_wait_ms = Max-Field $summaries "torchtrt_forward_direct_queue_wait_ms"
    torchtrt_forward_direct_enqueue_wall_ms = Max-Field $summaries "torchtrt_forward_direct_enqueue_wall_ms"
    torchtrt_forward_direct_event_sync_wait_ms = Max-Field $summaries "torchtrt_forward_direct_event_sync_wait_ms"
    torchtrt_forward_direct_event_sync_over_gpu_ms = Max-Field $summaries "torchtrt_forward_direct_event_sync_over_gpu_ms"
    torchtrt_output_host_register_ms = Max-Field $summaries "torchtrt_output_host_register_ms"
    torchtrt_output_copy_sync_ms = Max-Field $summaries "torchtrt_output_copy_sync_ms"
    torchtrt_output_host_unregister_ms = Max-Field $summaries "torchtrt_output_host_unregister_ms"
    torchtrt_replay_gpu_ms = Max-Field $summaries "torchtrt_replay_gpu_ms"
}

$stageObservability = [ordered]@{
    torchtrt_work_stream_guard_ms_field_count = Count-FieldPresence $summaries "torchtrt_work_stream_guard_ms"
    torchtrt_work_stream_guard_present_field_count = Count-FieldPresence $summaries "torchtrt_work_stream_guard_present"
    torchtrt_work_stream_guard_present_count = Count-FlagSet $summaries "torchtrt_work_stream_guard_present"
    torchtrt_input_boundary_host_roundtrip_present_count = Count-FlagSet $summaries "torchtrt_input_boundary_host_roundtrip_present"
    torchtrt_forward_direct_present_count = Count-FlagSet $summaries "torchtrt_forward_direct_present"
    torchtrt_forward_direct_queue_wait_field_count = Count-FieldPresence $summaries "torchtrt_forward_direct_queue_wait_ms"
    torchtrt_forward_direct_enqueue_wall_field_count = Count-FieldPresence $summaries "torchtrt_forward_direct_enqueue_wall_ms"
    torchtrt_forward_direct_event_sync_wait_field_count = Count-FieldPresence $summaries "torchtrt_forward_direct_event_sync_wait_ms"
    torchtrt_forward_direct_event_sync_over_gpu_field_count = Count-FieldPresence $summaries "torchtrt_forward_direct_event_sync_over_gpu_ms"
    torchtrt_cuda_graph_fallback_not_enabled_present_count = Count-FlagSet $summaries "torchtrt_cuda_graph_fallback_not_enabled_present"
}

$findings = New-Object System.Collections.Generic.List[string]
if ($fallbackMatches.Count -gt 0) {
    $findings.Add("CPU fallback message present in recent OFX log.")
}
if ($averages.torchtrt_input_ready_wait_ms -gt $InputReadyWaitBudgetMs) {
    $findings.Add("TorchTRT input readiness wait exceeds budget.")
}
if ($averages.gpu_prepare_wait_over_device_ms -gt $GpuPrepareWaitBudgetMs) {
    $findings.Add("GPU prepare wait over measured device time exceeds budget.")
}
if ($averages.torchtrt_input_copy_queue_wait_ms -gt $InputCopyQueueWaitBudgetMs) {
    $findings.Add("CUDA Graph static input copy queue wait exceeds budget.")
}
if ($summaries.Count -gt 0 -and $stageObservability.torchtrt_work_stream_guard_ms_field_count -eq 0) {
    $findings.Add("TorchTRT work stream guard timing field missing in selected OFX summaries.")
} elseif ($summaries.Count -gt 0 -and $stageObservability.torchtrt_work_stream_guard_present_field_count -eq 0) {
    $findings.Add("TorchTRT work stream guard presence flag missing; selected package/log predates Task 0005 instrumentation.")
} elseif ($summaries.Count -gt 0 -and $stageObservability.torchtrt_work_stream_guard_present_count -eq 0) {
    $findings.Add("TorchTRT work stream guard stage absent in selected OFX summaries.")
}
if ($summaries.Count -eq 0) {
    $findings.Add("No OFX render summaries found.")
}
if ((Count-Field $summaries "pid").Count -gt 1) {
    $findings.Add("Recent OFX summaries include multiple plugin process ids; verify the window before drawing Resolve conclusions.")
}

[pscustomobject]@{
    log_dir = $LogDir
    ofx_log = $ofxLog
    runtime_log = if ($serverLog -ne $null) { $serverLog.FullName } else { "" }
    runtime_display_version = $runtimeDisplayVersion
    sample_count = $summaries.Count
    filters = [ordered]@{
        since_local_time = $SinceLocalTime
        pid = $PluginPid
        work_origin = $WorkOrigin
        tail_summaries = $TailSummaries
    }
    summary_window = [ordered]@{
        first_line = $firstSummaryLineNumber
        last_line = if ($summaryMatches.Count -gt 0) { ($summaryMatches | Select-Object -Last 1).LineNumber } else { 0 }
        first_timestamp = if ($summaries.Count -gt 0 -and $summaries[0].ContainsKey("timestamp")) { $summaries[0]["timestamp"] } else { "" }
        last_timestamp = if ($summaries.Count -gt 0 -and $summaries[-1].ContainsKey("timestamp")) { $summaries[-1]["timestamp"] } else { "" }
        pids = Count-Field $summaries "pid"
        work_origins = Count-Field $summaries "work_origin"
    }
    runtime_environment = $runtimeEnvironment
    budgets_ms = [ordered]@{
        torchtrt_input_ready_wait_ms = $InputReadyWaitBudgetMs
        gpu_prepare_wait_over_device_ms = $GpuPrepareWaitBudgetMs
        torchtrt_input_copy_queue_wait_ms = $InputCopyQueueWaitBudgetMs
    }
    averages_ms = $averages
    maximums_ms = $maximums
    runtime_detail_sample_count = $runtimeDetails.Count
    runtime_detail_stages_ms = [ordered]@{
        frame_prepare_inputs = Stats-Field $runtimeDetails "frame_prepare_inputs"
        torchtrt_forward_direct = Stats-Field $runtimeDetails "torchtrt_forward_direct"
        torchtrt_forward_direct_gpu = Stats-Field $runtimeDetails "torchtrt_forward_direct_gpu"
        torchtrt_forward_direct_queue_wait = Stats-Field $runtimeDetails "torchtrt_forward_direct_queue_wait"
        torchtrt_forward_direct_enqueue_wall = Stats-Field $runtimeDetails "torchtrt_forward_direct_enqueue_wall"
        torchtrt_forward_direct_event_sync_wait = Stats-Field $runtimeDetails "torchtrt_forward_direct_event_sync_wait"
        torchtrt_forward_direct_event_sync_over_gpu = Stats-Field $runtimeDetails "torchtrt_forward_direct_event_sync_over_gpu"
        torchtrt_cuda_graph_input_copy_queue_wait = Stats-Field $runtimeDetails "torchtrt_cuda_graph_input_copy_queue_wait"
        frame_extract_outputs_resize = Stats-Field $runtimeDetails "frame_extract_outputs_resize"
        post_source_passthrough = Stats-Field $runtimeDetails "post_source_passthrough"
        post_source_passthrough_gpu_threshold = Stats-Field $runtimeDetails "post_source_passthrough_gpu_threshold"
        post_source_passthrough_gpu_erode = Stats-Field $runtimeDetails "post_source_passthrough_gpu_erode"
        post_source_passthrough_gpu_blur = Stats-Field $runtimeDetails "post_source_passthrough_gpu_blur"
        post_source_passthrough_gpu_source_copy_enqueue = Stats-Field $runtimeDetails "post_source_passthrough_gpu_source_copy_enqueue"
        post_source_passthrough_gpu_blend = Stats-Field $runtimeDetails "post_source_passthrough_gpu_blend"
        post_despeckle = Stats-Field $runtimeDetails "post_despeckle"
        post_despill = Stats-Field $runtimeDetails "post_despill"
        post_gpu_prepare = Stats-Field $runtimeDetails "post_gpu_prepare"
        torchtrt_output_d2h_direct = Stats-Field $runtimeDetails "torchtrt_output_d2h_direct"
        torchtrt_output_host_register = Stats-Field $runtimeDetails "torchtrt_output_host_register"
        torchtrt_output_copy_enqueue = Stats-Field $runtimeDetails "torchtrt_output_copy_enqueue"
        torchtrt_output_copy_sync = Stats-Field $runtimeDetails "torchtrt_output_copy_sync"
        torchtrt_output_host_unregister = Stats-Field $runtimeDetails "torchtrt_output_host_unregister"
    }
    stage_observability = $stageObservability
    findings = @($findings)
    recent_cpu_fallback_lines = @($fallbackMatches | ForEach-Object { $_.Line })
} | ConvertTo-Json -Depth 6
