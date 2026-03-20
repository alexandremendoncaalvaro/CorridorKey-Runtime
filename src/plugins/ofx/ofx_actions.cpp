#include <array>
#include <cstring>
#include <vector>

#include "ofx_logging.hpp"
#include "ofx_shared.hpp"

namespace corridorkey::ofx {

namespace {

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

    define_group_param(param_set, "quality_group", "Quality", true);

    define_choice_param(param_set, kParamQualityMode, "Quality Mode", kQualityAuto,
                        {"Auto", "Preview (512)", "Standard (768)", "High (1024)", "Ultra (1536)",
                         "Maximum (2048)"},
                        "Inference resolution. Auto selects based on input size. "
                        "Higher values produce better detail at the cost of speed.",
                        "quality_group");
    define_bool_param(param_set, kParamEnableTiling, "Enable Tiling", 0,
                      "Process at native resolution using overlapping tiles. "
                      "Sharper details but slower.",
                      "quality_group");
    define_int_param(param_set, kParamTileOverlap, "Tile Overlap", 64, 8, 128,
                     "Pixel overlap between tiles for seamless blending.", "quality_group");

    define_group_param(param_set, "output_group", "Output", true);

    define_choice_param(param_set, kParamOutputMode, "Output Mode", kOutputProcessed,
                        {"Processed", "Matte Only", "Foreground Only", "Source + Matte"},
                        "What to output. Matte Only shows the alpha channel as grayscale.",
                        "output_group");
    define_choice_param(param_set, kParamUpscaleMethod, "Upscale Method", kUpscaleLanczos4,
                        {"Lanczos4", "Bilinear"},
                        "Method used to upscale model output to source resolution. "
                        "Lanczos4 is sharper; Bilinear is smoother.",
                        "output_group");

    define_group_param(param_set, "keying_group", "Keying", true);

    define_double_param(param_set, kParamDespillStrength, "Despill Strength", 0.5, 0.0, 1.0,
                        "Strength of green spill suppression.", "keying_group");
    define_bool_param(param_set, kParamAutoDespeckle, "Auto Despeckle", 0,
                      "Clean small alpha speckles automatically.", "keying_group");
    define_int_param(param_set, kParamDespeckleSize, "Despeckle Size", 400, 50, 2000,
                     "Minimum connected component area in pixels to keep.", "keying_group");
    define_double_param(param_set, kParamRefinerScale, "Refiner Scale", 1.0, 0.0, 3.0,
                        "Edge refinement strength. 0 disables the refiner.", "keying_group");
    define_bool_param(param_set, kParamSourcePassthrough, "Source Passthrough",
                      kDefaultSourcePassthroughEnabled,
                      "Blend original source pixels into high-confidence alpha regions "
                      "for sharper interior detail.",
                      "keying_group");
    define_int_param(param_set, kParamEdgeErode, "Edge Erode", kDefaultEdgeErode, 0, 10,
                     "Erosion radius for the passthrough interior mask. "
                     "Higher values widen the transition zone at edges.",
                     "keying_group");
    define_int_param(param_set, kParamEdgeBlur, "Edge Blur", kDefaultEdgeBlur, 0, 20,
                     "Blur radius for smoothing the passthrough transition. "
                     "Higher values create a softer blend between source and model.",
                     "keying_group");

    define_group_param(param_set, "alpha_group", "Alpha", true);

    define_double_param(param_set, kParamAlphaBlackPoint, "Alpha Black Point", 0.0, 0.0, 1.0,
                        "Remap alpha: values at or below this become fully transparent.",
                        "alpha_group");
    define_double_param(param_set, kParamAlphaWhitePoint, "Alpha White Point", 1.0, 0.0, 1.0,
                        "Remap alpha: values at or above this become fully opaque.", "alpha_group");
    define_double_param(param_set, kParamAlphaErode, "Alpha Erode/Dilate", 0.0, -10.0, 10.0,
                        "Shrink (negative) or expand (positive) the alpha edge in pixels.",
                        "alpha_group");
    define_double_param(param_set, kParamAlphaSoftness, "Alpha Edge Softness", 0.0, 0.0, 5.0,
                        "Blur the alpha edge to soften transitions.", "alpha_group");

    define_group_param(param_set, "color_group", "Color", true);

    define_double_param(param_set, kParamBrightness, "Brightness", 1.0, 0.5, 2.0,
                        "Foreground brightness adjustment applied in linear space.", "color_group");
    define_double_param(param_set, kParamSaturation, "Saturation", 1.0, 0.0, 2.0,
                        "Foreground saturation adjustment. 0 = grayscale, 1 = original.",
                        "color_group");

    define_group_param(param_set, "advanced_group", "Advanced", false);

    define_bool_param(param_set, kParamInputIsLinear, "Input Is Linear", 0,
                      "Disable sRGB to linear conversion for linear footage.", "advanced_group");

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

    std::string components_key =
        std::string("OfxImageClipPropComponents_") + kOfxImageEffectOutputClipName;
    std::string depth_key = std::string("OfxImageClipPropDepth_") + kOfxImageEffectOutputClipName;

    g_suites.property->propSetString(out_args, components_key.c_str(), 0, kOfxImageComponentRGBA);
    g_suites.property->propSetString(out_args, depth_key.c_str(), 0, depth_value);
    g_suites.property->propSetString(out_args, kOfxImageEffectPropPreMultiplication, 0,
                                     kOfxImagePreMultiplied);

    log_message("get_clip_preferences", "Clip preferences set.");
    return kOfxStatOK;
}

}  // namespace corridorkey::ofx
