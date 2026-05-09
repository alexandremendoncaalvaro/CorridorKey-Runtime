$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

function Assert-True {
    param(
        [bool]$Condition,
        [string]$Message
    )
    if (-not $Condition) {
        throw $Message
    }
}

function Get-Case {
    param(
        [object[]]$Cases,
        [string]$CaseId
    )
    return ,@($Cases | Where-Object { $_.case_id -eq $CaseId })
}

$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$matrixScript = Join-Path $repoRoot "scripts\run_windows_torchtrt_matrix.ps1"

$output = & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $matrixScript `
    -Profile readiness `
    -RpcResolutions 2048 `
    -ListCases

if ($LASTEXITCODE -ne 0) {
    throw "TorchTRT matrix case listing failed with exit code $LASTEXITCODE."
}

$caseListing = ($output | Out-String) | ConvertFrom-Json
$cases = @($caseListing.rpc_cases)

Assert-True ($cases.Count -gt 0) "TorchTRT readiness matrix returned no RPC cases."

$greenDespeckleConstant = Get-Case -Cases $cases -CaseId "green_despeckle_2048"
Assert-True ($greenDespeckleConstant.Count -eq 1) "Missing legacy green_despeckle_2048 case."
Assert-True ($greenDespeckleConstant[0].input_mode -eq "constant") `
    "green_despeckle_2048 must remain a constant-input compatibility case."

$greenDespecklePlate = Get-Case -Cases $cases -CaseId "green_despeckle_plate_2048"
Assert-True ($greenDespecklePlate.Count -eq 1) "Missing green_despeckle_plate_2048 case."
Assert-True ($greenDespecklePlate[0].despeckle -eq $true) `
    "green_despeckle_plate_2048 must enable auto_despeckle."
Assert-True ($greenDespecklePlate[0].input_mode -eq "plate") `
    "green_despeckle_plate_2048 must use plate input."

$greenDespeckleRandom = Get-Case -Cases $cases -CaseId "green_despeckle_random_2048"
Assert-True ($greenDespeckleRandom.Count -eq 1) "Missing green_despeckle_random_2048 case."
Assert-True ($greenDespeckleRandom[0].despeckle -eq $true) `
    "green_despeckle_random_2048 must enable auto_despeckle."
Assert-True ($greenDespeckleRandom[0].input_mode -eq "random") `
    "green_despeckle_random_2048 must use random input."

$greenSourcePassthroughPlate = Get-Case -Cases $cases `
    -CaseId "green_source_passthrough_plate_2048"
Assert-True ($greenSourcePassthroughPlate.Count -eq 1) `
    "Missing green_source_passthrough_plate_2048 case."
Assert-True ($greenSourcePassthroughPlate[0].source_passthrough -eq $true) `
    "green_source_passthrough_plate_2048 must enable source_passthrough."
Assert-True ($greenSourcePassthroughPlate[0].input_mode -eq "plate") `
    "green_source_passthrough_plate_2048 must use plate input."

$blueProcessedPlate = Get-Case -Cases $cases -CaseId "blue_processed_lanczos_plate_2048"
Assert-True ($blueProcessedPlate.Count -eq 1) `
    "Missing blue_processed_lanczos_plate_2048 case."
Assert-True ($blueProcessedPlate[0].model_color -eq "blue") `
    "blue_processed_lanczos_plate_2048 must use the blue model."
Assert-True ($blueProcessedPlate[0].input_mode -eq "plate") `
    "blue_processed_lanczos_plate_2048 must use plate input."

$blueGreenSwapPlate = Get-Case -Cases $cases -CaseId "blue_green_swap_plate_2048"
Assert-True ($blueGreenSwapPlate.Count -eq 1) "Missing blue_green_swap_plate_2048 case."
Assert-True ($blueGreenSwapPlate[0].screen_color_mode -eq "blue_green") `
    "blue_green_swap_plate_2048 must cover blue-green channel swap."
Assert-True ($blueGreenSwapPlate[0].input_mode -eq "plate") `
    "blue_green_swap_plate_2048 must use plate input."

$despeckleNonConstant = @(
    $cases | Where-Object { $_.despeckle -eq $true -and $_.input_mode -ne "constant" }
)
Assert-True ($despeckleNonConstant.Count -gt 0) `
    "Readiness matrix must not cover auto_despeckle only with constant input."

$sourcePassthroughNonConstant = @(
    $cases | Where-Object { $_.source_passthrough -eq $true -and $_.input_mode -ne "constant" }
)
Assert-True ($sourcePassthroughNonConstant.Count -gt 0) `
    "Readiness matrix must cover source_passthrough with non-constant input."

$blueNonConstant = @(
    $cases | Where-Object { $_.model_color -eq "blue" -and $_.input_mode -ne "constant" }
)
Assert-True ($blueNonConstant.Count -gt 0) `
    "Readiness matrix must cover blue model cases with non-constant input."
