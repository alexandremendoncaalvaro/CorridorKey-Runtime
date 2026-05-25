param(
    [string]$SourcePath = "assets\video_samples\Jordan4k.mp4",
    [string]$AlphaHintPath = "assets\video_samples\Jordan4k_alphahint.mp4",
    [string]$FusionCompositionPath = "",
    [string]$ResolveReferencePath = "",
    [string]$AfterFxPath = "",
    [string]$AerenderPath = "",
    [ValidateSet("green", "blue")]
    [string]$EffectComponent = "green",
    [int]$StartFrame = 0,
    [ValidateRange(1, 120)]
    [int]$FrameCount = 3,
    [ValidateRange(1, 128)]
    [int]$PixelStride = 4,
    [ValidateRange(0, 5)]
    [int]$QualityChoice = 0,
    [ValidateRange(0, 2)]
    [int]$InputColorSpaceChoice = 0,
    [ValidateRange(0, 5)]
    [int]$OutputModeChoice = 0,
    [ValidateRange(0, 3)]
    [int]$QualityFallbackChoice = 0,
    [ValidateRange(0, 5)]
    [int]$CoarseResolutionChoice = 0,
    [ValidateRange(-1, 1)]
    [int]$RecoverOriginalDetailsChoice = -1,
    [ValidateRange(-1, 128)]
    [int]$DetailsEdgeShrink = -1,
    [ValidateRange(-1, 128)]
    [int]$DetailsEdgeFeather = -1,
    [string]$WorkDir = "",
    [string]$ReportPath = "",
    [int]$ScriptTimeoutSeconds = 180,
    [int]$RenderTimeoutSeconds = 1800,
    [ValidateSet(8, 16, 32)]
    [int]$ProjectBitsPerChannel = 8,
    [switch]$CompositeMagentaBackground,
    [switch]$RequireResolveReference
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
    return [System.IO.Path]::GetFullPath((Join-Path $repoRoot $Path))
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
        throw "Close After Effects/aerender and retry. Running: $summary"
    }
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

function Wait-RenderedFrames {
    param(
        [string]$RenderDir,
        [int]$ExpectedCount = 1,
        [int]$TimeoutSeconds = 30
    )

    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    do {
        $frames = @(Get-ChildItem -LiteralPath $RenderDir -Filter "*.tif" -File -ErrorAction SilentlyContinue |
                Where-Object { $_.Name -notlike "AEtemp-*" } |
                Sort-Object -Property Name)
        if ($frames.Count -ge $ExpectedCount) {
            return @($frames | Select-Object -First $ExpectedCount)
        }
        Start-Sleep -Milliseconds 500
    } while ((Get-Date) -lt $deadline)

    return @($frames | Select-Object -First $ExpectedCount)
}

function Write-CorridorKeyJsonFile {
    param([string]$Path, $Payload)

    $directory = Split-Path -Parent $Path
    if (-not [string]::IsNullOrWhiteSpace($directory)) {
        [System.IO.Directory]::CreateDirectory($directory) | Out-Null
    }
    $Payload | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $Path -Encoding UTF8
}

function Get-FusionInputValue {
    param(
        [string]$Text,
        [string]$Name
    )

    $pattern = "(?m)^\s*$([regex]::Escape($Name))\s*=\s*Input\s*\{\s*Value\s*=\s*([^,\r\n}]+)"
    $match = [regex]::Match($Text, $pattern)
    if (-not $match.Success) {
        return $null
    }
    $value = $match.Groups[1].Value.Trim()
    if ($value -match "^-?\d+$") {
        return [int]$value
    }
    return $value.Trim('"')
}

function Get-FusionCompositionSettings {
    param([string]$Path)

    if ([string]::IsNullOrWhiteSpace($Path)) {
        return [ordered]@{
            path = ""
            applied = $false
        }
    }

    $resolved = Resolve-FullPath -Path $Path
    if (-not (Test-Path -LiteralPath $resolved)) {
        throw "Fusion composition path not found: $resolved"
    }
    $text = Get-Content -LiteralPath $resolved -Raw

    $qualityMode = Get-FusionInputValue -Text $text -Name "quality_mode"
    $inputColorSpace = Get-FusionInputValue -Text $text -Name "input_color_space"
    $outputMode = Get-FusionInputValue -Text $text -Name "output_mode"
    $qualityFallbackMode = Get-FusionInputValue -Text $text -Name "quality_fallback_mode"
    $coarseResolutionOverride = Get-FusionInputValue -Text $text -Name "coarse_resolution_override"
    $upscaleMethod = Get-FusionInputValue -Text $text -Name "upscale_method"
    $sourcePassthrough = Get-FusionInputValue -Text $text -Name "source_passthrough"
    $edgeErode = Get-FusionInputValue -Text $text -Name "edge_erode"
    $edgeBlur = Get-FusionInputValue -Text $text -Name "edge_blur"

    $mappedInputColorSpace = switch ($inputColorSpace) {
        0 { 1 }
        1 { 2 }
        2 { 2 }
        default { 1 }
    }

    return [ordered]@{
        path = $resolved
        applied = $true
        ofx_quality_mode = $qualityMode
        ofx_input_color_space = $inputColorSpace
        ofx_output_mode = $outputMode
        ofx_quality_fallback_mode = $qualityFallbackMode
        ofx_coarse_resolution_override = $coarseResolutionOverride
        ofx_upscale_method = $upscaleMethod
        ofx_source_passthrough = $sourcePassthrough
        ofx_edge_erode = $edgeErode
        ofx_edge_blur = $edgeBlur
        ae_quality_choice = if ($null -ne $qualityMode) { [Math]::Min(5, [Math]::Max(1, $qualityMode + 1)) } else { 5 }
        ae_input_color_space_choice = $mappedInputColorSpace
        ae_output_mode_choice = if ($null -ne $outputMode) { [Math]::Min(5, [Math]::Max(1, $outputMode + 1)) } else { 1 }
        ae_quality_fallback_choice = if ($null -ne $qualityFallbackMode) { [Math]::Min(3, [Math]::Max(1, $qualityFallbackMode + 1)) } else { 1 }
        ae_coarse_resolution_choice = if ($null -ne $coarseResolutionOverride) { [Math]::Min(5, [Math]::Max(1, $coarseResolutionOverride + 1)) } else { 1 }
        ae_upscale_choice = if ($null -ne $upscaleMethod) { [Math]::Min(2, [Math]::Max(1, $upscaleMethod + 1)) } else { 2 }
        ae_recover_original_details_choice = if ($null -ne $sourcePassthrough) { [Math]::Min(1, [Math]::Max(0, $sourcePassthrough)) } else { 1 }
        ae_details_edge_shrink = if ($null -ne $edgeErode) { [Math]::Max(0, $edgeErode) } else { 3 }
        ae_details_edge_feather = if ($null -ne $edgeBlur) { [Math]::Max(0, $edgeBlur) } else { 7 }
    }
}

function Resolve-Ffmpeg {
    $command = Get-Command ffmpeg -ErrorAction SilentlyContinue
    if (-not $command) {
        throw "ffmpeg.exe was not found on PATH. It is required to extract Resolve reference video frames."
    }
    return $command.Source
}

function Export-VideoFrames {
    param(
        [string]$InputPath,
        [string]$OutputDir,
        [int]$FirstFrame,
        [int]$Count
    )

    [System.IO.Directory]::CreateDirectory($OutputDir) | Out-Null
    $ffmpeg = Resolve-Ffmpeg
    $pattern = Join-Path $OutputDir "resolve_reference_%05d.png"
    $filter = "select=gte(n\,$FirstFrame)"
    & $ffmpeg -hide_banner -y -i $InputPath -vf $filter -frames:v $Count $pattern | Out-Null
    if ($LASTEXITCODE -ne 0) {
        throw "ffmpeg failed while extracting reference frames from $InputPath"
    }
    $frames = @(Get-ChildItem -LiteralPath $OutputDir -Filter "*.png" -File | Sort-Object -Property Name)
    if ($frames.Count -eq 0) {
        throw "ffmpeg produced no reference frames in $OutputDir"
    }
    return $frames
}

function Get-ReferenceFrames {
    param(
        [string]$Path,
        [string]$OutputDir,
        [int]$FirstFrame,
        [int]$Count
    )

    if ([string]::IsNullOrWhiteSpace($Path)) {
        return @()
    }

    $resolved = Resolve-FullPath -Path $Path
    if (-not (Test-Path -LiteralPath $resolved)) {
        throw "Resolve reference path not found: $resolved"
    }

    if ((Get-Item -LiteralPath $resolved).PSIsContainer) {
        return @(Get-ChildItem -LiteralPath $resolved -File |
                Where-Object { $_.Extension -match '^\.(png|jpg|jpeg|tif|tiff)$' } |
                Sort-Object -Property Name |
                Select-Object -First $Count)
    }

    $extension = [System.IO.Path]::GetExtension($resolved).ToLowerInvariant()
    if ($extension -in @(".png", ".jpg", ".jpeg", ".tif", ".tiff")) {
        return @((Get-Item -LiteralPath $resolved))
    }

    return Export-VideoFrames -InputPath $resolved `
        -OutputDir $OutputDir `
        -FirstFrame $FirstFrame `
        -Count $Count
}

function Get-ResolveAutomationInfo {
    $resolvePath = Join-Path $env:ProgramFiles "Blackmagic Design\DaVinci Resolve\Resolve.exe"
    $fuscriptPath = Join-Path $env:ProgramFiles "Blackmagic Design\DaVinci Resolve\fuscript.exe"
    $scriptingApiPath = Join-Path $env:PROGRAMDATA "Blackmagic Design\DaVinci Resolve\Support\Developer\Scripting"

    return [ordered]@{
        resolve_path = if (Test-Path -LiteralPath $resolvePath) { $resolvePath } else { "" }
        fuscript_path = if (Test-Path -LiteralPath $fuscriptPath) { $fuscriptPath } else { "" }
        scripting_api_path = $scriptingApiPath
        scripting_api_available = (Test-Path -LiteralPath $scriptingApiPath)
        note = "Pass -ResolveReferencePath with a Resolve render when Resolve scripting is unavailable."
    }
}

function New-AdobeJordanProject {
    param(
        [string]$AfterFx,
        [string]$Method,
        [int]$UpscaleChoice,
        [string]$MatchName,
        [string]$ProjectPath,
        [string]$Source,
        [string]$Hint,
        [string]$OutputPattern,
        [string]$FixtureReportPath,
        [int]$Quality,
        [int]$InputColorSpace,
        [int]$OutputMode,
        [int]$QualityFallback,
        [int]$CoarseResolution,
        [int]$RecoverOriginalDetails,
        [int]$DetailsEdgeShrink,
        [int]$DetailsEdgeFeather,
        [int]$FirstFrame,
        [int]$Count,
        [int]$BitsPerChannel,
        [bool]$CompositeMagenta,
        [int]$TimeoutSeconds
    )

    $jsxPath = [System.IO.Path]::ChangeExtension($ProjectPath, ".jsx")
    $compositeMagentaJs = if ($CompositeMagenta) { "true" } else { "false" }
    $jsx = @"
function jsonEscape(value) {
    return String(value).replace(/\\/g, "\\\\").replace(/"/g, "\\\"").replace(/\r/g, "\\r").replace(/\n/g, "\\n");
}
function writeReport(status, fields) {
    var file = new File("$(ConvertTo-JsxPath -Path $FixtureReportPath)");
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
function importFootage(path, label) {
    var file = new File(path);
    if (!file.exists) {
        throw new Error(label + " not found: " + path);
    }
    return app.project.importFile(new ImportOptions(file));
}
try {
    app.beginSuppressDialogs();
    app.newProject();
    app.project.bitsPerChannel = $BitsPerChannel;

    var sourceItem = importFootage("$(ConvertTo-JsxPath -Path $Source)", "source");
    var hintItem = importFootage("$(ConvertTo-JsxPath -Path $Hint)", "alpha hint");
    var width = sourceItem.width;
    var height = sourceItem.height;
    var frameRate = sourceItem.frameRate;
    if (!isFinite(frameRate) || frameRate <= 0) {
        frameRate = 30;
    }
    var duration = sourceItem.duration;
    var neededDuration = ($FirstFrame + $Count) / frameRate;
    if (!isFinite(duration) || duration <= 0 || duration < neededDuration) {
        duration = neededDuration;
    }

    var comp = app.project.items.addComp("CorridorKey_Jordan_$Method", width, height, 1, duration, frameRate);
    var hintLayer = comp.layers.add(hintItem);
    hintLayer.name = "Alpha Hint Layer";
    hintLayer.guideLayer = true;
    var sourceLayer = comp.layers.add(sourceItem);
    sourceLayer.name = "Jordan4k Source";
    if ($compositeMagentaJs) {
        var backgroundLayer = comp.layers.addSolid([1.0, 0.0, 1.0], "Magenta Background", width, height, 1, duration);
        backgroundLayer.moveToEnd();
    }

    var effect = sourceLayer.property("ADBE Effect Parade").addProperty("$MatchName");
    if (effect === null) {
        throw new Error("Could not add CorridorKey effect by match name $MatchName");
    }
    effect.property("Alpha Hint Layer").setValue(hintLayer.index);
    effect.property("Input Color Space").setValue($InputColorSpace);
    effect.property("Output Mode").setValue($OutputMode);
    effect.property("Quality").setValue($Quality);
    effect.property("Upscale Method").setValue($UpscaleChoice);
    effect.property("Quality Fallback").setValue($QualityFallback);
    effect.property("Coarse Resolution Override").setValue($CoarseResolution);
    effect.property("Recover Original Details").setValue($RecoverOriginalDetails);
    effect.property("Details Edge Shrink").setValue($DetailsEdgeShrink);
    effect.property("Details Edge Feather").setValue($DetailsEdgeFeather);
    effect.property("Prepare Timeout (s)").setValue(120);
    effect.property("Render Timeout (s)").setValue(300);

    var rqItem = app.project.renderQueue.items.add(comp);
    rqItem.timeSpanStart = $FirstFrame / frameRate;
    rqItem.timeSpanDuration = Math.max(comp.frameDuration, $Count / frameRate);
    var outputModule = rqItem.outputModule(1);
    var outputTemplate = findTemplate(outputModule.templates, ["TIFF", "Photoshop"]);
    if (outputTemplate.length > 0) {
        outputModule.applyTemplate(outputTemplate);
    }
    outputModule.file = new File("$(ConvertTo-JsxPath -Path $OutputPattern)");

    app.project.save(new File("$(ConvertTo-JsxPath -Path $ProjectPath)"));
    writeReport("ok", {
        method: "$Method",
        effect_name: effect.name,
        match_name: effect.matchName,
        alpha_hint_layer_index: hintLayer.index,
        output_mode_choice: "$OutputMode",
        quality_choice: "$Quality",
        input_color_space_choice: "$InputColorSpace",
        quality_fallback_choice: "$QualityFallback",
        coarse_resolution_choice: "$CoarseResolution",
        upscale_choice: "$UpscaleChoice",
        recover_original_details_choice: "$RecoverOriginalDetails",
        details_edge_shrink: "$DetailsEdgeShrink",
        details_edge_feather: "$DetailsEdgeFeather",
        project_bits_per_channel: "$BitsPerChannel",
        composite_magenta_background: "$CompositeMagenta",
        output_template: outputTemplate,
        width: width,
        height: height,
        frame_rate: frameRate,
        project_path: "$(ConvertTo-JsxPath -Path $ProjectPath)",
        output_pattern: "$(ConvertTo-JsxPath -Path $OutputPattern)"
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

    Invoke-AfterEffectsScript -ExecutablePath $AfterFx `
        -ScriptPath $jsxPath `
        -SentinelPath $FixtureReportPath `
        -TimeoutSeconds $TimeoutSeconds

    $fixtureReport = Get-Content -LiteralPath $FixtureReportPath -Raw | ConvertFrom-Json
    if ([string]$fixtureReport.status -ne "ok") {
        throw "After Effects fixture creation failed for ${Method}: $($fixtureReport.message)"
    }
    return $fixtureReport
}

function Render-AdobeMethod {
    param(
        [string]$AfterFx,
        [string]$Aerender,
        [string]$Method,
        [int]$UpscaleChoice,
        [string]$MatchName,
        [string]$RootDir,
        [string]$Source,
        [string]$Hint,
        [object]$AeSettings,
        [int]$FirstFrame,
        [int]$Count
    )

    $methodDir = Join-Path $RootDir $Method
    $renderDir = Join-Path $methodDir "render"
    [System.IO.Directory]::CreateDirectory($renderDir) | Out-Null
    $projectPath = Join-Path $methodDir "corridorkey_jordan_${Method}.aep"
    $fixtureReportPath = Join-Path $methodDir "fixture_report.json"
    $aerenderLogPath = Join-Path $methodDir "aerender.log"
    $outputPattern = Join-Path $renderDir "corridorkey_jordan_${Method}_[#####].tif"

    $fixtureReport = New-AdobeJordanProject `
        -AfterFx $AfterFx `
        -Method $Method `
        -UpscaleChoice $UpscaleChoice `
        -MatchName $MatchName `
        -ProjectPath $projectPath `
        -Source $Source `
        -Hint $Hint `
        -OutputPattern $outputPattern `
        -FixtureReportPath $fixtureReportPath `
        -Quality ([int]$AeSettings.quality_choice) `
        -InputColorSpace ([int]$AeSettings.input_color_space_choice) `
        -OutputMode ([int]$AeSettings.output_mode_choice) `
        -QualityFallback ([int]$AeSettings.quality_fallback_choice) `
        -CoarseResolution ([int]$AeSettings.coarse_resolution_choice) `
        -RecoverOriginalDetails ([int]$AeSettings.recover_original_details_choice) `
        -DetailsEdgeShrink ([int]$AeSettings.details_edge_shrink) `
        -DetailsEdgeFeather ([int]$AeSettings.details_edge_feather) `
        -FirstFrame $FirstFrame `
        -Count $Count `
        -BitsPerChannel $ProjectBitsPerChannel `
        -CompositeMagenta $CompositeMagentaBackground.IsPresent `
        -TimeoutSeconds $ScriptTimeoutSeconds

    Invoke-Aerender -ExecutablePath $Aerender `
        -ProjectPath $projectPath `
        -LogPath $aerenderLogPath `
        -TimeoutSeconds $RenderTimeoutSeconds

    $frames = @(Wait-RenderedFrames -RenderDir $renderDir -ExpectedCount $Count -TimeoutSeconds 60)
    if ($frames.Count -lt $Count) {
        throw "aerender completed but produced $($frames.Count) of $Count final TIFF frames in $renderDir."
    }

    return [ordered]@{
        method = $Method
        upscale_choice = $UpscaleChoice
        fixture = $fixtureReport
        project_path = $projectPath
        aerender_log = $aerenderLogPath
        render_dir = $renderDir
        frame_paths = @($frames | ForEach-Object { $_.FullName })
    }
}

function Copy-ToArgb32Bitmap {
    param([System.Drawing.Bitmap]$Bitmap)

    $copy = [System.Drawing.Bitmap]::new(
        $Bitmap.Width,
        $Bitmap.Height,
        [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $graphics = [System.Drawing.Graphics]::FromImage($copy)
    try {
        $graphics.DrawImage($Bitmap, 0, 0, $Bitmap.Width, $Bitmap.Height)
    } finally {
        $graphics.Dispose()
    }
    return $copy
}

function Read-Argb32ImageBytes {
    param(
        [string]$Path,
        [int]$TargetWidth = 0,
        [int]$TargetHeight = 0
    )

    Add-Type -AssemblyName System.Drawing
    $original = [System.Drawing.Bitmap]::new($Path)
    $bitmap = $null
    $resized = $false
    $data = $null
    try {
        if ($TargetWidth -gt 0 -and $TargetHeight -gt 0 -and
            ($original.Width -ne $TargetWidth -or $original.Height -ne $TargetHeight)) {
            $bitmap = [System.Drawing.Bitmap]::new(
                $TargetWidth,
                $TargetHeight,
                [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
            $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
            try {
                $graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
                $graphics.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
                $graphics.DrawImage($original, 0, 0, $TargetWidth, $TargetHeight)
            } finally {
                $graphics.Dispose()
            }
            $resized = $true
        } else {
            $bitmap = Copy-ToArgb32Bitmap -Bitmap $original
        }
        $rect = [System.Drawing.Rectangle]::new(0, 0, $bitmap.Width, $bitmap.Height)
        $format = [System.Drawing.Imaging.PixelFormat]::Format32bppArgb
        $data = $bitmap.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::ReadOnly, $format)
        $stride = [Math]::Abs($data.Stride)
        $bytes = New-Object byte[] ($stride * $bitmap.Height)
        [System.Runtime.InteropServices.Marshal]::Copy($data.Scan0, $bytes, 0, $bytes.Length)
        return [pscustomobject]@{
            width = $bitmap.Width
            height = $bitmap.Height
            stride = $stride
            bytes = $bytes
            resized = $resized
            source_width = $original.Width
            source_height = $original.Height
        }
    } finally {
        if ($null -ne $data -and $null -ne $bitmap) {
            $bitmap.UnlockBits($data)
        }
        if ($null -ne $bitmap) {
            $bitmap.Dispose()
        }
        $original.Dispose()
    }
}

function Compare-ImagePair {
    param(
        [string]$ActualPath,
        [string]$ReferencePath,
        [int]$Stride
    )

    $reference = Read-Argb32ImageBytes -Path $ReferencePath
    $actual = Read-Argb32ImageBytes -Path $ActualPath `
        -TargetWidth $reference.width `
        -TargetHeight $reference.height

    $maxDelta = 0.0
    $sumDelta = 0.0
    $sumSquares = 0.0
    $sampleCount = 0
    $darkCount = 0
    $darkLumaSum = 0.0
    $largeDeltaCount = 0

    for ($y = 0; $y -lt $actual.height; $y += $Stride) {
        $rowActual = $y * $actual.stride
        $rowReference = $y * $reference.stride
        for ($x = 0; $x -lt $actual.width; $x += $Stride) {
            $offsetActual = $rowActual + ($x * 4)
            $offsetReference = $rowReference + ($x * 4)

            $bActual = [int]$actual.bytes[$offsetActual]
            $gActual = [int]$actual.bytes[$offsetActual + 1]
            $rActual = [int]$actual.bytes[$offsetActual + 2]
            $bReference = [int]$reference.bytes[$offsetReference]
            $gReference = [int]$reference.bytes[$offsetReference + 1]
            $rReference = [int]$reference.bytes[$offsetReference + 2]

            $dr = [Math]::Abs($rActual - $rReference)
            $dg = [Math]::Abs($gActual - $gReference)
            $db = [Math]::Abs($bActual - $bReference)
            $pixelMax = [Math]::Max($dr, [Math]::Max($dg, $db))
            $pixelMean = ($dr + $dg + $db) / 3.0
            $maxDelta = [Math]::Max($maxDelta, $pixelMax)
            $sumDelta += $pixelMean
            $sumSquares += ($dr * $dr) + ($dg * $dg) + ($db * $db)

            $actualLuma = (0.2126 * $rActual) + (0.7152 * $gActual) + (0.0722 * $bActual)
            $referenceLuma = (0.2126 * $rReference) + (0.7152 * $gReference) + (0.0722 * $bReference)
            $lumaBias = $actualLuma - $referenceLuma
            if ($lumaBias -lt -8.0) {
                ++$darkCount
                $darkLumaSum += -$lumaBias
            }
            if ($pixelMax -gt 16.0) {
                ++$largeDeltaCount
            }
            ++$sampleCount
        }
    }

    $safeSamples = [Math]::Max(1, $sampleCount)
    return [ordered]@{
        actual_path = $ActualPath
        reference_path = $ReferencePath
        width = $actual.width
        height = $actual.height
        actual_source_width = $actual.source_width
        actual_source_height = $actual.source_height
        reference_source_width = $reference.source_width
        reference_source_height = $reference.source_height
        actual_resized_to_reference = $actual.resized
        pixel_stride = $Stride
        sampled_pixels = $sampleCount
        max_abs_rgb_delta = [Math]::Round($maxDelta, 3)
        mean_abs_rgb_delta = [Math]::Round($sumDelta / $safeSamples, 3)
        rms_rgb_delta = [Math]::Round([Math]::Sqrt($sumSquares / ($safeSamples * 3.0)), 3)
        dark_pixel_fraction = [Math]::Round($darkCount / $safeSamples, 6)
        mean_dark_luma_delta = [Math]::Round($darkLumaSum / [Math]::Max(1, $darkCount), 3)
        large_delta_fraction = [Math]::Round($largeDeltaCount / $safeSamples, 6)
    }
}

function Compare-FrameSets {
    param(
        [object[]]$ActualFrames,
        [object[]]$ReferenceFrames,
        [string]$Label,
        [int]$Stride
    )

    $count = [Math]::Min($ActualFrames.Count, $ReferenceFrames.Count)
    if ($count -le 0) {
        throw "No frames available for comparison: $Label"
    }

    $frames = @()
    $maxDelta = 0.0
    $meanDeltaSum = 0.0
    $darkFractionSum = 0.0
    $largeFractionSum = 0.0
    for ($index = 0; $index -lt $count; ++$index) {
        $metrics = Compare-ImagePair `
            -ActualPath $ActualFrames[$index].FullName `
            -ReferencePath $ReferenceFrames[$index].FullName `
            -Stride $Stride
        $frames += $metrics
        $maxDelta = [Math]::Max($maxDelta, [double]$metrics.max_abs_rgb_delta)
        $meanDeltaSum += [double]$metrics.mean_abs_rgb_delta
        $darkFractionSum += [double]$metrics.dark_pixel_fraction
        $largeFractionSum += [double]$metrics.large_delta_fraction
    }

    return [ordered]@{
        label = $Label
        compared_frames = $count
        max_abs_rgb_delta = [Math]::Round($maxDelta, 3)
        mean_abs_rgb_delta = [Math]::Round($meanDeltaSum / $count, 3)
        mean_dark_pixel_fraction = [Math]::Round($darkFractionSum / $count, 6)
        mean_large_delta_fraction = [Math]::Round($largeFractionSum / $count, 6)
        frames = $frames
    }
}

$source = Resolve-FullPath -Path $SourcePath
$hint = Resolve-FullPath -Path $AlphaHintPath
if (-not (Test-Path -LiteralPath $source)) {
    throw "Source fixture not found: $source"
}
if (-not (Test-Path -LiteralPath $hint)) {
    throw "Alpha hint fixture not found: $hint"
}
if ($StartFrame -lt 0) {
    throw "StartFrame must be non-negative."
}

$fusionSettings = Get-FusionCompositionSettings -Path $FusionCompositionPath
$aeSettings = [ordered]@{
    quality_choice = if ($QualityChoice -gt 0) { $QualityChoice } elseif ($fusionSettings.applied) { [int]$fusionSettings.ae_quality_choice } else { 5 }
    input_color_space_choice = if ($InputColorSpaceChoice -gt 0) { $InputColorSpaceChoice } elseif ($fusionSettings.applied) { [int]$fusionSettings.ae_input_color_space_choice } else { 1 }
    output_mode_choice = if ($OutputModeChoice -gt 0) { $OutputModeChoice } elseif ($fusionSettings.applied) { [int]$fusionSettings.ae_output_mode_choice } else { 1 }
    quality_fallback_choice = if ($QualityFallbackChoice -gt 0) { $QualityFallbackChoice } elseif ($fusionSettings.applied) { [int]$fusionSettings.ae_quality_fallback_choice } else { 1 }
    coarse_resolution_choice = if ($CoarseResolutionChoice -gt 0) { $CoarseResolutionChoice } elseif ($fusionSettings.applied) { [int]$fusionSettings.ae_coarse_resolution_choice } else { 1 }
    recover_original_details_choice = if ($RecoverOriginalDetailsChoice -ge 0) { $RecoverOriginalDetailsChoice } elseif ($fusionSettings.applied) { [int]$fusionSettings.ae_recover_original_details_choice } else { 1 }
    details_edge_shrink = if ($DetailsEdgeShrink -ge 0) { $DetailsEdgeShrink } elseif ($fusionSettings.applied) { [int]$fusionSettings.ae_details_edge_shrink } else { 3 }
    details_edge_feather = if ($DetailsEdgeFeather -ge 0) { $DetailsEdgeFeather } elseif ($fusionSettings.applied) { [int]$fusionSettings.ae_details_edge_feather } else { 7 }
}

if ([string]::IsNullOrWhiteSpace($WorkDir)) {
    $workRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("corridorkey-adobe-jordan-fixture-" + [System.Guid]::NewGuid().ToString("N"))
} else {
    $workRoot = Resolve-FullPath -Path $WorkDir
}
[System.IO.Directory]::CreateDirectory($workRoot) | Out-Null

if ([string]::IsNullOrWhiteSpace($ReportPath)) {
    $ReportPath = Join-Path $workRoot "adobe_jordan_fixture_comparison.json"
}
$ReportPath = Resolve-FullPath -Path $ReportPath

$report = [ordered]@{
    validation_passed = $false
    fixture = "jordan4k_alpha_hint"
    source_path = $source
    alpha_hint_path = $hint
    fusion_composition = $fusionSettings
    ae_settings = $aeSettings
    work_dir = $workRoot
    start_frame = $StartFrame
    frame_count = $FrameCount
    pixel_stride = $PixelStride
    project_bits_per_channel = $ProjectBitsPerChannel
    effect_component = $EffectComponent
    resolve_automation = Get-ResolveAutomationInfo
    adobe_renders = [ordered]@{}
    comparisons = [ordered]@{}
    composite_magenta_background = $CompositeMagentaBackground.IsPresent
}

try {
    Assert-AfterEffectsNotRunning
    $afterFx = Resolve-AfterEffectsTool -ProvidedPath $AfterFxPath -CandidateNames @("AfterFX.com", "AfterFX.exe")
    $aerender = Resolve-AfterEffectsTool -ProvidedPath $AerenderPath -CandidateNames @("aerender.exe")
    $report.afterfx_path = $afterFx
    $report.aerender_path = $aerender

    $matchName = if ($EffectComponent -eq "blue") { "com.corridorkey.effect.blue" } else { "com.corridorkey.effect" }
    $adobeRoot = Join-Path $workRoot "adobe"
    [System.IO.Directory]::CreateDirectory($adobeRoot) | Out-Null

    $lanczos = Render-AdobeMethod `
        -AfterFx $afterFx `
        -Aerender $aerender `
        -Method "lanczos4" `
        -UpscaleChoice 1 `
        -MatchName $matchName `
        -RootDir $adobeRoot `
        -Source $source `
        -Hint $hint `
        -AeSettings $aeSettings `
        -FirstFrame $StartFrame `
        -Count $FrameCount
    $bilinear = Render-AdobeMethod `
        -AfterFx $afterFx `
        -Aerender $aerender `
        -Method "bilinear" `
        -UpscaleChoice 2 `
        -MatchName $matchName `
        -RootDir $adobeRoot `
        -Source $source `
        -Hint $hint `
        -AeSettings $aeSettings `
        -FirstFrame $StartFrame `
        -Count $FrameCount

    $report.adobe_renders.lanczos4 = $lanczos
    $report.adobe_renders.bilinear = $bilinear

    $lanczosFrames = @($lanczos.frame_paths | ForEach-Object { Get-Item -LiteralPath $_ })
    $bilinearFrames = @($bilinear.frame_paths | ForEach-Object { Get-Item -LiteralPath $_ })
    $report.comparisons.adobe_bilinear_vs_lanczos4 = Compare-FrameSets `
        -ActualFrames $bilinearFrames `
        -ReferenceFrames $lanczosFrames `
        -Label "adobe_bilinear_vs_lanczos4" `
        -Stride $PixelStride

    if (-not [string]::IsNullOrWhiteSpace($ResolveReferencePath)) {
        $referenceFrames = @(Get-ReferenceFrames `
                -Path $ResolveReferencePath `
                -OutputDir (Join-Path $workRoot "resolve_reference_frames") `
                -FirstFrame $StartFrame `
                -Count $FrameCount)
        if ($referenceFrames.Count -eq 0) {
            throw "Resolve reference path produced no comparable frames."
        }
        $report.resolve_reference_path = Resolve-FullPath -Path $ResolveReferencePath
        $report.resolve_reference_frames = @($referenceFrames | ForEach-Object { $_.FullName })
        $report.comparisons.adobe_lanczos4_vs_resolve = Compare-FrameSets `
            -ActualFrames $lanczosFrames `
            -ReferenceFrames $referenceFrames `
            -Label "adobe_lanczos4_vs_resolve" `
            -Stride $PixelStride
        $report.comparisons.adobe_bilinear_vs_resolve = Compare-FrameSets `
            -ActualFrames $bilinearFrames `
            -ReferenceFrames $referenceFrames `
            -Label "adobe_bilinear_vs_resolve" `
            -Stride $PixelStride
    } elseif ($RequireResolveReference.IsPresent) {
        throw "Pass -ResolveReferencePath with a Resolve render, or omit -RequireResolveReference."
    } else {
        $report.resolve_reference_path = ""
        $report.resolve_reference_frames = @()
    }

    $report.validation_passed = $true
    Write-CorridorKeyJsonFile -Path $ReportPath -Payload $report
    Write-Host "[PASS] Adobe Jordan fixture comparison report: $ReportPath" -ForegroundColor Green
} catch {
    $report.error = $_.Exception.Message
    Write-CorridorKeyJsonFile -Path $ReportPath -Payload $report
    throw
}
