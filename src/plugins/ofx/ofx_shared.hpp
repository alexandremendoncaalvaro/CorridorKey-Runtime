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

#if defined(_WIN32)
#define CORRIDORKEY_OFX_EXPORT OfxExport
#elif defined(__GNUC__)
#define CORRIDORKEY_OFX_EXPORT __attribute__((visibility("default")))
#else
#define CORRIDORKEY_OFX_EXPORT
#endif

namespace corridorkey {
class Engine;
}

namespace corridorkey::ofx {

constexpr const char* kPluginIdentifier = "com.corridorkey.resolve";
constexpr const char* kPluginLabel = "CorridorKey";
constexpr const char* kPluginGroup = "Keying";

constexpr const char* kClipAlphaHint = "Alpha Hint";

constexpr const char* kParamQualityMode = "quality_mode";
constexpr const char* kParamOutputMode = "output_mode";
constexpr const char* kParamDespillStrength = "despill_strength";
constexpr const char* kParamAutoDespeckle = "auto_despeckle";
constexpr const char* kParamDespeckleSize = "despeckle_size";
constexpr const char* kParamRefinerScale = "refiner_scale";
constexpr const char* kParamInputIsLinear = "input_is_linear";
constexpr const char* kParamAlphaBlackPoint = "alpha_black_point";
constexpr const char* kParamAlphaWhitePoint = "alpha_white_point";
constexpr const char* kParamAlphaErode = "alpha_erode";
constexpr const char* kParamAlphaSoftness = "alpha_softness";

// Quality mode choice indices
constexpr int kQualityAuto = 0;
constexpr int kQualityPreview = 1;
constexpr int kQualityStandard = 2;
constexpr int kQualityHigh = 3;

// Output mode choice indices
constexpr int kOutputProcessed = 0;
constexpr int kOutputMatteOnly = 1;
constexpr int kOutputForegroundOnly = 2;
constexpr int kOutputSourceMatte = 3;

struct OfxSuites {
    const OfxPropertySuiteV1* property = nullptr;
    const OfxImageEffectSuiteV1* image_effect = nullptr;
    const OfxParameterSuiteV1* parameter = nullptr;
    const OfxMessageSuiteV2* message = nullptr;
};

struct InstanceData {
    OfxImageEffectHandle effect = nullptr;
    OfxImageClipHandle source_clip = nullptr;
    OfxImageClipHandle alpha_hint_clip = nullptr;
    OfxImageClipHandle output_clip = nullptr;
    OfxParamHandle quality_mode_param = nullptr;
    OfxParamHandle output_mode_param = nullptr;
    OfxParamHandle despill_param = nullptr;
    OfxParamHandle despeckle_param = nullptr;
    OfxParamHandle despeckle_size_param = nullptr;
    OfxParamHandle refiner_param = nullptr;
    OfxParamHandle input_is_linear_param = nullptr;
    OfxParamHandle alpha_black_point_param = nullptr;
    OfxParamHandle alpha_white_point_param = nullptr;
    OfxParamHandle alpha_erode_param = nullptr;
    OfxParamHandle alpha_softness_param = nullptr;
    std::unique_ptr<Engine> engine = nullptr;
    std::filesystem::path models_root = {};
    std::filesystem::path model_path = {};
    DeviceInfo device = {};
    int active_quality_mode = kQualityAuto;
    int active_resolution = 0;
};

extern OfxHost* g_host;
extern OfxSuites g_suites;

bool fetch_suites();
void post_message(const char* message_type, const char* message, OfxImageEffectHandle effect);

InstanceData* get_instance_data(OfxImageEffectHandle instance);
void set_instance_data(OfxImageEffectHandle instance, InstanceData* data);

bool ensure_engine_for_quality(InstanceData* data, int quality_mode, int input_width = 0,
                               int input_height = 0);

OfxStatus on_load();
OfxStatus describe(OfxImageEffectHandle descriptor);
OfxStatus describe_in_context(OfxImageEffectHandle descriptor, const char* context);
OfxStatus create_instance(OfxImageEffectHandle instance);
OfxStatus destroy_instance(OfxImageEffectHandle instance);
OfxStatus render(OfxImageEffectHandle instance, OfxPropertySetHandle in_args,
                 OfxPropertySetHandle out_args);
OfxStatus get_clip_preferences(OfxImageEffectHandle instance, OfxPropertySetHandle out_args);

}  // namespace corridorkey::ofx
