#pragma once

#include <corridorkey/types.hpp>
#include <cstddef>
#include <filesystem>
#include <string>

#include "AE_Effect.h"
#include "adobe_bridge.hpp"
#include "adobe_matte_params.hpp"
#include "post_process/screen_color.hpp"

namespace corridorkey::adobe {

inline constexpr std::size_t kEffectParameterSlotCount = 25;
inline constexpr PF_ParamIndex kParamInputLayer = 0;
inline constexpr PF_ParamIndex kParamQuality = 1;
inline constexpr PF_ParamIndex kParamScreenColor = 2;
inline constexpr PF_ParamIndex kParamInputColorSpace = 3;
inline constexpr PF_ParamIndex kParamAlphaHintLayer = 4;
inline constexpr PF_ParamIndex kParamMatteClipBlack = 5;
inline constexpr PF_ParamIndex kParamMatteClipWhite = 6;
inline constexpr PF_ParamIndex kParamMatteShrinkGrow = 7;
inline constexpr PF_ParamIndex kParamMatteEdgeBlur = 8;
inline constexpr PF_ParamIndex kParamDespillStrength = 9;
inline constexpr PF_ParamIndex kParamSpillMethod = 10;
inline constexpr PF_ParamIndex kParamRecoverOriginalDetails = 11;
inline constexpr PF_ParamIndex kParamDetailsEdgeShrink = 12;
inline constexpr PF_ParamIndex kParamDetailsEdgeFeather = 13;
inline constexpr PF_ParamIndex kParamMatteGamma = 14;
inline constexpr PF_ParamIndex kParamAutoDespeckle = 15;
inline constexpr PF_ParamIndex kParamDespeckleSize = 16;
inline constexpr PF_ParamIndex kParamOutputMode = 17;
inline constexpr PF_ParamIndex kParamEnableTiling = 18;
inline constexpr PF_ParamIndex kParamTileOverlap = 19;
inline constexpr PF_ParamIndex kParamUpscaleMethod = 20;
inline constexpr PF_ParamIndex kParamQualityFallbackMode = 21;
inline constexpr PF_ParamIndex kParamCoarseResolutionOverride = 22;
inline constexpr PF_ParamIndex kParamPrepareTimeoutSeconds = 23;
inline constexpr PF_ParamIndex kParamRenderTimeoutSeconds = 24;

struct AdobeEffectRuntimeRequestContext {
    std::filesystem::path models_root;
    std::string host_surface;
    std::string effect_identity;
    std::string client_instance_id;
    int width = 0;
    int height = 0;
    DeviceInfo requested_device = {};
    EngineCreateOptions engine_options = {};
};

struct AdobeEffectRuntimeRequest {
    AdobePrepareSessionOptions prepare_options;
    InferenceParams inference_params;
    AdobeMatteParams matte_params;
    ScreenColorMode screen_color_mode = ScreenColorMode::Green;
    int output_mode = 0;
    int render_timeout_ms = 0;
};

PF_Err setup_effect_parameters(PF_InData* input_data, PF_OutData& output_data) noexcept;

Result<AdobeEffectRuntimeRequest> build_effect_runtime_request(
    PF_ParamDef* const parameters[], const AdobeEffectRuntimeRequestContext& context);

}  // namespace corridorkey::adobe
