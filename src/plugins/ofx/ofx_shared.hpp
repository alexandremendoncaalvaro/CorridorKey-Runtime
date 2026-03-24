#pragma once

#include <corridorkey/types.hpp>
#include <corridorkey/version.hpp>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

#include "ofxCore.h"
#include "ofxImageEffect.h"
#include "ofxMessage.h"
#include "ofxParam.h"
#include "ofxProperty.h"
#include "ofx_constants.hpp"
#include "post_process/alpha_edge.hpp"

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
constexpr const char* kClipMatteOutput = "Matte Output";
constexpr const char* kClipForegroundOutput = "Foreground Output";
constexpr const char* kClipCompositeOutput = "Composite Output";

constexpr const char* kParamQualityMode = "quality_mode";
constexpr const char* kParamOutputMode = "output_mode";
constexpr const char* kParamInputColorSpace = "input_color_space";
constexpr const char* kParamQuantizationMode = "quantization_mode";
constexpr const char* kParamScreenColor = "screen_color";
constexpr const char* kParamTemporalSmoothing = "temporal_smoothing";
constexpr const char* kParamDespillStrength = "despill_strength";
constexpr const char* kParamSpillMethod = "spill_method";
constexpr const char* kParamAutoDespeckle = "auto_despeckle";
constexpr const char* kParamDespeckleSize = "despeckle_size";
constexpr const char* kParamAlphaBlackPoint = "alpha_black_point";
constexpr const char* kParamAlphaWhitePoint = "alpha_white_point";
constexpr const char* kParamAlphaErode = "alpha_erode";
constexpr const char* kParamAlphaSoftness = "alpha_softness";
constexpr const char* kParamAlphaGamma = "alpha_gamma";
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
constexpr const char* kParamRuntimeStatus = "runtime_status";
constexpr const char* kParamRenderTimeout = "render_timeout";
constexpr const char* kParamPrepareTimeout = "prepare_timeout";
constexpr const char* kParamOpenStartHereGuide = "open_start_here_guide";
constexpr const char* kParamOpenQualityGuide = "open_quality_guide";
constexpr const char* kParamOpenAlphaHintGuide = "open_alpha_hint_guide";
constexpr const char* kParamOpenRecoverDetailsGuide = "open_recover_details_guide";
constexpr const char* kParamOpenTilingGuide = "open_tiling_guide";
constexpr const char* kParamOpenResolveTutorial = "open_resolve_tutorial";
constexpr const char* kParamOpenTroubleshooting = "open_troubleshooting";
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
    OfxParamHandle input_color_space_param = nullptr;
    OfxParamHandle quantization_mode_param = nullptr;
    OfxParamHandle screen_color_param = nullptr;
    OfxParamHandle temporal_smoothing_param = nullptr;
    OfxParamHandle despill_param = nullptr;
    OfxParamHandle spill_method_param = nullptr;
    OfxParamHandle despeckle_param = nullptr;
    OfxParamHandle despeckle_size_param = nullptr;
    OfxParamHandle alpha_black_point_param = nullptr;
    OfxParamHandle alpha_white_point_param = nullptr;
    OfxParamHandle alpha_erode_param = nullptr;
    OfxParamHandle alpha_softness_param = nullptr;
    OfxParamHandle alpha_gamma_param = nullptr;
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
    OfxParamHandle runtime_status_param = nullptr;
    OfxParamHandle render_timeout_param = nullptr;
    OfxParamHandle prepare_timeout_param = nullptr;
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
    // Non-fatal status note shown alongside frame timings. Set when the engine fell back to a
    // lower resolution because the requested one failed to compile (e.g. TensorRT 2048 -> 1536).
    std::string last_warning = {};
    double last_frame_ms = 0.0;
    double avg_frame_ms = 0.0;
    std::uint64_t frame_time_samples = 0;
    bool in_render = false;
    bool runtime_panel_dirty = false;
    bool quantization_error_active = false;

    FrameResult cached_result = {};
    bool cached_result_valid = false;
    double cached_time = 0.0;
    int cached_width = 0;
    int cached_height = 0;
    std::uint64_t cached_signature = 0;
    bool cached_signature_valid = false;
    InferenceParams cached_params = {};
    std::filesystem::path cached_model_path = {};
    int cached_screen_color = kDefaultScreenColor;
    double cached_alpha_black_point = 0.0;
    double cached_alpha_white_point = 1.0;
    double cached_alpha_erode = 0.0;
    double cached_alpha_softness = 0.0;
    double cached_alpha_gamma = 1.0;
    double cached_temporal_smoothing = kDefaultTemporalSmoothing;

    ImageBuffer temporal_alpha = {};
    ImageBuffer temporal_foreground = {};
    bool temporal_state_valid = false;
    double temporal_time = 0.0;
    int temporal_width = 0;
    int temporal_height = 0;

    AlphaEdgeState alpha_edge_state = {};
};

extern OfxHost* g_host;
extern OfxSuites g_suites;

bool fetch_suites();
void post_message(const char* message_type, const char* message, OfxImageEffectHandle effect);

InstanceData* get_instance_data(OfxImageEffectHandle instance);
void set_instance_data(OfxImageEffectHandle instance, InstanceData* data);

struct QualityArtifactSelection;
std::optional<QualityArtifactSelection> select_quality_artifact(
    const std::filesystem::path& models_dir, Backend runtime_backend, int quality_mode,
    int input_width = 0, int input_height = 0, int quantization_mode = kQuantizationFp16);
bool ensure_engine_for_quality(InstanceData* data, int quality_mode, int input_width = 0,
                               int input_height = 0, int quantization_mode = kQuantizationFp16);
void update_runtime_panel(InstanceData* data);
void flush_runtime_panel(InstanceData* data);
OfxStatus instance_changed(OfxImageEffectHandle instance, OfxPropertySetHandle in_args);

OfxStatus on_load();
OfxStatus describe(OfxImageEffectHandle descriptor);
OfxStatus describe_in_context(OfxImageEffectHandle descriptor, const char* context);
OfxStatus create_instance(OfxImageEffectHandle instance);
OfxStatus destroy_instance(OfxImageEffectHandle instance);
OfxStatus render(OfxImageEffectHandle instance, OfxPropertySetHandle in_args,
                 OfxPropertySetHandle out_args);
OfxStatus get_clip_preferences(OfxImageEffectHandle instance, OfxPropertySetHandle out_args);

}  // namespace corridorkey::ofx
