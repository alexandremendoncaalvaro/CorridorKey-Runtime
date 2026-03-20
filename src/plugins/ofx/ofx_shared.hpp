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
#include "ofx_constants.hpp"

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

class OfxRuntimeClient;

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
constexpr const char* kParamBrightness = "brightness";
constexpr const char* kParamSaturation = "saturation";
constexpr const char* kParamUpscaleMethod = "upscale_method";
constexpr const char* kParamEnableTiling = "enable_tiling";
constexpr const char* kParamTileOverlap = "tile_overlap";
constexpr const char* kParamSourcePassthrough = "source_passthrough";
constexpr const char* kParamEdgeErode = "edge_erode";
constexpr const char* kParamEdgeBlur = "edge_blur";
constexpr const char* kParamRuntimeProcessing = "runtime_processing";
constexpr const char* kParamRuntimeDevice = "runtime_device";
constexpr const char* kParamRuntimeRequestedQuality = "runtime_requested_quality";
constexpr const char* kParamRuntimeEffectiveQuality = "runtime_effective_quality";
constexpr const char* kParamRuntimeArtifact = "runtime_artifact";
constexpr const char* kRuntimeStatusStringMode = kOfxParamStringIsSingleLine;
constexpr int kRuntimeStatusEnabled = 0;

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
    OfxParamHandle brightness_param = nullptr;
    OfxParamHandle saturation_param = nullptr;
    OfxParamHandle upscale_method_param = nullptr;
    OfxParamHandle enable_tiling_param = nullptr;
    OfxParamHandle tile_overlap_param = nullptr;
    OfxParamHandle source_passthrough_param = nullptr;
    OfxParamHandle edge_erode_param = nullptr;
    OfxParamHandle edge_blur_param = nullptr;
    OfxParamHandle runtime_processing_param = nullptr;
    OfxParamHandle runtime_device_param = nullptr;
    OfxParamHandle runtime_requested_quality_param = nullptr;
    OfxParamHandle runtime_effective_quality_param = nullptr;
    OfxParamHandle runtime_artifact_param = nullptr;
    std::unique_ptr<OfxRuntimeClient> runtime_client = nullptr;
    std::unique_ptr<Engine> engine = nullptr;
    std::filesystem::path models_root = {};
    std::filesystem::path model_path = {};
    std::filesystem::path runtime_server_path = {};
    DeviceInfo device = {};
    DeviceInfo preferred_device = {};
    int active_quality_mode = kQualityAuto;
    int requested_resolution = 0;
    int active_resolution = 0;
    bool cpu_quality_guardrail_active = false;
    bool use_runtime_server = false;
    std::uint64_t render_count = 0;
    std::string last_error = {};
};

extern OfxHost* g_host;
extern OfxSuites g_suites;

bool fetch_suites();
void post_message(const char* message_type, const char* message, OfxImageEffectHandle effect);

InstanceData* get_instance_data(OfxImageEffectHandle instance);
void set_instance_data(OfxImageEffectHandle instance, InstanceData* data);

bool ensure_engine_for_quality(InstanceData* data, int quality_mode, int input_width = 0,
                               int input_height = 0);
void update_runtime_panel(InstanceData* data);

OfxStatus on_load();
OfxStatus describe(OfxImageEffectHandle descriptor);
OfxStatus describe_in_context(OfxImageEffectHandle descriptor, const char* context);
OfxStatus create_instance(OfxImageEffectHandle instance);
OfxStatus destroy_instance(OfxImageEffectHandle instance);
OfxStatus render(OfxImageEffectHandle instance, OfxPropertySetHandle in_args,
                 OfxPropertySetHandle out_args);
OfxStatus get_clip_preferences(OfxImageEffectHandle instance, OfxPropertySetHandle out_args);

}  // namespace corridorkey::ofx
