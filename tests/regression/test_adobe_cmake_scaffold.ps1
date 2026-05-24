Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)

if (-not $env:VCPKG_ROOT) {
    throw "VCPKG_ROOT must be set for the Adobe CMake scaffold regression test."
}

$vcpkgRoot = $env:VCPKG_ROOT
. (Join-Path $repoRoot "scripts\windows_runtime_helpers.ps1")
Initialize-CorridorKeyMsvcEnvironment
$env:VCPKG_ROOT = $vcpkgRoot

$tempRoot = Join-Path $env:TEMP ("corridorkey-adobe-cmake-" + [guid]::NewGuid().ToString("N"))
$fakeSdk = Join-Path $tempRoot "after-effects-sdk"
$buildDir = Join-Path $tempRoot "build"
$toolchain = Join-Path $vcpkgRoot "scripts\buildsystems\vcpkg.cmake"
$ortRoot = Join-Path $repoRoot "vendor\onnxruntime-windows-rtx"

function Test-TextContainsPath {
    param(
        [Parameter(Mandatory = $true)]
        [string] $Text,
        [Parameter(Mandatory = $true)]
        [string] $Path
    )

    $forwardPath = $Path.Replace("\", "/")
    $backwardPath = $Path.Replace("/", "\")
    return $Text.Contains($forwardPath) -or $Text.Contains($backwardPath)
}

try {
    $missingSdk = Join-Path $tempRoot "missing-sdk"
    $missingBuildDir = Join-Path $tempRoot "missing-build"
    $missingConfigureLog = Join-Path $tempRoot "missing-configure.log"
    New-Item -ItemType Directory -Path $missingSdk -Force | Out-Null

    $missingSdkArgs = @(
        "-S"
        $repoRoot
        "-B"
        $missingBuildDir
        "-G"
        "Ninja"
        "-DCMAKE_TOOLCHAIN_FILE=$toolchain"
        "-DVCPKG_MANIFEST_MODE=ON"
        "-DCMAKE_BUILD_TYPE=Debug"
        "-DCORRIDORKEY_WINDOWS_ORT_ROOT=$ortRoot"
        "-DCORRIDORKEY_ENABLE_ADOBE_PLUGIN=ON"
        "-DCORRIDORKEY_ADOBE_SDK_ROOT=$missingSdk"
    )
    $nativeErrorPreferenceVariable = Get-Variable -Name PSNativeCommandUseErrorActionPreference -ErrorAction SilentlyContinue
    $previousNativeErrorPreference = $null
    if ($null -ne $nativeErrorPreferenceVariable) {
        $previousNativeErrorPreference = $nativeErrorPreferenceVariable.Value
        $PSNativeCommandUseErrorActionPreference = $false
    }
    $previousErrorActionPreference = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try {
        & cmake.exe @missingSdkArgs *> $missingConfigureLog
        $missingConfigureExitCode = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $previousErrorActionPreference
        if ($null -ne $nativeErrorPreferenceVariable) {
            $PSNativeCommandUseErrorActionPreference = $previousNativeErrorPreference
        }
    }
    if ($missingConfigureExitCode -eq 0) {
        throw "Adobe-enabled configure must fail when the requested SDK root is incomplete."
    }

    $fakeSdkIncludeDir = Join-Path $fakeSdk "Headers"
    $fakeSdkResourceDir = Join-Path $fakeSdk "Resources"
    New-Item -ItemType Directory -Path $fakeSdkIncludeDir -Force | Out-Null
    New-Item -ItemType Directory -Path $fakeSdkResourceDir -Force | Out-Null

    Set-Content -Path (Join-Path $fakeSdkIncludeDir "AEConfig.h") -Value @"
#define AE_OS_WIN 1
#define AE_PROC_INTELx64 1
"@ -Encoding Ascii
    Set-Content -Path (Join-Path $fakeSdkIncludeDir "AE_EffectVers.h") -Value @"
#define PF_PLUG_IN_VERSION 13
#define PF_PLUG_IN_SUBVERS 28
"@ -Encoding Ascii
    Set-Content -Path (Join-Path $fakeSdkIncludeDir "AE_Effect.h") -Value @"
#pragma once
using A_long = int;
using A_short = short;
using PF_Boolean = char;
using PF_Cmd = int;
using PF_Err = int;
using PF_FpShort = double;
using PF_OutFlags = int;
using PF_OutFlags2 = int;
using PF_ParamFlags = int;
using PF_ParamIndex = int;
using PF_ParamType = int;
using PF_ParamValue = int;
using PF_Precision = int;
using PF_ProgPtr = void*;
constexpr PF_Cmd PF_Cmd_ABOUT = 0;
constexpr PF_Cmd PF_Cmd_GLOBAL_SETUP = 1;
constexpr PF_Cmd PF_Cmd_GLOBAL_SETDOWN = 2;
constexpr PF_Cmd PF_Cmd_PARAMS_SETUP = 3;
constexpr PF_Cmd PF_Cmd_SEQUENCE_SETUP = 4;
constexpr PF_Cmd PF_Cmd_SEQUENCE_RESETUP = 5;
constexpr PF_Cmd PF_Cmd_SEQUENCE_SETDOWN = 6;
constexpr PF_Cmd PF_Cmd_RENDER = 7;
constexpr PF_Cmd PF_Cmd_SMART_PRE_RENDER = 8;
constexpr PF_Cmd PF_Cmd_SMART_RENDER = 9;
constexpr PF_Err PF_Err_NONE = 0;
constexpr PF_Err PF_Err_INTERNAL_STRUCT_DAMAGED = 1;
constexpr PF_Err PF_Err_BAD_CALLBACK_PARAM = 2;
constexpr PF_ParamFlags PF_ParamFlag_NONE = 0;
constexpr PF_ParamFlags PF_ParamFlag_CANNOT_INTERP = 1 << 2;
constexpr PF_ParamType PF_Param_POPUP = 1;
constexpr PF_ParamType PF_Param_FLOAT_SLIDER = 2;
constexpr PF_ParamType PF_Param_CHECKBOX = 3;
constexpr PF_Precision PF_Precision_INTEGER = 0;
constexpr PF_Precision PF_Precision_HUNDREDTHS = 2;
#define PF_VERSION(vers, subvers, bugvers, stage, build) ((vers) + (subvers) + (bugvers) + (stage) + (build))
#define PF_DEF_NAME name_do_not_use_directly
#define PF_DEF_NAMEPTR nameptr
#define PF_DEF_NAMESPTR namesptr
struct PF_PopupDef {
    PF_ParamValue value;
    A_short num_choices;
    A_short dephault;
    union {
        const char* PF_DEF_NAMESPTR;
    } u;
};
struct PF_FloatSliderDef {
    PF_FpShort value;
    PF_FpShort valid_min;
    PF_FpShort valid_max;
    PF_FpShort slider_min;
    PF_FpShort slider_max;
    PF_FpShort dephault;
    PF_Precision precision;
};
struct PF_CheckBoxDef {
    PF_ParamValue value;
    PF_Boolean dephault;
    union {
        const char* PF_DEF_NAMEPTR;
    } u;
};
union PF_ParamDefUnion {
    PF_PopupDef pd;
    PF_FloatSliderDef fs_d;
    PF_CheckBoxDef bd;
};
struct PF_ParamDef {
    union {
        A_long id;
    } uu;
    PF_ParamType param_type;
    char PF_DEF_NAME[64];
    PF_ParamFlags flags;
    PF_ParamDefUnion u;
};
using PF_ParamDefPtr = PF_ParamDef*;
struct PF_InteractCallbacks {
    PF_Err (*add_param)(PF_ProgPtr, PF_ParamIndex, PF_ParamDefPtr);
};
struct PF_InData {
    PF_InteractCallbacks inter;
    PF_ProgPtr effect_ref;
};
#define PF_ADD_PARAM(IN_DATA, INDEX, DEF) (*(IN_DATA)->inter.add_param)((IN_DATA)->effect_ref, (INDEX), (DEF))
struct PF_OutData {
    A_long my_version;
    A_long num_params;
    PF_OutFlags out_flags;
    PF_OutFlags2 out_flags2;
    char return_msg[256];
};
struct PF_LayerDef {};
"@ -Encoding Ascii
    foreach ($header in @("AE_EffectCB.h", "AE_Macros.h", "entry.h")) {
        Set-Content -Path (Join-Path $fakeSdkIncludeDir $header) -Value "" -Encoding Ascii
    }

    $fakePiplToolExe = Join-Path $fakeSdkResourceDir "PiPLTool.exe"
    Set-Content -Path $fakePiplToolExe -Value "" -Encoding Ascii

    $cmakeArgs = @(
        "-S"
        $repoRoot
        "-B"
        $buildDir
        "-G"
        "Ninja"
        "-DCMAKE_TOOLCHAIN_FILE=$toolchain"
        "-DVCPKG_MANIFEST_MODE=ON"
        "-DCMAKE_BUILD_TYPE=Debug"
        "-DCORRIDORKEY_WINDOWS_ORT_ROOT=$ortRoot"
        "-DCORRIDORKEY_ENABLE_ADOBE_PLUGIN=ON"
        "-DCORRIDORKEY_ADOBE_SDK_ROOT=$fakeSdk"
    )
    & cmake.exe @cmakeArgs

    if ($LASTEXITCODE -ne 0) {
        throw "CMake configure failed for the Adobe-enabled scaffold."
    }

    $ninjaFile = Join-Path $buildDir "build.ninja"
    if (-not (Test-Path $ninjaFile)) {
        throw "Expected Ninja build file at $ninjaFile."
    }

    $ninjaText = Get-Content -Path $ninjaFile -Raw
    if ($ninjaText -notmatch "corridorkey_adobe_green" -or
        $ninjaText -notmatch "corridorkey_adobe_blue") {
        throw "Expected the Adobe-enabled configure to create Green and Blue Adobe targets."
    }
    if ($ninjaText -notmatch "corridorkey_adobe_green\.aex" -or
        $ninjaText -notmatch "corridorkey_adobe_blue\.aex") {
        throw "Expected the Adobe target scaffold to configure Green and Blue .aex suffixes."
    }
    if (-not (Test-TextContainsPath -Text $ninjaText -Path $fakeSdkIncludeDir)) {
        throw "Expected the Adobe target scaffold to include the fake SDK headers."
    }
    if (-not (Test-TextContainsPath -Text $ninjaText -Path $fakePiplToolExe)) {
        throw "Expected the Adobe target scaffold to wire the fake PiPLTool path."
    }
    if ($ninjaText -notmatch "CorridorKeyRunAdobePipl\.cmake") {
        throw "Expected the Adobe target scaffold to generate the PiPL resource rule."
    }

    $generatedPiplSource = Join-Path $buildDir "src\plugins\adobe\corridorkey_adobe_green.r"
    $generatedBluePiplSource = Join-Path $buildDir "src\plugins\adobe\corridorkey_adobe_blue.r"
    if (-not (Test-Path $generatedPiplSource) -or -not (Test-Path $generatedBluePiplSource)) {
        throw "Expected Adobe Green and Blue PiPL source scaffolds."
    }
    $generatedPiplText = Get-Content -Path $generatedPiplSource -Raw
    $generatedBluePiplText = Get-Content -Path $generatedBluePiplSource -Raw
    if ($generatedPiplText -notmatch 'CodeWin64X86\s+\{\s+"EffectMain"\s+\}') {
        throw "Expected Adobe PiPL scaffold to declare EffectMain for Win64."
    }
    if ($generatedPiplText -notmatch 'AE_Effect_Match_Name\s+\{\s+"com\.corridorkey\.effect"\s+\}') {
        throw "Expected Adobe PiPL scaffold to contain the stable effect match name."
    }
    if ($generatedBluePiplText -notmatch 'AE_Effect_Match_Name\s+\{\s+"com\.corridorkey\.effect\.blue"\s+\}') {
        throw "Expected Adobe Blue PiPL scaffold to contain the stable Blue effect match name."
    }
    if ($generatedPiplText -notmatch 'AE_Effect_Support_URL\s+\{\s+"https://github\.com/alexandremendoncaalvaro/CorridorKey-Runtime/issues"\s+\}') {
        throw "Expected Adobe PiPL scaffold to contain the support URL."
    }
    if ($generatedPiplText -notmatch 'AE_Effect_Version\s+\{\s+524289\s+\}') {
        throw "Expected Adobe PiPL scaffold to use the PF_VERSION(1,0,0,0,1) encoded value."
    }

    Write-Host "[PASS] Adobe CMake scaffold regression checks passed." -ForegroundColor Green
} finally {
    if (Test-Path $tempRoot) {
        Remove-Item -Path $tempRoot -Recurse -Force
    }
}
