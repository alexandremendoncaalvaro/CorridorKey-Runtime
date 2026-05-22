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

    New-Item -ItemType Directory -Path (Join-Path $fakeSdk "Headers") -Force | Out-Null
    New-Item -ItemType Directory -Path (Join-Path $fakeSdk "Resources") -Force | Out-Null

    Set-Content -Path (Join-Path $fakeSdk "Headers\AEConfig.h") -Value @"
#define AE_OS_WIN 1
#define AE_PROC_INTELx64 1
"@ -Encoding Ascii
    Set-Content -Path (Join-Path $fakeSdk "Headers\AE_EffectVers.h") -Value @"
#define PF_PLUG_IN_VERSION 13
#define PF_PLUG_IN_SUBVERS 28
"@ -Encoding Ascii
    Set-Content -Path (Join-Path $fakeSdk "Headers\AE_Effect.h") -Value @"
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
        Set-Content -Path (Join-Path $fakeSdk "Headers\$header") -Value "" -Encoding Ascii
    }

    $fakePiplToolSource = Join-Path $tempRoot "fake_pipl_tool.cpp"
    $fakePiplToolExe = Join-Path $fakeSdk "Resources\PiPLTool.exe"
    Set-Content -Path $fakePiplToolSource -Value @"
#include <fstream>

int main(int argc, char** argv) {
    if (argc < 3) {
        return 1;
    }
    std::ofstream output(argv[2], std::ios::binary);
    output << "16000 RCDATA { 0 }\n";
    return output ? 0 : 1;
}
"@ -Encoding Ascii
    & cl.exe /nologo /EHsc "/Fe:$fakePiplToolExe" $fakePiplToolSource
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to compile the fake PiPLTool test executable."
    }

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
    if ($ninjaText -notmatch "corridorkey_adobe") {
        throw "Expected the Adobe-enabled configure to create corridorkey_adobe."
    }
    if ($ninjaText -notmatch "corridorkey_adobe\.aex") {
        throw "Expected the Adobe plugin target to produce corridorkey_adobe.aex."
    }

    & cmake.exe --build $buildDir --target corridorkey_adobe
    if ($LASTEXITCODE -ne 0) {
        throw "Building corridorkey_adobe failed."
    }

    $pluginArtifact = Join-Path $buildDir "src\plugins\adobe\corridorkey_adobe.aex"
    if (-not (Test-Path $pluginArtifact)) {
        throw "Expected Adobe plugin artifact at $pluginArtifact."
    }

    $exports = & dumpbin.exe /nologo /exports $pluginArtifact
    if ($LASTEXITCODE -ne 0) {
        throw "dumpbin export inspection failed."
    }
    if (($exports -join "`n") -notmatch "\bEffectMain\b") {
        throw "Expected corridorkey_adobe.aex to export EffectMain."
    }

    $dependents = & dumpbin.exe /nologo /dependents $pluginArtifact
    if ($LASTEXITCODE -ne 0) {
        throw "dumpbin dependent inspection failed."
    }
    $dependentText = $dependents -join "`n"
    $forbiddenDependents = @(
        "onnxruntime",
        "cudart",
        "cuda",
        "nvcuda",
        "nvrtc",
        "nvToolsExt",
        "cublas",
        "cudnn",
        "nppc",
        "nppial",
        "nppicc",
        "nppidei",
        "nppif",
        "nppig",
        "nppim",
        "nppist",
        "nppisu",
        "nppitc",
        "npps",
        "nvinfer",
        "nvonnxparser",
        "torch",
        "torch_cuda",
        "c10"
    )
    foreach ($forbidden in $forbiddenDependents) {
        if ($dependentText -match $forbidden) {
            throw "Adobe plugin module must not depend on $forbidden."
        }
    }

    Write-Host "[PASS] Adobe CMake scaffold regression checks passed." -ForegroundColor Green
} finally {
    if (Test-Path $tempRoot) {
        Remove-Item -Path $tempRoot -Recurse -Force
    }
}
