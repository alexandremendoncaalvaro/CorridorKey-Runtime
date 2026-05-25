#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string>
#include <string_view>

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
constexpr int kDefaultInputColorSpace = 0;
constexpr int kInputColorLinear = 1;
constexpr int kScreenColorGreen = 0;
constexpr int kScreenColorBlueGreen = 1;
constexpr double kDefaultDespillStrength = 0.5;
constexpr double kMinimumDespillStrength = 0.0;
constexpr double kMaximumDespillStrength = 1.0;
constexpr int kDefaultSpillMethod = 0;
constexpr bool kDefaultRecoverOriginalDetails = true;
constexpr int kDefaultDetailsEdgeShrink = 3;
constexpr int kDefaultDetailsEdgeFeather = 7;
constexpr double kDefaultMatteClipBlack = 0.0;
constexpr double kDefaultMatteClipWhite = 1.0;
constexpr double kDefaultMatteShrinkGrow = 0.0;
constexpr double kDefaultMatteEdgeBlur = 0.0;
constexpr double kDefaultMatteGamma = 1.0;
constexpr int kDefaultOutputMode = 0;
constexpr int kOutputMatteOnly = 1;
constexpr int kOutputSourceMatte = 3;
constexpr int kMaximumOutputMode = 4;
constexpr int kDefaultDespeckleSize = 400;
constexpr int kMinimumDespeckleSize = 50;
constexpr int kMaximumDespeckleSize = 2000;
constexpr int kDefaultTileOverlap = 64;
constexpr int kMinimumTileOverlap = 8;
constexpr int kMaximumTileOverlap = 128;
constexpr int kDefaultUpscaleMethod = 1;
constexpr int kUpscaleMethodBilinear = 1;
constexpr int kDefaultQualityFallbackMode = 0;
constexpr int kQualityFallbackCoarseToFine = 2;
constexpr int kDefaultCoarseResolutionOverride = 0;
constexpr int kCoarseResolution512 = 1;
constexpr int kCoarseResolution1024 = 2;
constexpr int kCoarseResolution1536 = 3;
constexpr int kCoarseResolution2048 = 4;
constexpr int kDefaultPrepareTimeoutSeconds = 30;
constexpr int kDefaultRenderTimeoutSeconds = 120;
constexpr int kMinimumTimeoutSeconds = 10;
constexpr int kMaximumPrepareTimeoutSeconds = 600;
constexpr int kMaximumRenderTimeoutSeconds = 300;
constexpr int kMinimumEdgePixels = 0;
constexpr int kMaximumEdgePixels = 100;
constexpr double kMinimumMatteClip = 0.0;
constexpr double kMaximumMatteClip = 1.0;
constexpr double kMinimumMatteShrinkGrow = -10.0;
constexpr double kMaximumMatteShrinkGrow = 10.0;
constexpr double kMinimumMatteEdgeBlur = 0.0;
constexpr double kMaximumMatteEdgeBlur = 5.0;
constexpr double kMinimumMatteGamma = 0.1;
constexpr double kMaximumMatteGamma = 10.0;
constexpr std::string_view kAdobeBlueEffectMatchName = "com.corridorkey.effect.blue";

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

corridorkey::QualityFallbackMode quality_fallback_mode_for_choice(int choice) noexcept {
    return choice == kQualityFallbackCoarseToFine ? corridorkey::QualityFallbackMode::CoarseToFine
                                                  : corridorkey::QualityFallbackMode::Direct;
}

corridorkey::UpscaleMethod upscale_method_for_choice(int choice) noexcept {
    return choice == kUpscaleMethodBilinear ? corridorkey::UpscaleMethod::Bilinear
                                            : corridorkey::UpscaleMethod::Lanczos4;
}

int coarse_resolution_for_choice(int choice) noexcept {
    switch (choice) {
        case kCoarseResolution512:
            return kQualityDraftResolution;
        case kCoarseResolution1024:
            return kQualityHighResolution;
        case kCoarseResolution1536:
            return kQualityUltraResolution;
        case kCoarseResolution2048:
            return kQualityMaximumResolution;
        default:
            return 0;
    }
}

int scale_integer_pixels_to_source_long_edge(int pixels_at_baseline, int width,
                                             int height) noexcept {
    constexpr double kBaselineLongEdge = 1920.0;
    const int long_edge = std::max(width, height);
    const double scale = long_edge > 0 ? static_cast<double>(long_edge) / kBaselineLongEdge : 1.0;
    return std::max(0, static_cast<int>(std::lround(pixels_at_baseline * scale)));
}

int recommended_coarse_resolution_for(int requested_resolution) noexcept {
    if (requested_resolution > kQualityHighResolution) {
        return kQualityHighResolution;
    }
    if (requested_resolution > kQualityDraftResolution) {
        return kQualityDraftResolution;
    }
    return 0;
}

int effective_coarse_resolution_override(corridorkey::QualityFallbackMode fallback_mode,
                                         int selected_resolution,
                                         int requested_resolution) noexcept {
    if (fallback_mode != corridorkey::QualityFallbackMode::CoarseToFine) {
        return 0;
    }
    if (selected_resolution > 0) {
        return selected_resolution;
    }
    return recommended_coarse_resolution_for(requested_resolution);
}

bool is_blue_effect_identity(std::string_view effect_identity) noexcept {
    return effect_identity == kAdobeBlueEffectMatchName;
}

std::string node_identity_for_effect_identity(std::string_view effect_identity) {
    return is_blue_effect_identity(effect_identity) ? "blue" : "green";
}

int despill_screen_channel_for_effect_identity(std::string_view effect_identity) noexcept {
    return is_blue_effect_identity(effect_identity) ? 2 : 1;
}

int screen_color_maximum_choice_for_effect_identity(std::string_view effect_identity) noexcept {
    return is_blue_effect_identity(effect_identity) ? 0 : kScreenColorBlueGreen;
}

std::string screen_color_domain_for(std::string_view effect_identity) {
    return is_blue_effect_identity(effect_identity) ? "blue" : "green";
}

corridorkey::ScreenColorMode screen_color_mode_for(std::string_view effect_identity,
                                                   int screen_color) noexcept {
    if (is_blue_effect_identity(effect_identity)) {
        return corridorkey::ScreenColorMode::Blue;
    }
    if (screen_color == kScreenColorBlueGreen) {
        return corridorkey::ScreenColorMode::BlueGreen;
    }
    return corridorkey::ScreenColorMode::Green;
}

int effective_resolution_for_artifact(const std::filesystem::path& model_path,
                                      int requested_resolution) {
    const auto packaged_resolution = corridorkey::app::packaged_model_resolution(model_path);
    if (packaged_resolution.has_value() && *packaged_resolution > 0) {
        return *packaged_resolution;
    }
    return requested_resolution;
}

bool output_mode_requires_foreground(int output_mode) noexcept {
    return output_mode != kOutputMatteOnly && output_mode != kOutputSourceMatte;
}

corridorkey::adobe::AdobeMatteParams build_matte_params(PF_ParamDef* const parameters[]) noexcept {
    return corridorkey::adobe::AdobeMatteParams{
        .black_point =
            bounded_slider_value(parameters, corridorkey::adobe::kParamMatteClipBlack,
                                 kDefaultMatteClipBlack, kMinimumMatteClip, kMaximumMatteClip),
        .white_point =
            bounded_slider_value(parameters, corridorkey::adobe::kParamMatteClipWhite,
                                 kDefaultMatteClipWhite, kMinimumMatteClip, kMaximumMatteClip),
        .shrink_grow_pixels = bounded_slider_value(
            parameters, corridorkey::adobe::kParamMatteShrinkGrow, kDefaultMatteShrinkGrow,
            kMinimumMatteShrinkGrow, kMaximumMatteShrinkGrow),
        .edge_blur_pixels = bounded_slider_value(
            parameters, corridorkey::adobe::kParamMatteEdgeBlur, kDefaultMatteEdgeBlur,
            kMinimumMatteEdgeBlur, kMaximumMatteEdgeBlur),
        .gamma = bounded_slider_value(parameters, corridorkey::adobe::kParamMatteGamma,
                                      kDefaultMatteGamma, kMinimumMatteGamma, kMaximumMatteGamma),
    };
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

corridorkey::InferenceParams build_inference_params(
    PF_ParamDef* const parameters[], int requested_resolution, int output_mode,
    std::string_view effect_identity, corridorkey::QualityFallbackMode fallback_mode,
    corridorkey::ScreenColorMode screen_color_mode, int coarse_resolution_override,
    int effective_resolution, int source_width, int source_height) {
    corridorkey::InferenceParams inference_params;
    inference_params.target_resolution = effective_resolution;
    inference_params.requested_quality_resolution = requested_resolution;
    inference_params.quality_fallback_mode = fallback_mode;
    inference_params.refinement_mode = corridorkey::RefinementMode::Auto;
    inference_params.coarse_resolution_override = coarse_resolution_override;
    inference_params.despill_strength = static_cast<float>(bounded_slider_value(
        parameters, corridorkey::adobe::kParamDespillStrength, kDefaultDespillStrength,
        kMinimumDespillStrength, kMaximumDespillStrength));
    inference_params.spill_method =
        popup_choice(parameters, corridorkey::adobe::kParamSpillMethod, kDefaultSpillMethod, 2);
    inference_params.despill_screen_channel =
        despill_screen_channel_for_effect_identity(effect_identity);
    inference_params.alpha_hint_policy = corridorkey::AlphaHintPolicy::AutoRoughFallback;
    const int input_color_space =
        popup_choice(parameters, corridorkey::adobe::kParamInputColorSpace, kDefaultInputColorSpace,
                     kInputColorLinear);
    inference_params.input_is_linear = input_color_space == kInputColorLinear;
    const int upscale_method =
        popup_choice(parameters, corridorkey::adobe::kParamUpscaleMethod, kDefaultUpscaleMethod, 1);
    inference_params.upscale_method = upscale_method_for_choice(upscale_method);
    inference_params.enable_tiling =
        checkbox_value(parameters, corridorkey::adobe::kParamEnableTiling, false);
    inference_params.tile_padding =
        integer_slider_value(parameters, corridorkey::adobe::kParamTileOverlap, kDefaultTileOverlap,
                             kMinimumTileOverlap, kMaximumTileOverlap);
    inference_params.auto_despeckle =
        checkbox_value(parameters, corridorkey::adobe::kParamAutoDespeckle, false);
    inference_params.despeckle_size =
        integer_slider_value(parameters, corridorkey::adobe::kParamDespeckleSize,
                             kDefaultDespeckleSize, kMinimumDespeckleSize, kMaximumDespeckleSize);
    const bool source_passthrough_requested =
        checkbox_value(parameters, corridorkey::adobe::kParamRecoverOriginalDetails,
                       kDefaultRecoverOriginalDetails);
    inference_params.source_passthrough =
        source_passthrough_requested &&
        corridorkey::screen_color_allows_source_passthrough(screen_color_mode);
    inference_params.sp_erode_px = scale_integer_pixels_to_source_long_edge(
        integer_slider_value(parameters, corridorkey::adobe::kParamDetailsEdgeShrink,
                             kDefaultDetailsEdgeShrink, kMinimumEdgePixels, kMaximumEdgePixels),
        source_width, source_height);
    inference_params.sp_blur_px = scale_integer_pixels_to_source_long_edge(
        integer_slider_value(parameters, corridorkey::adobe::kParamDetailsEdgeFeather,
                             kDefaultDetailsEdgeFeather, kMinimumEdgePixels, kMaximumEdgePixels),
        source_width, source_height);
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

    const int quality_mode = popup_choice(parameters, kParamQuality, kDefaultQualityMode, 4);
    const int requested_resolution = requested_resolution_for_quality(quality_mode);
    const std::string node_identity_label =
        node_identity_for_effect_identity(context.effect_identity);
    const int screen_color =
        popup_choice(parameters, kParamScreenColor, kScreenColorGreen,
                     screen_color_maximum_choice_for_effect_identity(context.effect_identity));
    const std::string screen_color_domain = screen_color_domain_for(context.effect_identity);
    const ScreenColorMode screen_color_mode =
        screen_color_mode_for(context.effect_identity, screen_color);
    const int fallback_choice =
        popup_choice(parameters, kParamQualityFallbackMode, kDefaultQualityFallbackMode, 2);
    const QualityFallbackMode fallback_mode = quality_fallback_mode_for_choice(fallback_choice);
    const int selected_coarse_resolution = coarse_resolution_for_choice(popup_choice(
        parameters, kParamCoarseResolutionOverride, kDefaultCoarseResolutionOverride, 4));
    const int coarse_resolution_override = effective_coarse_resolution_override(
        fallback_mode, selected_coarse_resolution, requested_resolution);
    auto artifact_selection = app::host_plugin_runtime_artifact_selection_for_request(
        context.models_root, context.requested_device, requested_resolution, false, fallback_mode,
        screen_color_domain, coarse_resolution_override);
    if (!artifact_selection) {
        return Unexpected<Error>(artifact_selection.error());
    }
    const int effective_resolution =
        effective_resolution_for_artifact(artifact_selection->model_path, requested_resolution);

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
    request.prepare_options.effective_resolution = effective_resolution;
    request.prepare_options.prepare_timeout_ms =
        timeout_milliseconds(parameters, kParamPrepareTimeoutSeconds, kDefaultPrepareTimeoutSeconds,
                             kMaximumPrepareTimeoutSeconds);
    request.inference_params = build_inference_params(
        parameters, requested_resolution, *output_mode, context.effect_identity, fallback_mode,
        screen_color_mode, coarse_resolution_override, effective_resolution, context.width,
        context.height);
    request.matte_params = build_matte_params(parameters);
    request.screen_color_mode = screen_color_mode;
    request.output_mode = *output_mode;
    request.render_timeout_ms =
        timeout_milliseconds(parameters, kParamRenderTimeoutSeconds, kDefaultRenderTimeoutSeconds,
                             kMaximumRenderTimeoutSeconds);
    return request;
}

}  // namespace corridorkey::adobe
