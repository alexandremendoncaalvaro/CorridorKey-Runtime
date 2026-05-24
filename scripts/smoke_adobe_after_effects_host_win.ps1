param(
    [string]$PackagePath = "",
    [switch]$UseInstalledPayload,
    [string]$CommonPluginInstallPath = "",
    [string]$AfterFxPath = "",
    [string]$AerenderPath = "",
    [ValidateSet("green", "blue")]
    [string]$EffectComponent = "green",
    [string]$ExpectedDisplayVersionLabel = "",
    [string]$ReportPath = "",
    [string]$WorkDir = "",
    [int]$ScriptTimeoutSeconds = 180,
    [int]$RenderTimeoutSeconds = 900,
    [switch]$AllowSystemPluginPath,
    [switch]$KeepWorkDir
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot

function Resolve-FullPath {
    param([string]$Path)

    if ([string]::IsNullOrWhiteSpace($Path)) {
        throw "Path must not be empty."
    }
    if ([System.IO.Path]::IsPathRooted($Path)) {
        return [System.IO.Path]::GetFullPath($Path)
    }
    return [System.IO.Path]::GetFullPath((Join-Path (Get-Location) $Path))
}

function Test-CurrentProcessElevated {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = [Security.Principal.WindowsPrincipal]::new($identity)
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Test-PathUnderRoot {
    param([string]$Path, [string]$Root)

    $pathSeparators = [char[]]@([System.IO.Path]::DirectorySeparatorChar, [System.IO.Path]::AltDirectorySeparatorChar)
    $normalizedPath = ([System.IO.Path]::GetFullPath($Path)).TrimEnd($pathSeparators)
    $normalizedRoot = ([System.IO.Path]::GetFullPath($Root)).TrimEnd($pathSeparators)
    $rootPrefix = $normalizedRoot + [System.IO.Path]::DirectorySeparatorChar
    return $normalizedPath.StartsWith($rootPrefix, [System.StringComparison]::OrdinalIgnoreCase)
}

function Get-AdobeCommonPluginInstallPath {
    foreach ($registryRoot in @("HKLM:\SOFTWARE\Adobe\After Effects", "HKLM:\SOFTWARE\WOW6432Node\Adobe\After Effects")) {
        if (-not (Test-Path $registryRoot)) {
            continue
        }
        $keys = @(Get-ChildItem -Path $registryRoot -ErrorAction SilentlyContinue)
        foreach ($key in ($keys | Sort-Object -Property PSChildName -Descending)) {
            try {
                $value = $key.GetValue("CommonPluginInstallPath", $null, "DoNotExpandEnvironmentNames")
                if ($null -ne $value -and -not [string]::IsNullOrWhiteSpace([string]$value)) {
                    return ([string]$value).Trim()
                }
            } catch {
            }
        }
    }
    return Join-Path $env:ProgramFiles "Adobe\Common\Plug-ins\7.0\MediaCore"
}

function Resolve-CommonPluginInstallPath {
    param([string]$Path)

    if ([string]::IsNullOrWhiteSpace($Path)) {
        return Resolve-FullPath -Path (Get-AdobeCommonPluginInstallPath)
    }
    return Resolve-FullPath -Path $Path
}

function Resolve-AfterEffectsSupportDirs {
    $dirs = @()
    foreach ($root in @($env:ProgramFiles, ${env:ProgramFiles(x86)})) {
        if ([string]::IsNullOrWhiteSpace($root)) {
            continue
        }
        $adobeRoot = Join-Path $root "Adobe"
        if (-not (Test-Path -LiteralPath $adobeRoot)) {
            continue
        }
        $dirs += Get-ChildItem -LiteralPath $adobeRoot -Directory -ErrorAction SilentlyContinue |
            Where-Object { $_.Name -like "Adobe After Effects*" } |
            ForEach-Object { Join-Path $_.FullName "Support Files" } |
            Where-Object { Test-Path -LiteralPath $_ }
    }
    return @($dirs | Sort-Object -Descending -Unique)
}

function Resolve-AfterEffectsTool {
    param(
        [string]$ProvidedPath,
        [string[]]$CandidateNames
    )

    if (-not [string]::IsNullOrWhiteSpace($ProvidedPath)) {
        $resolved = Resolve-FullPath -Path $ProvidedPath
        if (-not (Test-Path -LiteralPath $resolved)) {
            throw "After Effects tool not found: $resolved"
        }
        return $resolved
    }

    foreach ($supportDir in Resolve-AfterEffectsSupportDirs) {
        foreach ($name in $CandidateNames) {
            $candidate = Join-Path $supportDir $name
            if (Test-Path -LiteralPath $candidate) {
                return $candidate
            }
        }
    }

    throw "Could not find After Effects tool: $($CandidateNames -join ', ')"
}

function Assert-AfterEffectsNotRunning {
    $running = @(Get-Process -ErrorAction SilentlyContinue |
            Where-Object { $_.ProcessName -in @("AfterFX", "AfterFX.com", "aerender") })
    if ($running.Count -gt 0) {
        $summary = ($running | ForEach-Object { "$($_.ProcessName)($($_.Id))" }) -join ", "
        throw "Adobe host smoke requires a fresh After Effects process. Close After Effects/aerender and retry. Running: $summary"
    }
}

function Assert-InstalledPayload {
    param(
        [string]$TargetPath,
        [string]$Component
    )

    $win64Dir = Join-Path $TargetPath "Contents\Win64"
    $componentEffect = if ($Component -eq "blue") {
        "corridorkey_adobe_blue.aex"
    } else {
        "corridorkey_adobe_green.aex"
    }
    foreach ($requiredFile in @($componentEffect, "corridorkey.exe", "corridorkey_host_plugin_runtime_server.exe")) {
        $path = Join-Path $win64Dir $requiredFile
        if (-not (Test-Path -LiteralPath $path)) {
            throw "Installed Adobe payload is missing file required for host smoke: $path"
        }
    }
}

function Read-ExpectedDisplayVersionLabel {
    param([string]$PackageRoot, [string]$ProvidedLabel)

    if (-not [string]::IsNullOrWhiteSpace($ProvidedLabel)) {
        return $ProvidedLabel
    }
    if ([string]::IsNullOrWhiteSpace($PackageRoot)) {
        return ""
    }
    $validationPath = Join-Path $PackageRoot "adobe_package_validation.json"
    if (-not (Test-Path -LiteralPath $validationPath)) {
        return ""
    }
    $validation = Get-Content -LiteralPath $validationPath -Raw | ConvertFrom-Json
    if ($null -ne $validation.runtime -and
        -not [string]::IsNullOrWhiteSpace([string]$validation.runtime.expected_display_version)) {
        return [string]$validation.runtime.expected_display_version
    }
    return ""
}

function Install-PackagePayloadIfRequested {
    param(
        [string]$PackageRoot,
        [string]$CommonPluginRoot
    )

    if ($UseInstalledPayload.IsPresent) {
        return
    }
    if ([string]::IsNullOrWhiteSpace($PackageRoot)) {
        throw "Pass -PackagePath, or pass -UseInstalledPayload to validate the already installed Adobe payload."
    }

    $programFilesRoot = Resolve-FullPath -Path $env:ProgramFiles
    if ((Test-PathUnderRoot -Path $CommonPluginRoot -Root $programFilesRoot) -and
        -not (Test-CurrentProcessElevated)) {
        throw "Installing the Adobe package into '$CommonPluginRoot' requires an elevated shell. Run elevated with -AllowSystemPluginPath, or install the package first and rerun with -UseInstalledPayload."
    }

    $installSmokeScript = Join-Path $repoRoot "scripts\smoke_adobe_install_win.ps1"
    $installReport = Join-Path $script:workRoot "adobe_install_smoke_host.json"
    $arguments = @(
        "-PackagePath", $PackageRoot,
        "-CommonPluginInstallPath", $CommonPluginRoot,
        "-Mode", "clean",
        "-ReportPath", $installReport
    )
    if ($AllowSystemPluginPath.IsPresent) {
        $arguments += "-AllowSystemPluginPath"
    }

    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $installSmokeScript @arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Adobe package install smoke failed before host smoke."
    }
}

function ConvertTo-JsxPath {
    param([string]$Path)
    return ([System.IO.Path]::GetFullPath($Path)).Replace("\", "/").Replace("'", "\\'")
}

function ConvertTo-CommandLineArgument {
    param([string]$Argument)

    if ($Argument -notmatch '[\s"]') {
        return $Argument
    }
    return '"' + ($Argument -replace '"', '\"') + '"'
}

function Invoke-AfterEffectsScript {
    param(
        [string]$ExecutablePath,
        [string]$ScriptPath,
        [string]$SentinelPath,
        [int]$TimeoutSeconds
    )

    $process = Start-Process -FilePath $ExecutablePath `
        -ArgumentList @("-r", $ScriptPath) `
        -WindowStyle Hidden `
        -PassThru
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    while ((Get-Date) -lt $deadline) {
        if (Test-Path -LiteralPath $SentinelPath) {
            return
        }
        if ($process.HasExited -and -not (Test-Path -LiteralPath $SentinelPath)) {
            throw "AfterFX.com exited before writing the fixture report."
        }
        Start-Sleep -Milliseconds 500
    }
    throw "AfterFX.com did not produce the fixture report within $TimeoutSeconds seconds."
}

function Invoke-Aerender {
    param(
        [string]$ExecutablePath,
        [string]$ProjectPath,
        [string]$LogPath,
        [int]$TimeoutSeconds
    )

    $arguments = @(
        "-project", $ProjectPath,
        "-rqindex", "1",
        "-v", "ERRORS_AND_PROGRESS",
        "-log", $LogPath
    )
    $argumentLine = ($arguments | ForEach-Object { ConvertTo-CommandLineArgument -Argument $_ }) -join " "
    $process = Start-Process -FilePath $ExecutablePath `
        -ArgumentList $argumentLine `
        -WindowStyle Hidden `
        -PassThru
    if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
        try {
            $process.Kill()
        } catch {
        }
        throw "aerender.exe did not finish within $TimeoutSeconds seconds."
    }
    if ($process.ExitCode -ne 0) {
        $tail = ""
        if (Test-Path -LiteralPath $LogPath) {
            $tail = (Get-Content -LiteralPath $LogPath -Tail 80) -join [Environment]::NewLine
        }
        throw "aerender.exe failed with exit code $($process.ExitCode).$([Environment]::NewLine)$tail"
    }
}

function Measure-RenderedFrame {
    param([string]$ImagePath)

    Add-Type -AssemblyName System.Drawing
    $bitmap = [System.Drawing.Bitmap]::new($ImagePath)
    try {
        $minimum = 255.0
        $maximum = 0.0
        $sum = 0.0
        $count = 0
        for ($y = 0; $y -lt $bitmap.Height; ++$y) {
            for ($x = 0; $x -lt $bitmap.Width; ++$x) {
                $pixel = $bitmap.GetPixel($x, $y)
                $luma = (0.2126 * $pixel.R) + (0.7152 * $pixel.G) + (0.0722 * $pixel.B)
                $minimum = [Math]::Min($minimum, $luma)
                $maximum = [Math]::Max($maximum, $luma)
                $sum += $luma
                ++$count
            }
        }
        return [ordered]@{
            path = $ImagePath
            width = $bitmap.Width
            height = $bitmap.Height
            output_luma_min = [Math]::Round($minimum, 3)
            output_luma_max = [Math]::Round($maximum, 3)
            output_luma_mean = [Math]::Round(($sum / [Math]::Max(1, $count)), 3)
        }
    } finally {
        $bitmap.Dispose()
    }
}

function Wait-RenderedFrames {
    param(
        [string]$RenderDir,
        [int]$TimeoutSeconds = 30
    )

    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    do {
        $frames = @(Get-ChildItem -LiteralPath $RenderDir -Filter "*.tif" -File -ErrorAction SilentlyContinue |
                Sort-Object -Property Name)
        if ($frames.Count -gt 0) {
            return $frames
        }
        Start-Sleep -Milliseconds 500
    } while ((Get-Date) -lt $deadline)

    return @()
}

function Write-CorridorKeyJsonFile {
    param([string]$Path, $Payload)

    $directory = Split-Path -Parent $Path
    if (-not [string]::IsNullOrWhiteSpace($directory)) {
        [System.IO.Directory]::CreateDirectory($directory) | Out-Null
    }
    $Payload | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $Path -Encoding UTF8
}

$packageRoot = ""
if (-not [string]::IsNullOrWhiteSpace($PackagePath)) {
    $packageRoot = Resolve-FullPath -Path $PackagePath
    if (-not (Test-Path -LiteralPath $packageRoot)) {
        throw "Adobe package path not found: $packageRoot"
    }
}

$expectedLabel = Read-ExpectedDisplayVersionLabel -PackageRoot $packageRoot -ProvidedLabel $ExpectedDisplayVersionLabel
$commonPluginRoot = Resolve-CommonPluginInstallPath -Path $CommonPluginInstallPath
$targetPath = Join-Path $commonPluginRoot "CorridorKey"

if ([string]::IsNullOrWhiteSpace($WorkDir)) {
    $script:workRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("corridorkey-adobe-host-smoke-" + [System.Guid]::NewGuid().ToString("N"))
} else {
    $script:workRoot = Resolve-FullPath -Path $WorkDir
}

if ([string]::IsNullOrWhiteSpace($ReportPath)) {
    $ReportPath = if (-not [string]::IsNullOrWhiteSpace($packageRoot)) {
        Join-Path $packageRoot ("adobe_after_effects_host_smoke_" + $EffectComponent + ".json")
    } else {
        Join-Path $script:workRoot ("adobe_after_effects_host_smoke_" + $EffectComponent + ".json")
    }
}
$ReportPath = Resolve-FullPath -Path $ReportPath

$report = [ordered]@{
    validation_passed = $false
    host_surface = "after_effects"
    effect_component = $EffectComponent
    expected_display_version = $expectedLabel
    package_path = $packageRoot
    common_plugin_install_path = $commonPluginRoot
    target_path = $targetPath
    work_dir = $script:workRoot
    known_host_cancel_status = "PF_Interrupt_CANCEL"
}

try {
    Assert-AfterEffectsNotRunning
    [System.IO.Directory]::CreateDirectory($script:workRoot) | Out-Null
    Install-PackagePayloadIfRequested -PackageRoot $packageRoot -CommonPluginRoot $commonPluginRoot
    Assert-InstalledPayload -TargetPath $targetPath -Component $EffectComponent

    $afterFx = Resolve-AfterEffectsTool -ProvidedPath $AfterFxPath -CandidateNames @("AfterFX.com", "AfterFX.exe")
    $aerender = Resolve-AfterEffectsTool -ProvidedPath $AerenderPath -CandidateNames @("aerender.exe")
    $renderDir = Join-Path $script:workRoot "render"
    [System.IO.Directory]::CreateDirectory($renderDir) | Out-Null

    $matchName = if ($EffectComponent -eq "blue") { "com.corridorkey.effect.blue" } else { "com.corridorkey.effect" }
    $projectPath = Join-Path $script:workRoot "corridorkey_after_effects_host_smoke.aep"
    $fixtureReportPath = Join-Path $script:workRoot "fixture_report.json"
    $aerenderLogPath = Join-Path $script:workRoot "aerender.log"
    $outputPattern = Join-Path $renderDir "corridorkey_after_effects_host_smoke_[#####].tif"
    $jsxPath = Join-Path $script:workRoot "create_corridorkey_after_effects_host_smoke.jsx"

    $jsx = @"
function jsonEscape(value) {
    return String(value).replace(/\\/g, "\\\\").replace(/"/g, "\\\"").replace(/\r/g, "\\r").replace(/\n/g, "\\n");
}
function writeReport(status, fields) {
    var file = new File("$(ConvertTo-JsxPath -Path $fixtureReportPath)");
    file.open("w");
    var text = "{\n  \"status\": \"" + jsonEscape(status) + "\"";
    for (var key in fields) {
        text += ",\n  \"" + jsonEscape(key) + "\": \"" + jsonEscape(fields[key]) + "\"";
    }
    text += "\n}\n";
    file.write(text);
    file.close();
}
function findTemplate(templates, needles) {
    for (var n = 0; n < needles.length; ++n) {
        for (var i = 0; i < templates.length; ++i) {
            if (templates[i].toLowerCase().indexOf(needles[n].toLowerCase()) >= 0) {
                return templates[i];
            }
        }
    }
    return templates.length > 0 ? templates[0] : "";
}
try {
    app.beginSuppressDialogs();
    app.newProject();
    var width = 64;
    var height = 64;
    var sourceComp = app.project.items.addComp("CorridorKey_Source", width, height, 1, 1, 1);
    sourceComp.layers.addSolid([0, 1, 0], "Green Background", width, height, 1, 1);
    var foreground = sourceComp.layers.addSolid([1, 0, 0], "Red Foreground", width / 2, height, 1, 1);
    foreground.position.setValue([width / 4, height / 2]);

    var comp = app.project.items.addComp("CorridorKey_E2E", width, height, 1, 1, 1);
    var alphaLayer = comp.layers.addSolid([1, 1, 1], "Alpha Hint Layer", width, height, 1, 1);
    var mask = alphaLayer.Masks.addProperty("Mask");
    var shape = new Shape();
    shape.vertices = [[0, 0], [width / 2, 0], [width / 2, height], [0, height]];
    shape.inTangents = [[0, 0], [0, 0], [0, 0], [0, 0]];
    shape.outTangents = [[0, 0], [0, 0], [0, 0], [0, 0]];
    shape.closed = true;
    mask.property("ADBE Mask Shape").setValue(shape);
    alphaLayer.guideLayer = true;

    var sourceLayer = comp.layers.add(sourceComp);
    sourceLayer.moveBefore(alphaLayer);
    var effect = sourceLayer.property("ADBE Effect Parade").addProperty("$matchName");
    if (effect === null) {
        throw new Error("Could not add CorridorKey effect by match name $matchName");
    }
    effect.property("Alpha Hint Layer").setValue(alphaLayer.index);
    effect.property("Output Mode").setValue(2);
    effect.property("Quality").setValue(2);
    effect.property("Prepare Timeout (s)").setValue(120);
    effect.property("Render Timeout (s)").setValue(300);

    var rqItem = app.project.renderQueue.items.add(comp);
    rqItem.timeSpanStart = 0;
    rqItem.timeSpanDuration = comp.frameDuration;
    var outputModule = rqItem.outputModule(1);
    var outputTemplate = findTemplate(outputModule.templates, ["TIFF", "Photoshop"]);
    if (outputTemplate.length > 0) {
        outputModule.applyTemplate(outputTemplate);
    }
    outputModule.file = new File("$(ConvertTo-JsxPath -Path $outputPattern)");
    app.project.save(new File("$(ConvertTo-JsxPath -Path $projectPath)"));
    writeReport("ok", {
        effect_name: effect.name,
        match_name: effect.matchName,
        alpha_hint_layer: "Alpha Hint Layer",
        output_mode: "Matte Only",
        output_template: outputTemplate,
        project_path: "$(ConvertTo-JsxPath -Path $projectPath)",
        output_pattern: "$(ConvertTo-JsxPath -Path $outputPattern)"
    });
    app.endSuppressDialogs(false);
    app.quit();
} catch (err) {
    writeReport("error", { message: err.toString(), line: err.line || "" });
    try { app.endSuppressDialogs(false); } catch (ignored) {}
    try { app.quit(); } catch (ignoredQuit) {}
}
"@
    Set-Content -LiteralPath $jsxPath -Encoding UTF8 -Value $jsx

    Invoke-AfterEffectsScript -ExecutablePath $afterFx `
        -ScriptPath $jsxPath `
        -SentinelPath $fixtureReportPath `
        -TimeoutSeconds $ScriptTimeoutSeconds

    $fixtureReport = Get-Content -LiteralPath $fixtureReportPath -Raw | ConvertFrom-Json
    if ([string]$fixtureReport.status -ne "ok") {
        throw "After Effects fixture creation failed: $($fixtureReport.message)"
    }
    if (-not [string]::IsNullOrWhiteSpace($expectedLabel) -and
        ([string]$fixtureReport.effect_name).IndexOf($expectedLabel, [System.StringComparison]::OrdinalIgnoreCase) -lt 0) {
        throw "After Effects loaded effect '$($fixtureReport.effect_name)', expected display label '$expectedLabel'."
    }

    Invoke-Aerender -ExecutablePath $aerender `
        -ProjectPath $projectPath `
        -LogPath $aerenderLogPath `
        -TimeoutSeconds $RenderTimeoutSeconds

    $renderedFrames = @(Wait-RenderedFrames -RenderDir $renderDir)
    if ($renderedFrames.Count -eq 0) {
        throw "aerender completed but produced no TIFF frames in $renderDir."
    }
    $stats = Measure-RenderedFrame -ImagePath $renderedFrames[0].FullName
    if ([double]$stats.output_luma_min -ge 250.0 -and [double]$stats.output_luma_max -ge 250.0) {
        throw "Rendered matte is fully white; this matches the Alpha Hint failure mode this smoke is meant to catch."
    }
    if (([double]$stats.output_luma_max - [double]$stats.output_luma_min) -lt 16.0) {
        throw "Rendered matte does not contain enough luminance variation to prove the fixture rendered correctly."
    }

    $pluginLoadingLogs = @(Get-ChildItem -Path (Join-Path $env:APPDATA "Adobe\After Effects") `
            -Recurse -Filter "Plugin Loading.log" -ErrorAction SilentlyContinue |
            Sort-Object -Property LastWriteTime -Descending)
    $pluginLoadingLog = if ($pluginLoadingLogs.Count -gt 0) { $pluginLoadingLogs[0].FullName } else { "" }

    $report.afterfx_path = $afterFx
    $report.aerender_path = $aerender
    $report.fixture = $fixtureReport
    $report.render = [ordered]@{
        project_path = $projectPath
        aerender_log = $aerenderLogPath
        frame_path = $renderedFrames[0].FullName
    }
    $report.pixel_probe = $stats
    $report.plugin_loading_log = $pluginLoadingLog
    $report.validation_passed = $true
    Write-CorridorKeyJsonFile -Path $ReportPath -Payload $report
    Write-Host "[PASS] Adobe After Effects host smoke report: $ReportPath" -ForegroundColor Green
} catch {
    $report.error = $_.Exception.Message
    Write-CorridorKeyJsonFile -Path $ReportPath -Payload $report
    throw
} finally {
    if (-not $KeepWorkDir.IsPresent -and (Test-Path -LiteralPath $script:workRoot)) {
        Remove-Item -LiteralPath $script:workRoot -Recurse -Force
    }
}
