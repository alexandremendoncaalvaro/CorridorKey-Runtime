#include <array>
#include <cstring>
#include <vector>

#include "ofx_logging.hpp"
#include "ofx_shared.hpp"

namespace corridorkey::ofx {

namespace {

void define_double_param(OfxParamSetHandle param_set, const char* name, const char* label,
                         double default_value, double min_value, double max_value,
                         const char* hint) {
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
}

void define_int_param(OfxParamSetHandle param_set, const char* name, const char* label,
                      int default_value, int min_value, int max_value, const char* hint) {
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
}

void define_bool_param(OfxParamSetHandle param_set, const char* name, const char* label,
                       int default_value, const char* hint) {
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
}

void define_choice_param(OfxParamSetHandle param_set, const char* name, const char* label,
                         int default_value, const std::vector<const char*>& options,
                         const char* hint) {
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

    g_suites.property->propSetString(props, kOfxPropLabel, 0, kPluginLabel);
    g_suites.property->propSetString(props, kOfxPropShortLabel, 0, kPluginLabel);
    g_suites.property->propSetString(props, kOfxPropLongLabel, 0, kPluginLabel);
    g_suites.property->propSetString(props, kOfxPropPluginDescription, 0,
                                     "CorridorKey keyer for DaVinci Resolve.");
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

    define_choice_param(param_set, kParamQualityMode, "Quality Mode", kQualityAuto,
                        {"Auto", "Preview (512)", "Standard (768)", "High (1024)"},
                        "Inference resolution. Auto selects based on input size. "
                        "Higher values produce better detail at the cost of speed.");

    define_choice_param(param_set, kParamOutputMode, "Output Mode", kOutputProcessed,
                        {"Processed", "Matte Only", "Foreground Only", "Source + Matte"},
                        "What to output. Matte Only shows the alpha channel as grayscale.");

    define_double_param(param_set, kParamDespillStrength, "Despill Strength", 1.0, 0.0, 1.0,
                        "Strength of green spill suppression.");
    define_bool_param(param_set, kParamAutoDespeckle, "Auto Despeckle", 0,
                      "Clean small alpha speckles automatically.");
    define_int_param(param_set, kParamDespeckleSize, "Despeckle Size", 400, 50, 2000,
                     "Minimum connected component area in pixels to keep.");
    define_double_param(param_set, kParamRefinerScale, "Refiner Scale", 1.0, 0.0, 3.0,
                        "Edge refinement strength. 0 disables the refiner.");

    define_double_param(param_set, kParamAlphaBlackPoint, "Alpha Black Point", 0.0, 0.0, 1.0,
                        "Remap alpha: values at or below this become fully transparent.");
    define_double_param(param_set, kParamAlphaWhitePoint, "Alpha White Point", 1.0, 0.0, 1.0,
                        "Remap alpha: values at or above this become fully opaque.");
    define_double_param(param_set, kParamAlphaErode, "Alpha Erode/Dilate", 0.0, -10.0, 10.0,
                        "Shrink (negative) or expand (positive) the alpha edge in pixels.");
    define_double_param(param_set, kParamAlphaSoftness, "Alpha Edge Softness", 0.0, 0.0, 5.0,
                        "Blur the alpha edge to soften transitions.");

    define_bool_param(param_set, kParamInputIsLinear, "Input Is Linear", 0,
                      "Disable sRGB to linear conversion for linear footage.");

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
