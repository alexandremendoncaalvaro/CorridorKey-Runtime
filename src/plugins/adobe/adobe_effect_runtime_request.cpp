#include <algorithm>
#include <cmath>
#include <string>

#include "adobe_effect_parameters.hpp"
#include "app/runtime_contracts.hpp"

namespace {

constexpr int kQualityDraftResolution = 512;
constexpr int kQualityHighResolution = 1024;
constexpr int kQualityUltraResolution = 1536;
constexpr int kQualityMaximumResolution = 2048;
constexpr int kPopupZeroBasedOffset = 1;
constexpr int kMillisecondsPerSecond = 1000;
constexpr int kDefaultQualityMode = 1;
constexpr int kDefaultScreenColorGreen = 0;
constexpr int kScreenColorBlue = 1;
constexpr int kDefaultAlphaHintPolicy = 0;
constexpr int kAlphaHintPolicyRequireExternal = 1;
constexpr int kDefaultNodeIdentity = 0;
constexpr int kNodeIdentityBlue = 1;
constexpr double kDefaultDespillStrength = 0.5;
constexpr double kMinimumDespillStrength = 0.0;
constexpr double kMaximumDespillStrength = 1.0;
constexpr int kDefaultSpillMethod = 0;
constexpr bool kDefaultRecoverOriginalDetails = true;
constexpr int kDefaultDetailsEdgeShrink = 3;
constexpr int kDefaultDetailsEdgeFeather = 7;
constexpr int kDefaultOutputMode = 0;
constexpr int kOutputMatteOnly = 1;
constexpr int kOutputSourceMatte = 3;
constexpr int kMaximumOutputMode = 4;
constexpr int kDefaultPrepareTimeoutSeconds = 30;
constexpr int kDefaultRenderTimeoutSeconds = 120;
constexpr int kMinimumTimeoutSeconds = 10;
constexpr int kMaximumPrepareTimeoutSeconds = 600;
constexpr int kMaximumRenderTimeoutSeconds = 300;
constexpr int kMinimumEdgePixels = 0;
constexpr int kMaximumEdgePixels = 100;

const PF_ParamDef* parameter_at(PF_ParamDef* const parameters[], PF_ParamIndex index) noexcept {
    if (parameters == nullptr || index < 0 ||
        static_cast<std::size_t>(index) >= corridorkey::adobe::kEffectParameterSlotCount) {
        return nullptr;
    }
    return parameters[static_cast<std::size_t>(index)];
}

int popup_choice(PF_ParamDef* const parameters[], PF_ParamIndex index, int fallback,
                 int maximum_choice) noexcept {
    const PF_ParamDef* parameter = parameter_at(parameters, index);
    if (parameter == nullptr || parameter->param_type != PF_Param_POPUP) {
        return fallback;
    }
    const int zero_based = static_cast<int>(parameter->u.pd.value) - kPopupZeroBasedOffset;
    return std::clamp(zero_based, 0, maximum_choice);
}

corridorkey::Result<int> strict_popup_choice(PF_ParamDef* const parameters[], PF_ParamIndex index,
                                             int fallback, int maximum_choice, const char* label) {
    const PF_ParamDef* parameter = parameter_at(parameters, index);
    if (parameter == nullptr || parameter->param_type != PF_Param_POPUP) {
        return fallback;
    }
    const int zero_based = static_cast<int>(parameter->u.pd.value) - kPopupZeroBasedOffset;
    if (zero_based < 0 || zero_based > maximum_choice) {
        return corridorkey::Unexpected<corridorkey::Error>(
            corridorkey::Error{corridorkey::ErrorCode::InvalidParameters,
                               "Adobe " + std::string(label) + " is out of range."});
    }
    return zero_based;
}

double slider_value(PF_ParamDef* const parameters[], PF_ParamIndex index,
                    double fallback) noexcept {
    const PF_ParamDef* parameter = parameter_at(parameters, index);
    if (parameter == nullptr || parameter->param_type != PF_Param_FLOAT_SLIDER) {
        return fallback;
    }
    return static_cast<double>(parameter->u.fs_d.value);
}

double bounded_slider_value(PF_ParamDef* const parameters[], PF_ParamIndex index, double fallback,
                            double minimum_value, double maximum_value) noexcept {
    const double value = slider_value(parameters, index, fallback);
    if (!std::isfinite(value)) {
        return fallback;
    }
    return std::clamp(value, minimum_value, maximum_value);
}

bool checkbox_value(PF_ParamDef* const parameters[], PF_ParamIndex index, bool fallback) noexcept {
    const PF_ParamDef* parameter = parameter_at(parameters, index);
    if (parameter == nullptr || parameter->param_type != PF_Param_CHECKBOX) {
        return fallback;
    }
    return parameter->u.bd.value != 0;
}

int integer_slider_value(PF_ParamDef* const parameters[], PF_ParamIndex index, int fallback,
                         int minimum_value, int maximum_value) noexcept {
    const auto value = static_cast<int>(std::lround(
        bounded_slider_value(parameters, index, fallback, static_cast<double>(minimum_value),
                             static_cast<double>(maximum_value))));
    return std::clamp(value, minimum_value, maximum_value);
}

int timeout_milliseconds(PF_ParamDef* const parameters[], PF_ParamIndex index, int fallback_seconds,
                         int maximum_seconds) noexcept {
    const int seconds = integer_slider_value(parameters, index, fallback_seconds,
                                             kMinimumTimeoutSeconds, maximum_seconds);
    return seconds * kMillisecondsPerSecond;
}

int requested_resolution_for_quality(int quality_mode) noexcept {
    switch (quality_mode) {
        case 2:
            return kQualityHighResolution;
        case 3:
            return kQualityUltraResolution;
        case 4:
            return kQualityMaximumResolution;
        default:
            return kQualityDraftResolution;
    }
}

int despill_screen_channel_for(int screen_color) noexcept {
    return screen_color == kScreenColorBlue ? 2 : 1;
}

corridorkey::AlphaHintPolicy alpha_hint_policy_for(int alpha_hint_policy) noexcept {
    if (alpha_hint_policy == kAlphaHintPolicyRequireExternal) {
        return corridorkey::AlphaHintPolicy::RequireExternalHint;
    }
    return corridorkey::AlphaHintPolicy::AutoRoughFallback;
}

std::string node_identity_for(int node_identity) {
    return node_identity == kNodeIdentityBlue ? "blue" : "green";
}

bool output_mode_requires_foreground(int output_mode) noexcept {
    return output_mode != kOutputMatteOnly && output_mode != kOutputSourceMatte;
}

corridorkey::Result<void> validate_runtime_request_context(
    const corridorkey::adobe::AdobeEffectRuntimeRequestContext& context) {
    if (context.models_root.empty()) {
        return corridorkey::Unexpected<corridorkey::Error>(corridorkey::Error{
            corridorkey::ErrorCode::InvalidParameters, "Adobe models root is required."});
    }
    if (context.host_surface.empty()) {
        return corridorkey::Unexpected<corridorkey::Error>(corridorkey::Error{
            corridorkey::ErrorCode::InvalidParameters, "Adobe host surface is required."});
    }
    if (context.effect_identity.empty()) {
        return corridorkey::Unexpected<corridorkey::Error>(corridorkey::Error{
            corridorkey::ErrorCode::InvalidParameters, "Adobe effect identity is required."});
    }
    if (context.width <= 0 || context.height <= 0) {
        return corridorkey::Unexpected<corridorkey::Error>(corridorkey::Error{
            corridorkey::ErrorCode::InvalidParameters, "Adobe frame dimensions are required."});
    }
    return {};
}

corridorkey::InferenceParams build_inference_params(PF_ParamDef* const parameters[],
                                                    int requested_resolution, int output_mode) {
    const int screen_color = popup_choice(parameters, corridorkey::adobe::kParamScreenColor,
                                          kDefaultScreenColorGreen, 2);
    const int alpha_hint_policy = popup_choice(
        parameters, corridorkey::adobe::kParamAlphaHintPolicy, kDefaultAlphaHintPolicy, 1);

    corridorkey::InferenceParams inference_params;
    inference_params.target_resolution = requested_resolution;
    inference_params.requested_quality_resolution = requested_resolution;
    inference_params.quality_fallback_mode = corridorkey::QualityFallbackMode::Direct;
    inference_params.refinement_mode = corridorkey::RefinementMode::Auto;
    inference_params.despill_strength = static_cast<float>(bounded_slider_value(
        parameters, corridorkey::adobe::kParamDespillStrength, kDefaultDespillStrength,
        kMinimumDespillStrength, kMaximumDespillStrength));
    inference_params.spill_method =
        popup_choice(parameters, corridorkey::adobe::kParamSpillMethod, kDefaultSpillMethod, 2);
    inference_params.despill_screen_channel = despill_screen_channel_for(screen_color);
    inference_params.alpha_hint_policy = alpha_hint_policy_for(alpha_hint_policy);
    inference_params.upscale_method = corridorkey::UpscaleMethod::Lanczos4;
    inference_params.enable_tiling = false;
    inference_params.tile_padding = corridorkey::InferenceParams::kDefaultTilePaddingPx;
    inference_params.source_passthrough =
        checkbox_value(parameters, corridorkey::adobe::kParamRecoverOriginalDetails,
                       kDefaultRecoverOriginalDetails);
    inference_params.sp_erode_px =
        integer_slider_value(parameters, corridorkey::adobe::kParamDetailsEdgeShrink,
                             kDefaultDetailsEdgeShrink, kMinimumEdgePixels, kMaximumEdgePixels);
    inference_params.sp_blur_px =
        integer_slider_value(parameters, corridorkey::adobe::kParamDetailsEdgeFeather,
                             kDefaultDetailsEdgeFeather, kMinimumEdgePixels, kMaximumEdgePixels);
    inference_params.output_alpha_only = !output_mode_requires_foreground(output_mode);
    return inference_params;
}

}  // namespace

namespace corridorkey::adobe {

Result<AdobeEffectRuntimeRequest> build_effect_runtime_request(
    PF_ParamDef* const parameters[], const AdobeEffectRuntimeRequestContext& context) {
    auto validation = validate_runtime_request_context(context);
    if (!validation) {
        return Unexpected<Error>(validation.error());
    }

    auto node_identity = strict_popup_choice(parameters, kParamNodeIdentity, kDefaultNodeIdentity,
                                             1, "node identity");
    if (!node_identity) {
        return Unexpected<Error>(node_identity.error());
    }
    const int quality_mode = popup_choice(parameters, kParamQuality, kDefaultQualityMode, 4);
    const int requested_resolution = requested_resolution_for_quality(quality_mode);
    const std::string node_identity_label = node_identity_for(*node_identity);
    auto artifact_selection = app::host_plugin_runtime_artifact_selection_for_request(
        context.models_root, context.requested_device, requested_resolution, false,
        QualityFallbackMode::Direct, node_identity_label);
    if (!artifact_selection) {
        return Unexpected<Error>(artifact_selection.error());
    }

    auto output_mode = strict_popup_choice(parameters, kParamOutputMode, kDefaultOutputMode,
                                           kMaximumOutputMode, "output mode");
    if (!output_mode) {
        return Unexpected<Error>(output_mode.error());
    }

    AdobeEffectRuntimeRequest request;
    request.prepare_options.host_surface = context.host_surface;
    request.prepare_options.effect_identity = context.effect_identity;
    request.prepare_options.node_identity = node_identity_label;
    request.prepare_options.client_instance_id = context.client_instance_id;
    request.prepare_options.model_path = artifact_selection->model_path;
    request.prepare_options.requested_device = artifact_selection->requested_device;
    request.prepare_options.engine_options = context.engine_options;
    request.prepare_options.requested_quality_mode = quality_mode;
    request.prepare_options.requested_resolution = requested_resolution;
    request.prepare_options.effective_resolution = requested_resolution;
    request.prepare_options.prepare_timeout_ms =
        timeout_milliseconds(parameters, kParamPrepareTimeoutSeconds, kDefaultPrepareTimeoutSeconds,
                             kMaximumPrepareTimeoutSeconds);
    request.inference_params =
        build_inference_params(parameters, requested_resolution, *output_mode);
    request.output_mode = *output_mode;
    request.render_timeout_ms =
        timeout_milliseconds(parameters, kParamRenderTimeoutSeconds, kDefaultRenderTimeoutSeconds,
                             kMaximumRenderTimeoutSeconds);
    return request;
}

}  // namespace corridorkey::adobe
