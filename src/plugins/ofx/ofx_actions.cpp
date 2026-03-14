#include "ofx_shared.hpp"

#include <array>
#include <cstring>

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

}  // namespace

OfxStatus describe(OfxImageEffectHandle descriptor) {
    if (g_suites.property == nullptr || g_suites.image_effect == nullptr) {
        return kOfxStatErrMissingHostFeature;
    }

    OfxPropertySetHandle props = nullptr;
    if (g_suites.image_effect->getPropertySet(descriptor, &props) != kOfxStatOK) {
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
    g_suites.property->propSetIntN(props, kOfxPropVersion,
                                   static_cast<int>(version_parts.size()),
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

    return kOfxStatOK;
}

OfxStatus describe_in_context(OfxImageEffectHandle descriptor, const char* context) {
    if (g_suites.property == nullptr || g_suites.image_effect == nullptr ||
        g_suites.parameter == nullptr) {
        return kOfxStatErrMissingHostFeature;
    }
    if (context == nullptr || std::strcmp(context, kOfxImageEffectContextFilter) != 0) {
        return kOfxStatErrUnsupported;
    }

    OfxPropertySetHandle clip_props = nullptr;
    if (g_suites.image_effect->clipDefine(descriptor, kOfxImageEffectSimpleSourceClipName,
                                          &clip_props) != kOfxStatOK) {
        return kOfxStatFailed;
    }
    g_suites.property->propSetString(clip_props, kOfxImageEffectPropSupportedComponents, 0,
                                     kOfxImageComponentRGBA);
    g_suites.property->propSetInt(clip_props, kOfxImageClipPropOptional, 0, 0);

    if (g_suites.image_effect->clipDefine(descriptor, kOfxImageEffectOutputClipName,
                                          &clip_props) != kOfxStatOK) {
        return kOfxStatFailed;
    }
    g_suites.property->propSetString(clip_props, kOfxImageEffectPropSupportedComponents, 0,
                                     kOfxImageComponentRGBA);

    OfxParamSetHandle param_set = nullptr;
    if (g_suites.image_effect->getParamSet(descriptor, &param_set) != kOfxStatOK) {
        return kOfxStatFailed;
    }

    define_double_param(param_set, kParamDespillStrength, "Despill Strength", 1.0, 0.0, 1.0,
                        "Strength of green spill suppression.");
    define_bool_param(param_set, kParamAutoDespeckle, "Auto Despeckle", 0,
                      "Clean small alpha speckles automatically.");
    define_double_param(param_set, kParamRefinerScale, "Refiner Scale", 1.0, 0.5, 2.0,
                        "Refinement scale applied during inference.");
    define_bool_param(param_set, kParamInputIsLinear, "Input Is Linear", 0,
                      "Disable sRGB to linear conversion for linear footage.");

    return kOfxStatOK;
}

OfxStatus get_clip_preferences(OfxImageEffectHandle instance, OfxPropertySetHandle out_args) {
    if (out_args == nullptr || g_suites.property == nullptr || g_suites.image_effect == nullptr) {
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
    std::string depth_key =
        std::string("OfxImageClipPropDepth_") + kOfxImageEffectOutputClipName;

    g_suites.property->propSetString(out_args, components_key.c_str(), 0, kOfxImageComponentRGBA);
    g_suites.property->propSetString(out_args, depth_key.c_str(), 0, depth_value);

    return kOfxStatOK;
}

}  // namespace corridorkey::ofx
