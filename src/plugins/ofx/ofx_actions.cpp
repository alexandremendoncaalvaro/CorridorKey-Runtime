#include <array>
#include <cstring>
#include <initializer_list>
#include <vector>

#include "common/ofx_runtime_defaults.hpp"
#include "ofx_logging.hpp"
#include "ofx_shared.hpp"

namespace corridorkey::ofx {

namespace {

std::string clip_property_key(const char* property_name, const char* clip_name) {
    return std::string(property_name) + "_" + clip_name;
}

void set_preferred_colourspaces(OfxPropertySetHandle props, const char* clip_name,
                                std::initializer_list<const char*> colourspaces) {
    const std::string property_key =
        clip_property_key(kOfxImageClipPropPreferredColourspaces, clip_name);
    int index = 0;
    for (const char* colourspace : colourspaces) {
        g_suites.property->propSetString(props, property_key.c_str(), index++, colourspace);
    }
}

void set_parent(OfxPropertySetHandle param_props, const char* parent) {
    if (parent != nullptr) {
        g_suites.property->propSetString(param_props, kOfxParamPropParent, 0, parent);
    }
}

void define_double_param(OfxParamSetHandle param_set, const char* name, const char* label,
                         double default_value, double min_value, double max_value, const char* hint,
                         const char* parent = nullptr) {
    OfxPropertySetHandle param_props = nullptr;
    if (g_suites.parameter->paramDefine(param_set, kOfxParamTypeDouble, name, &param_props) !=
        kOfxStatOK) {
        return;
    }

    g_suites.property->propSetString(param_props, kOfxPropLabel, 0, label);
    g_suites.property->propSetDouble(param_props, kOfxParamPropDefault, 0, default_value);
    g_suites.property->propSetDouble(param_props, kOfxParamPropMin, 0, min_value);
    g_suites.property->propSetDouble(param_props, kOfxParamPropMax, 0, max_value);
    g_suites.property->propSetDouble(param_props, kOfxParamPropDisplayMin, 0, min_value);
    g_suites.property->propSetDouble(param_props, kOfxParamPropDisplayMax, 0, max_value);
    if (hint != nullptr) {
        g_suites.property->propSetString(param_props, kOfxParamPropHint, 0, hint);
    }
    set_parent(param_props, parent);
}

void define_int_param(OfxParamSetHandle param_set, const char* name, const char* label,
                      int default_value, int min_value, int max_value, const char* hint,
                      const char* parent = nullptr) {
    OfxPropertySetHandle param_props = nullptr;
    if (g_suites.parameter->paramDefine(param_set, kOfxParamTypeInteger, name, &param_props) !=
        kOfxStatOK) {
        return;
    }

    g_suites.property->propSetString(param_props, kOfxPropLabel, 0, label);
    g_suites.property->propSetInt(param_props, kOfxParamPropDefault, 0, default_value);
    g_suites.property->propSetInt(param_props, kOfxParamPropMin, 0, min_value);
    g_suites.property->propSetInt(param_props, kOfxParamPropMax, 0, max_value);
    g_suites.property->propSetInt(param_props, kOfxParamPropDisplayMin, 0, min_value);
    g_suites.property->propSetInt(param_props, kOfxParamPropDisplayMax, 0, max_value);
    if (hint != nullptr) {
        g_suites.property->propSetString(param_props, kOfxParamPropHint, 0, hint);
    }
    set_parent(param_props, parent);
}

void define_bool_param(OfxParamSetHandle param_set, const char* name, const char* label,
                       int default_value, const char* hint, const char* parent = nullptr) {
    OfxPropertySetHandle param_props = nullptr;
    if (g_suites.parameter->paramDefine(param_set, kOfxParamTypeBoolean, name, &param_props) !=
        kOfxStatOK) {
        return;
    }

    g_suites.property->propSetString(param_props, kOfxPropLabel, 0, label);
    g_suites.property->propSetInt(param_props, kOfxParamPropDefault, 0, default_value);
    if (hint != nullptr) {
        g_suites.property->propSetString(param_props, kOfxParamPropHint, 0, hint);
    }
    set_parent(param_props, parent);
}

void define_choice_param(OfxParamSetHandle param_set, const char* name, const char* label,
                         int default_value, const std::vector<const char*>& options,
                         const char* hint, const char* parent = nullptr) {
    OfxPropertySetHandle param_props = nullptr;
    if (g_suites.parameter->paramDefine(param_set, kOfxParamTypeChoice, name, &param_props) !=
        kOfxStatOK) {
        return;
    }

    g_suites.property->propSetString(param_props, kOfxPropLabel, 0, label);
    g_suites.property->propSetInt(param_props, kOfxParamPropDefault, 0, default_value);
    for (int i = 0; i < static_cast<int>(options.size()); ++i) {
        g_suites.property->propSetString(param_props, kOfxParamPropChoiceOption, i, options[i]);
    }
    if (hint != nullptr) {
        g_suites.property->propSetString(param_props, kOfxParamPropHint, 0, hint);
    }
    set_parent(param_props, parent);
}

void define_runtime_status_param(OfxParamSetHandle param_set, const char* name, const char* label,
                                 const char* default_value, const char* hint,
                                 const char* parent = nullptr) {
    OfxPropertySetHandle param_props = nullptr;
    if (g_suites.parameter->paramDefine(param_set, kOfxParamTypeString, name, &param_props) !=
        kOfxStatOK) {
        return;
    }

    g_suites.property->propSetString(param_props, kOfxPropLabel, 0, label);
    g_suites.property->propSetString(param_props, kOfxParamPropDefault, 0, default_value);
    g_suites.property->propSetString(param_props, kOfxParamPropStringMode, 0,
                                     kRuntimeStatusStringMode);
    g_suites.property->propSetInt(param_props, kOfxParamPropEnabled, 0, kRuntimeStatusEnabled);
    g_suites.property->propSetInt(param_props, kOfxParamPropEvaluateOnChange, 0, 0);
    if (hint != nullptr) {
        g_suites.property->propSetString(param_props, kOfxParamPropHint, 0, hint);
    }
    set_parent(param_props, parent);
}

void define_push_button_param(OfxParamSetHandle param_set, const char* name, const char* label,
                              const char* hint, const char* parent = nullptr) {
    OfxPropertySetHandle param_props = nullptr;
    if (g_suites.parameter->paramDefine(param_set, kOfxParamTypePushButton, name, &param_props) !=
        kOfxStatOK) {
        return;
    }

    g_suites.property->propSetString(param_props, kOfxPropLabel, 0, label);
    if (hint != nullptr) {
        g_suites.property->propSetString(param_props, kOfxParamPropHint, 0, hint);
    }
    set_parent(param_props, parent);
}

void define_info_param(OfxParamSetHandle param_set, const char* name, const char* label,
                       const char* value, const char* hint, const char* parent = nullptr) {
    OfxPropertySetHandle param_props = nullptr;
    if (g_suites.parameter->paramDefine(param_set, kOfxParamTypeString, name, &param_props) !=
        kOfxStatOK) {
        return;
    }

    g_suites.property->propSetString(param_props, kOfxPropLabel, 0, label);
    g_suites.property->propSetString(param_props, kOfxParamPropDefault, 0, value);
    g_suites.property->propSetString(param_props, kOfxParamPropStringMode, 0,
                                     kOfxParamStringIsLabel);
    g_suites.property->propSetInt(param_props, kOfxParamPropEnabled, 0, 0);
    g_suites.property->propSetInt(param_props, kOfxParamPropEvaluateOnChange, 0, 0);
    if (hint != nullptr) {
        g_suites.property->propSetString(param_props, kOfxParamPropHint, 0, hint);
    }
    set_parent(param_props, parent);
}

void define_group_param(OfxParamSetHandle param_set, const char* name, const char* label, bool open,
                        const char* parent = nullptr) {
    OfxPropertySetHandle param_props = nullptr;
    if (g_suites.parameter->paramDefine(param_set, kOfxParamTypeGroup, name, &param_props) !=
        kOfxStatOK) {
        return;
    }

    g_suites.property->propSetString(param_props, kOfxPropLabel, 0, label);
    g_suites.property->propSetInt(param_props, kOfxParamPropGroupOpen, 0, open ? 1 : 0);
    if (parent != nullptr) {
        g_suites.property->propSetString(param_props, kOfxParamPropParent, 0, parent);
    }
}

}  // namespace

OfxStatus describe(OfxImageEffectHandle descriptor) {
    if (g_suites.property == nullptr || g_suites.image_effect == nullptr) {
        log_message("describe", "Missing property or image_effect suite.");
        return kOfxStatErrMissingHostFeature;
    }

    OfxPropertySetHandle props = nullptr;
    if (g_suites.image_effect->getPropertySet(descriptor, &props) != kOfxStatOK) {
        log_message("describe", "Failed to get property set.");
        return kOfxStatFailed;
    }

    std::string long_label = std::string(kPluginLabel) + " v" + CORRIDORKEY_VERSION_STRING;
    g_suites.property->propSetString(props, kOfxPropLabel, 0, kPluginLabel);
    g_suites.property->propSetString(props, kOfxPropShortLabel, 0, kPluginLabel);
    g_suites.property->propSetString(props, kOfxPropLongLabel, 0, long_label.c_str());
    std::string description =
        std::string("CorridorKey AI green screen keyer v") + CORRIDORKEY_VERSION_STRING;
    g_suites.property->propSetString(props, kOfxPropPluginDescription, 0, description.c_str());
    g_suites.property->propSetString(props, kOfxImageEffectPluginPropGrouping, 0, kPluginGroup);

    std::array<int, 3> version_parts = {CORRIDORKEY_VERSION_MAJOR, CORRIDORKEY_VERSION_MINOR,
                                        CORRIDORKEY_VERSION_PATCH};
    g_suites.property->propSetIntN(props, kOfxPropVersion, static_cast<int>(version_parts.size()),
                                   version_parts.data());
    g_suites.property->propSetString(props, kOfxPropVersionLabel, 0, CORRIDORKEY_VERSION_STRING);

    g_suites.property->propSetString(props, kOfxImageEffectPropSupportedContexts, 0,
                                     kOfxImageEffectContextFilter);
    const char* depths[] = {kOfxBitDepthFloat, kOfxBitDepthByte};
    g_suites.property->propSetStringN(props, kOfxImageEffectPropSupportedPixelDepths, 2, depths);
    g_suites.property->propSetString(props, kOfxImageEffectPluginRenderThreadSafety, 0,
                                     kOfxImageEffectRenderInstanceSafe);
    g_suites.property->propSetInt(props, kOfxImageEffectPluginPropHostFrameThreading, 0, 0);
    g_suites.property->propSetInt(props, kOfxImageEffectPropSupportsTiles, 0, 0);
    g_suites.property->propSetInt(props, kOfxImageEffectPropSupportsMultiResolution, 0, 1);
    g_suites.property->propSetInt(props, kOfxImageEffectPropTemporalClipAccess, 0, 0);
    g_suites.property->propSetString(props, kOfxImageEffectPropColourManagementStyle, 0,
                                     kOfxImageEffectColourManagementCore);
    g_suites.property->propSetString(props, kOfxImageEffectPropColourManagementAvailableConfigs, 0,
                                     kOfxConfigIdentifier);

    log_message("describe", "Describe completed.");
    return kOfxStatOK;
}

OfxStatus describe_in_context(OfxImageEffectHandle descriptor, const char* context) {
    if (g_suites.property == nullptr || g_suites.image_effect == nullptr ||
        g_suites.parameter == nullptr) {
        log_message("describe_in_context", "Missing required suites.");
        return kOfxStatErrMissingHostFeature;
    }
    if (context == nullptr || std::strcmp(context, kOfxImageEffectContextFilter) != 0) {
        log_message("describe_in_context", "Unsupported context.");
        return kOfxStatErrUnsupported;
    }

    OfxPropertySetHandle clip_props = nullptr;
    if (g_suites.image_effect->clipDefine(descriptor, kOfxImageEffectSimpleSourceClipName,
                                          &clip_props) != kOfxStatOK) {
        log_message("describe_in_context", "Failed to define source clip.");
        return kOfxStatFailed;
    }
    g_suites.property->propSetString(clip_props, kOfxImageEffectPropSupportedComponents, 0,
                                     kOfxImageComponentRGBA);
    g_suites.property->propSetInt(clip_props, kOfxImageClipPropOptional, 0, 0);

    if (g_suites.image_effect->clipDefine(descriptor, kClipAlphaHint, &clip_props) != kOfxStatOK) {
        log_message("describe_in_context", "Failed to define alpha hint clip.");
        return kOfxStatFailed;
    }
    g_suites.property->propSetString(clip_props, kOfxImageEffectPropSupportedComponents, 0,
                                     kOfxImageComponentRGBA);
    g_suites.property->propSetString(clip_props, kOfxImageEffectPropSupportedComponents, 1,
                                     kOfxImageComponentAlpha);
    g_suites.property->propSetString(clip_props, kOfxImageEffectPropSupportedComponents, 2,
                                     kOfxImageComponentRGB);
    g_suites.property->propSetInt(clip_props, kOfxImageClipPropOptional, 0, 1);

    if (g_suites.image_effect->clipDefine(descriptor, kOfxImageEffectOutputClipName, &clip_props) !=
        kOfxStatOK) {
        log_message("describe_in_context", "Failed to define output clip.");
        return kOfxStatFailed;
    }
    g_suites.property->propSetString(clip_props, kOfxImageEffectPropSupportedComponents, 0,
                                     kOfxImageComponentRGBA);

    OfxParamSetHandle param_set = nullptr;
    if (g_suites.image_effect->getParamSet(descriptor, &param_set) != kOfxStatOK) {
        log_message("describe_in_context", "Failed to get param set.");
        return kOfxStatFailed;
    }

    // --- Group 1: Status (runtime diagnostics and current version at the top) ---
    std::string runtime_group_label = std::string("CorridorKey v") + CORRIDORKEY_VERSION_STRING;
    define_group_param(param_set, "runtime_group", runtime_group_label.c_str(), true);

    define_runtime_status_param(
        param_set, kParamRuntimeProcessing, "Processing Backend", "Initializing...",
        "Shows the backend currently used by this OFX instance.", "runtime_group");
    define_runtime_status_param(
        param_set, kParamRuntimeDevice, "Processing Device", "Initializing...",
        "Shows the device selected for this OFX instance.", "runtime_group");
    define_runtime_status_param(
        param_set, kParamRuntimeRequestedQuality, "Requested Quality", "Initializing...",
        "Shows the quality mode currently requested by the OFX controls.", "runtime_group");
    define_runtime_status_param(
        param_set, kParamRuntimeEffectiveQuality, "Effective Quality", "Initializing...",
        "Shows the actual resolution currently used for inference after artifact selection.",
        "runtime_group");
    define_runtime_status_param(param_set, kParamRuntimeArtifact, "Loaded Artifact",
                                "Initializing...",
                                "Shows the actual model or bridge file loaded for the current "
                                "quality mode.",
                                "runtime_group");
    define_runtime_status_param(
        param_set, kParamRuntimeStatus, "Status", "Initializing...",
        "Shows the current runtime state, warnings, or the most recent error during engine load "
        "or render.",
        "runtime_group");
    define_runtime_status_param(
        param_set, kParamRuntimeTimings, "Frame Times", "Initializing...",
        "Shows the most recent frame time and rolling average for this OFX instance.",
        "runtime_group");

    // --- Group 2: Help & Docs (actionable links only) ---
    define_group_param(param_set, kParamHelpGroup, "Help & Docs", false);

    define_push_button_param(param_set, kParamOpenStartHereGuide, "Open Start Here Guide",
                             "Open the quick-start guide for CorridorKey in Resolve.",
                             kParamHelpGroup);
    define_push_button_param(param_set, kParamOpenQualityGuide, "Open Quality Guide",
                             "Open the quality and fallback guide for CorridorKey.",
                             kParamHelpGroup);
    define_push_button_param(param_set, kParamOpenAlphaHintGuide, "Open Alpha Hint Guide",
                             "Open the Alpha Hint setup guide and input-format reference.",
                             kParamHelpGroup);
    define_push_button_param(param_set, kParamOpenRecoverDetailsGuide, "Open Recover Details Guide",
                             "Open the Recover Original Details guide.", kParamHelpGroup);
    define_push_button_param(param_set, kParamOpenTilingGuide, "Open Tiling Guide",
                             "Open the tiling guide and trade-offs.", kParamHelpGroup);
    define_push_button_param(
        param_set, kParamOpenResolveTutorial, "Open Resolve Tutorial",
        "Open step-by-step CorridorKey workflows for DaVinci Resolve on GitHub.", kParamHelpGroup);
    define_push_button_param(param_set, kParamOpenTroubleshooting, "Open Troubleshooting",
                             "Open the troubleshooting guide on GitHub.", kParamHelpGroup);

    // --- Group 3: Key Setup (the two choices that determine the AI result) ---
    define_group_param(param_set, "setup_group", "Key Setup", true);

    define_choice_param(param_set, kParamScreenColor, "Screen Color", kDefaultScreenColor,
                        {"Green", "Blue"},
                        "Select the dominant screen color. Blue swaps channels internally so "
                        "the keyer treats blue screens like green screens.",
                        "setup_group");
    define_choice_param(
        param_set, kParamQualityMode, "Quality", kQualityPreview,
        {quality_mode_ui_label(kQualityAuto), quality_mode_ui_label(kQualityPreview),
         quality_mode_ui_label(kQualityStandard), quality_mode_ui_label(kQualityHigh),
         quality_mode_ui_label(kQualityUltra), quality_mode_ui_label(kQualityMaximum)},
        "Inference quality. Auto selects based on input size and hardware. "
        "Higher values produce better detail at the cost of speed. "
        "Resolutions: Draft (512), Standard (768), High (1024), Ultra (1536), "
        "Maximum (2048).",
        "setup_group");

    // --- Group 4: Interior Detail (recover opaque source texture, not edge fixes) ---
    define_group_param(param_set, "interior_detail_group", "Interior Detail", true);

    define_bool_param(param_set, kParamSourcePassthrough, "Recover Original Details",
                      kDefaultSourcePassthroughEnabled,
                      "Blend original source pixels back into opaque interior regions for "
                      "sharper texture. This is not an edge-fix tool. The recovered pixels still "
                      "flow through despill after the blend.",
                      "interior_detail_group");
    define_int_param(param_set, kParamEdgeErode, "Details Edge Shrink", kDefaultEdgeErode, 0,
                     kMaxEdgeErode,
                     "Shrink the recovered-details mask before blending source detail. "
                     "Values scale with the source long edge using a 1920px baseline.",
                     "interior_detail_group");
    define_int_param(param_set, kParamEdgeBlur, "Details Edge Feather", kDefaultEdgeBlur, 0,
                     kMaxEdgeBlur,
                     "Feather the recovered-details mask for a smoother handoff between source "
                     "detail and model foreground. Values scale with the source long edge using "
                     "a 1920px baseline.",
                     "interior_detail_group");

    // --- Group 5: Matte (refine the AI-generated alpha) ---
    define_group_param(param_set, "matte_group", "Matte", true);

    define_info_param(
        param_set, "alpha_hint_info", "Alpha Hint Input",
        "Connect a guide matte here. Current OFX behavior waits for Alpha Hint before keying.",
        "Current Resolve OFX behavior does not run inference until Alpha Hint is connected. "
        "Accepted formats: RGBA uses the alpha channel, Alpha uses the single channel directly, "
        "and RGB uses channel 0 (red). The controls below adjust the output matte generated by "
        "CorridorKey, not the incoming guide.\n\n"
        "Fusion: Connect your matte to the secondary 'Alpha Hint' pin.\n"
        "Color Page: Right-click this node -> 'Add OFX Input', then route a Qualifier or 3D Keyer "
        "output into the new green input.",
        "matte_group");
    define_double_param(param_set, kParamAlphaBlackPoint, "Matte Clip Black", 0.0, 0.0, 1.0,
                        "Remap matte: values at or below this become fully transparent.",
                        "matte_group");
    define_double_param(param_set, kParamAlphaWhitePoint, "Matte Clip White", 1.0, 0.0, 1.0,
                        "Remap matte: values at or above this become fully opaque.", "matte_group");
    define_double_param(param_set, kParamAlphaErode, "Matte Shrink/Grow", 0.0, -10.0, 10.0,
                        "Shrink (negative) or expand (positive) the matte edge. The effective "
                        "pixel radius scales with the source long edge using a 1920px baseline.",
                        "matte_group");
    define_double_param(param_set, kParamAlphaSoftness, "Matte Edge Blur", 0.0, 0.0, 5.0,
                        "Blur the matte edge to soften transitions. The effective pixel radius "
                        "scales with the source long edge using a 1920px baseline.",
                        "matte_group");
    define_double_param(param_set, kParamAlphaGamma, "Matte Gamma", 1.0, 0.1, 10.0,
                        "Non-linear matte curve. Values above 1.0 brighten semi-transparent "
                        "areas. Values below 1.0 darken and tighten them. Default 1.0 is "
                        "neutral.",
                        "matte_group");
    define_bool_param(param_set, kParamAutoDespeckle, "Auto Despeckle", 0,
                      "Clean small matte speckles automatically.", "matte_group");
    define_int_param(param_set, kParamDespeckleSize, "Min Region Size", 400, 50, 2000,
                     "Minimum connected component area in pixels to keep. "
                     "Regions smaller than this are removed.",
                     "matte_group");
    define_double_param(
        param_set, kParamTemporalSmoothing, "Temporal Smoothing", kDefaultTemporalSmoothing, 0.0,
        1.0, "Blend current output with the previous frame for temporal stability.", "matte_group");

    // --- Group 6: Edge & Spill (despill and boundary cleanup only) ---
    define_group_param(param_set, "edge_spill_group", "Edge & Spill", true);

    define_double_param(param_set, kParamDespillStrength, "Despill Strength", 0.5, 0.0, 1.0,
                        "Strength of screen color spill suppression on foreground edges.",
                        "edge_spill_group");
    define_choice_param(param_set, kParamSpillMethod, "Spill Method", kDefaultSpillMethod,
                        {"Average", "Double Limit", "Neutral"},
                        "How removed spill color is replaced. Average redistributes to "
                        "red and blue. Double Limit uses the stronger neighbor channel. "
                        "Neutral replaces with gray to avoid color shifts.",
                        "edge_spill_group");

    // --- Group 7: Output ---
    define_group_param(param_set, "output_group", "Output", true);

    define_choice_param(param_set, kParamOutputMode, "Output Mode", kOutputProcessed,
                        {"Processed", "Matte Only", "Foreground Only", "Source+Matte", "FG+Matte"},
                        "What to output.\n"
                        "Processed: CorridorKey's linear premultiplied RGBA output. "
                        "Matches the runtime's processed result and is safe for compositing.\n"
                        "Matte Only: alpha as grayscale.\n"
                        "Foreground Only: despilled foreground, full alpha.\n"
                        "Source+Matte: original source premultiplied by AI matte.\n"
                        "FG+Matte: explicit linear foreground+matte alias of Processed for "
                        "manual compositing workflows and backward compatibility.",
                        "output_group");
    define_choice_param(param_set, kParamUpscaleMethod, "Upscale Method", kUpscaleBilinear,
                        {"Lanczos4", "Bilinear"},
                        "Method used to upscale model output to source resolution. "
                        "Lanczos4 is sharper; Bilinear is smoother.",
                        "output_group");

    // --- Group 8: Performance ---
    define_group_param(param_set, "performance_group", "Performance", false);

    define_choice_param(
        param_set, kParamQuantizationMode, "Precision", kDefaultQuantizationMode,
        {"FP16 (Full)", "INT8 (Compact)"},
        "Model precision. FP16 (Full) selects highest quality files. "
        "INT8 (Compact) selects memory-efficient quantized files for faster inference.",
        "performance_group");
    define_bool_param(param_set, kParamEnableTiling, "Enable Tiling", 0,
                      "Process at native resolution using overlapping tiles. Use this when lower "
                      "quality modes lose too much detail or when you want sharper results than "
                      "the model resolution alone can provide. It is slower and increases memory "
                      "use.",
                      "performance_group");
    define_int_param(param_set, kParamTileOverlap, "Tile Overlap", 64, 8, 128,
                     "Pixel overlap between tiles for seam-safe blending. Larger values reduce "
                     "tile boundary artifacts at the cost of more work.",
                     "performance_group");
    define_choice_param(param_set, kParamInputColorSpace, "Input Color Space",
                        kDefaultInputColorSpace, {"sRGB", "Linear", "Auto (Host Managed)"},
                        "How CorridorKey should interpret the incoming source. Auto requests "
                        "host-managed color using sRGB Texture or Linear Rec.709 (sRGB). "
                        "Linear means Linear Rec.709 (sRGB), not an arbitrary project-linear "
                        "space. If host-managed color is unavailable, Auto falls back to the "
                        "manual Linear path.",
                        "performance_group");

    // --- Group 9: Advanced (timeouts) ---
    define_group_param(param_set, "advanced_group", "Advanced", false);

    define_choice_param(
        param_set, kParamQualityFallbackMode, "Quality Fallback", kQualityFallbackAuto,
        {quality_fallback_mode_ui_label(kQualityFallbackAuto),
         quality_fallback_mode_ui_label(kQualityFallbackDirect),
         quality_fallback_mode_ui_label(kQualityFallbackCoarseToFine)},
        "Advanced diagnostics override. Auto chooses the safest runtime path. Direct disables "
        "coarse-to-fine. Coarse to Fine forces the fallback path.",
        "advanced_group");
    define_choice_param(param_set, kParamRefinementMode, "Refinement Mode", kRefinementAuto,
                        {refinement_mode_ui_label(kRefinementAuto),
                         refinement_mode_ui_label(kRefinementFullFrame),
                         refinement_mode_ui_label(kRefinementTiled)},
                        "Advanced diagnostics override for validated refinement strategy "
                        "artifacts. Current packaged ONNX artifacts only support Auto.",
                        "advanced_group");
    define_choice_param(param_set, kParamCoarseResolutionOverride, "Coarse Resolution Override",
                        kCoarseResolutionAutomatic,
                        {"Automatic", "512", "768", "1024", "1536", "2048"},
                        "Advanced diagnostics override for the coarse artifact resolution.",
                        "advanced_group");

    define_int_param(param_set, kParamRenderTimeout, "Render Timeout (s)",
                     common::kDefaultOfxRenderTimeoutSeconds, 10, 300,
                     "Maximum time in seconds to wait for a single frame render. "
                     "Increase for high-resolution modes on slower hardware.",
                     "advanced_group");
    define_int_param(param_set, kParamPrepareTimeout, "Prepare Timeout (s)",
                     common::kDefaultOfxPrepareTimeoutSeconds, 30, 600,
                     "Maximum time in seconds to wait for model loading and bootstrap. "
                     "Increase if first-frame initialization times out.",
                     "advanced_group");

    log_message("describe_in_context", "Describe in context completed.");
    return kOfxStatOK;
}

OfxStatus get_clip_preferences(OfxImageEffectHandle instance, OfxPropertySetHandle out_args) {
    if (out_args == nullptr || g_suites.property == nullptr || g_suites.image_effect == nullptr) {
        log_message("get_clip_preferences", "Missing required suites or out_args.");
        return kOfxStatFailed;
    }

    InstanceData* data = get_instance_data(instance);
    const char* depth_value = kOfxBitDepthFloat;
    if (data != nullptr && data->source_clip != nullptr) {
        OfxPropertySetHandle source_props = nullptr;
        if (g_suites.image_effect->clipGetPropertySet(data->source_clip, &source_props) ==
            kOfxStatOK) {
            char* depth = nullptr;
            if (g_suites.property->propGetString(source_props, kOfxImageEffectPropPixelDepth, 0,
                                                 &depth) == kOfxStatOK &&
                depth != nullptr) {
                depth_value = depth;
            }
        }
    }

    const char* output_clips[] = {kOfxImageEffectOutputClipName};

    set_preferred_colourspaces(out_args, kOfxImageEffectSimpleSourceClipName,
                               {kOfxColourspaceSrgbTx, kOfxColourspaceLinRec709Srgb});
    set_preferred_colourspaces(out_args, kClipAlphaHint, {kOfxColourspaceRaw});

    for (const char* clip_name : output_clips) {
        std::string components_key = std::string("OfxImageClipPropComponents_") + clip_name;
        std::string depth_key = std::string("OfxImageClipPropDepth_") + clip_name;

        g_suites.property->propSetString(out_args, components_key.c_str(), 0,
                                         kOfxImageComponentRGBA);
        g_suites.property->propSetString(out_args, depth_key.c_str(), 0, depth_value);
        if (std::strcmp(clip_name, kOfxImageEffectOutputClipName) == 0) {
            std::string premult_key = std::string("OfxImageClipPropPreMultiplication_") + clip_name;
            g_suites.property->propSetString(out_args, premult_key.c_str(), 0,
                                             kOfxImagePreMultiplied);
        }
    }

    log_message("get_clip_preferences", "Clip preferences set.");
    return kOfxStatOK;
}

OfxStatus get_output_colourspace(OfxImageEffectHandle instance, OfxPropertySetHandle /*in_args*/,
                                 OfxPropertySetHandle out_args) {
    if (out_args == nullptr || g_suites.property == nullptr) {
        log_message("get_output_colourspace", "Missing out_args or property suite.");
        return kOfxStatFailed;
    }

    int output_mode = kOutputProcessed;
    if (InstanceData* data = get_instance_data(instance);
        data != nullptr && data->output_mode_param != nullptr && g_suites.parameter != nullptr) {
        g_suites.parameter->paramGetValue(data->output_mode_param, &output_mode);
    }

    g_suites.property->propSetString(out_args, kOfxImageClipPropColourspace, 0,
                                     output_colourspace_for_output_mode(output_mode));
    return kOfxStatOK;
}

}  // namespace corridorkey::ofx
