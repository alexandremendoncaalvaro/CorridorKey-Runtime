param(
    [string]$LogDir = (Join-Path $env:LOCALAPPDATA "CorridorKey\Logs"),
    [int]$TailSummaries = 20,
    [double]$InputReadyWaitBudgetMs = 5.0,
    [double]$InputCopyQueueWaitBudgetMs = 5.0,
    [double]$GpuPrepareWaitBudgetMs = 5.0
)

$ErrorActionPreference = "Stop"

function Parse-KeyValueLine {
    param([string]$Line)

    $values = @{}
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

if (-not (Test-Path -LiteralPath $LogDir -PathType Container)) {
    throw "Log directory not found: $LogDir"
}

$ofxLog = Join-Path $LogDir "ofx.log"
if (-not (Test-Path -LiteralPath $ofxLog -PathType Leaf)) {
    throw "OFX log not found: $ofxLog"
}

$serverLog = Get-ChildItem -LiteralPath $LogDir -Filter "ofx_runtime_server*.log" |
    Sort-Object LastWriteTime -Descending |
    Select-Object -First 1

$runtimeDisplayVersion = ""
if ($serverLog -ne $null) {
    $serverStart = Select-String -LiteralPath $serverLog.FullName -Pattern "event=server_start" |
        Select-Object -First 1
    if ($serverStart -ne $null -and $serverStart.Line -match "display_version=(?<label>\S+)") {
        $runtimeDisplayVersion = $matches["label"]
    }
}

$summaryMatches = Select-String -LiteralPath $ofxLog -Pattern "event=ofx_render_summary" |
    Select-Object -Last $TailSummaries
$summaries = @(
    foreach ($match in $summaryMatches) {
        Parse-KeyValueLine -Line $match.Line
    }
)

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
    torchtrt_input_ready_wait_ms = Average-Field $summaries "torchtrt_input_ready_wait_ms"
    torchtrt_input_copy_queue_wait_ms = Average-Field $summaries "torchtrt_input_copy_queue_wait_ms"
    torchtrt_forward_ms = Average-Field $summaries "torchtrt_forward_ms"
    torchtrt_replay_gpu_ms = Average-Field $summaries "torchtrt_replay_gpu_ms"
    post_gpu_prepare_ms = Average-Field $summaries "post_gpu_prepare_ms"
    torchtrt_output_d2h_direct_ms = Average-Field $summaries "torchtrt_output_d2h_direct_ms"
    ofx_client_readback_ms = Average-Field $summaries "ofx_client_readback_ms"
    ofx_write_output_ms = Average-Field $summaries "ofx_write_output_ms"
}

$maximums = [ordered]@{
    total_ms = Max-Field $summaries "total_ms"
    gpu_prepare_wait_over_device_ms = Max-Field $summaries "gpu_prepare_wait_over_device_ms"
    torchtrt_input_ready_wait_ms = Max-Field $summaries "torchtrt_input_ready_wait_ms"
    torchtrt_input_copy_queue_wait_ms = Max-Field $summaries "torchtrt_input_copy_queue_wait_ms"
    torchtrt_replay_gpu_ms = Max-Field $summaries "torchtrt_replay_gpu_ms"
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
if ($summaries.Count -eq 0) {
    $findings.Add("No OFX render summaries found.")
}

[pscustomobject]@{
    log_dir = $LogDir
    ofx_log = $ofxLog
    runtime_log = if ($serverLog -ne $null) { $serverLog.FullName } else { "" }
    runtime_display_version = $runtimeDisplayVersion
    sample_count = $summaries.Count
    budgets_ms = [ordered]@{
        torchtrt_input_ready_wait_ms = $InputReadyWaitBudgetMs
        gpu_prepare_wait_over_device_ms = $GpuPrepareWaitBudgetMs
        torchtrt_input_copy_queue_wait_ms = $InputCopyQueueWaitBudgetMs
    }
    averages_ms = $averages
    maximums_ms = $maximums
    findings = @($findings)
    recent_cpu_fallback_lines = @($fallbackMatches | ForEach-Object { $_.Line })
} | ConvertTo-Json -Depth 6
