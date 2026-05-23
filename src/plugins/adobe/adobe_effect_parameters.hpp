#pragma once

#include <corridorkey/types.hpp>
#include <cstddef>
#include <filesystem>
#include <string>

#include "AE_Effect.h"
#include "adobe_bridge.hpp"

namespace corridorkey::adobe {

inline constexpr std::size_t kEffectParameterSlotCount = 13;
inline constexpr PF_ParamIndex kParamInputLayer = 0;
inline constexpr PF_ParamIndex kParamNodeIdentity = 1;
inline constexpr PF_ParamIndex kParamQuality = 2;
inline constexpr PF_ParamIndex kParamScreenColor = 3;
inline constexpr PF_ParamIndex kParamAlphaHintPolicy = 4;
inline constexpr PF_ParamIndex kParamDespillStrength = 5;
inline constexpr PF_ParamIndex kParamSpillMethod = 6;
inline constexpr PF_ParamIndex kParamRecoverOriginalDetails = 7;
inline constexpr PF_ParamIndex kParamDetailsEdgeShrink = 8;
inline constexpr PF_ParamIndex kParamDetailsEdgeFeather = 9;
inline constexpr PF_ParamIndex kParamOutputMode = 10;
inline constexpr PF_ParamIndex kParamPrepareTimeoutSeconds = 11;
inline constexpr PF_ParamIndex kParamRenderTimeoutSeconds = 12;

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
    int output_mode = 0;
    int render_timeout_ms = 0;
};

PF_Err setup_effect_parameters(PF_InData* input_data, PF_OutData& output_data) noexcept;

Result<AdobeEffectRuntimeRequest> build_effect_runtime_request(
    PF_ParamDef* const parameters[], const AdobeEffectRuntimeRequestContext& context);

}  // namespace corridorkey::adobe
