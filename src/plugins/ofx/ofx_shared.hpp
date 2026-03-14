#pragma once

#include <corridorkey/types.hpp>
#include <corridorkey/version.hpp>
#include <filesystem>
#include <memory>
#include <string>

#include "ofxCore.h"
#include "ofxImageEffect.h"
#include "ofxMessage.h"
#include "ofxParam.h"
#include "ofxProperty.h"



namespace corridorkey {
class Engine;
}

namespace corridorkey::ofx {

constexpr const char* kPluginIdentifier = "com.corridorkey.resolve";
constexpr const char* kPluginLabel = "CorridorKey";
constexpr const char* kPluginGroup = "Keying";

constexpr const char* kParamDespillStrength = "despill_strength";
constexpr const char* kParamAutoDespeckle = "auto_despeckle";
constexpr const char* kParamRefinerScale = "refiner_scale";
constexpr const char* kParamInputIsLinear = "input_is_linear";

struct OfxSuites {
    const OfxPropertySuiteV1* property = nullptr;
    const OfxImageEffectSuiteV1* image_effect = nullptr;
    const OfxParameterSuiteV1* parameter = nullptr;
    const OfxMessageSuiteV2* message = nullptr;
};

struct InstanceData {
    OfxImageEffectHandle effect = nullptr;
    OfxImageClipHandle source_clip = nullptr;
    OfxImageClipHandle output_clip = nullptr;
    OfxParamHandle despill_param = nullptr;
    OfxParamHandle despeckle_param = nullptr;
    OfxParamHandle refiner_param = nullptr;
    OfxParamHandle input_is_linear_param = nullptr;
    std::unique_ptr<Engine> engine = nullptr;
    std::filesystem::path model_path = {};
    DeviceInfo device = {};
};

extern OfxHost* g_host;
extern OfxSuites g_suites;

bool fetch_suites();
void post_message(const char* message_type, const char* message, OfxImageEffectHandle effect);

InstanceData* get_instance_data(OfxImageEffectHandle instance);
void set_instance_data(OfxImageEffectHandle instance, InstanceData* data);

OfxStatus on_load();
OfxStatus describe(OfxImageEffectHandle descriptor);
OfxStatus describe_in_context(OfxImageEffectHandle descriptor, const char* context);
OfxStatus create_instance(OfxImageEffectHandle instance);
OfxStatus destroy_instance(OfxImageEffectHandle instance);
OfxStatus render(OfxImageEffectHandle instance, OfxPropertySetHandle in_args,
                 OfxPropertySetHandle out_args);
OfxStatus get_clip_preferences(OfxImageEffectHandle instance, OfxPropertySetHandle out_args);

}  // namespace corridorkey::ofx
