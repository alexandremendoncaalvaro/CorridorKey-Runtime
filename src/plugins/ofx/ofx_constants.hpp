#pragma once

#include <algorithm>
#include <cmath>
#include <corridorkey/types.hpp>
#include <cstdint>
#include <string_view>

#include "ofxColour.h"

namespace corridorkey::ofx {

// Quality mode choice indices
constexpr int kQualityAuto = 0;
constexpr int kQualityPreview = 1;
constexpr int kQualityHigh = 2;
constexpr int kQualityUltra = 3;
constexpr int kQualityMaximum = 4;

inline const char* quality_mode_ui_label(int quality_mode) {
    switch (quality_mode) {
        case kQualityPreview:
            return "Draft (512)";
        case kQualityHigh:
            return "High (1024)";
        case kQualityUltra:
            return "Ultra (1536)";
        case kQualityMaximum:
            return "Maximum (2048)";
        default:
            return "Recommended";
    }
}

constexpr int kQualityFallbackAuto = 0;
constexpr int kQualityFallbackDirect = 1;
constexpr int kQualityFallbackCoarseToFine = 2;

inline const char* quality_fallback_mode_ui_label(int choice) {
    switch (choice) {
        case kQualityFallbackDirect:
            return "Direct";
        case kQualityFallbackCoarseToFine:
            return "Coarse to Fine";
        default:
            return "Recommended";
    }
}

inline QualityFallbackMode quality_fallback_mode_from_choice(int choice) {
    switch (choice) {
        case kQualityFallbackDirect:
            return QualityFallbackMode::Direct;
        case kQualityFallbackCoarseToFine:
            return QualityFallbackMode::CoarseToFine;
        default:
            return QualityFallbackMode::Auto;
    }
}

constexpr int kRefinementAuto = 0;
constexpr int kRefinementFullFrame = 1;
constexpr int kRefinementTiled = 2;

inline const char* refinement_mode_ui_label(int choice) {
    switch (choice) {
        case kRefinementFullFrame:
            return "Full Frame";
        case kRefinementTiled:
            return "Tiled";
        default:
            return "Packaged";
    }
}

inline RefinementMode refinement_mode_from_choice(int choice) {
    switch (choice) {
        case kRefinementFullFrame:
            return RefinementMode::FullFrame;
        case kRefinementTiled:
            return RefinementMode::Tiled;
        default:
            return RefinementMode::Auto;
    }
}

constexpr int kCoarseResolutionAutomatic = 0;
constexpr int kCoarseResolution512 = 1;
constexpr int kCoarseResolution1024 = 2;
constexpr int kCoarseResolution1536 = 3;
constexpr int kCoarseResolution2048 = 4;

// Quality-ladder pixel sizes returned by the choice-index helpers below.
// Promoted out of inline switch arms so cppcoreguidelines-avoid-magic-
// numbers stays clean and the per-tier resolution is named at the source.
constexpr int kCoarseResolutionPx512 = 512;
constexpr int kCoarseResolutionPx1024 = 1024;
constexpr int kCoarseResolutionPx1536 = 1536;
constexpr int kCoarseResolutionPx2048 = 2048;

inline int coarse_resolution_override_from_choice(int choice) {
    switch (choice) {
        case kCoarseResolution512:
            return kCoarseResolutionPx512;
        case kCoarseResolution1024:
            return kCoarseResolutionPx1024;
        case kCoarseResolution1536:
            return kCoarseResolutionPx1536;
        case kCoarseResolution2048:
            return kCoarseResolutionPx2048;
        default:
            return 0;
    }
}

// Upscale method choice indices
constexpr int kUpscaleLanczos4 = 0;
constexpr int kUpscaleBilinear = 1;

// Output mode choice indices
constexpr int kOutputProcessed = 0;
constexpr int kOutputMatteOnly = 1;
constexpr int kOutputForegroundOnly = 2;
constexpr int kOutputSourceMatte = 3;
// Processed: post-processed model foreground premultiplied by the AI matte in linear space.
// Matches the runtime's core `FrameResult::processed` semantics.
// Never applies sRGB correction -- safe for compositing.
//
// FG+Matte: model foreground premultiplied by AI matte, alpha in A channel.
// Never applies sRGB correction -- always outputs linear premultiplied for manual compositing.
constexpr int kOutputFGMatte = 4;

inline bool output_mode_uses_linear_premultiplied_rgba(int output_mode) {
    return output_mode == kOutputProcessed || output_mode == kOutputFGMatte;
}

inline bool output_mode_requires_model_foreground(int output_mode) {
    return output_mode != kOutputMatteOnly && output_mode != kOutputSourceMatte;
}

inline bool should_apply_srgb_to_output(int output_mode, bool host_managed, bool input_is_linear) {
    if (output_mode == kOutputMatteOnly) {
        return false;
    }
    if (host_managed) {
        return false;
    }
    return !output_mode_uses_linear_premultiplied_rgba(output_mode) && !input_is_linear;
}

inline const char* output_colourspace_for_output_mode(int output_mode) {
    return output_mode == kOutputMatteOnly ? kOfxColourspaceRaw : kOfxColourspaceLinRec709Srgb;
}

// Alpha hint source mode
constexpr int kAlphaHintAuto = 0;
constexpr int kAlphaHintExternalOnly = 1;

// Input color space
constexpr int kInputColorSrgb = 0;
constexpr int kInputColorLinear = 1;
constexpr int kInputColorAutoHostManaged = 2;

enum class InputColorRuntimeMode : std::uint8_t {
    ManualSrgb,
    ManualLinear,
    HostManagedSrgbTx,
    HostManagedLinearRec709Srgb,
    AutoFallbackLinear,
};

inline InputColorRuntimeMode resolve_input_color_runtime_mode(int input_color_space,
                                                              std::string_view source_colourspace) {
    if (input_color_space == kInputColorSrgb) {
        return InputColorRuntimeMode::ManualSrgb;
    }
    if (input_color_space == kInputColorLinear) {
        return InputColorRuntimeMode::ManualLinear;
    }
    if (source_colourspace == kOfxColourspaceSrgbTx) {
        return InputColorRuntimeMode::HostManagedSrgbTx;
    }
    if (source_colourspace == kOfxColourspaceLinRec709Srgb) {
        return InputColorRuntimeMode::HostManagedLinearRec709Srgb;
    }
    return InputColorRuntimeMode::AutoFallbackLinear;
}

inline bool input_color_runtime_mode_is_host_managed(InputColorRuntimeMode mode) {
    return mode == InputColorRuntimeMode::HostManagedSrgbTx ||
           mode == InputColorRuntimeMode::HostManagedLinearRec709Srgb;
}

inline bool input_color_runtime_mode_used_manual_fallback(InputColorRuntimeMode mode) {
    return mode == InputColorRuntimeMode::AutoFallbackLinear;
}

inline bool input_color_runtime_mode_is_linear(InputColorRuntimeMode mode) {
    return mode == InputColorRuntimeMode::ManualLinear ||
           mode == InputColorRuntimeMode::HostManagedLinearRec709Srgb ||
           mode == InputColorRuntimeMode::AutoFallbackLinear;
}

// Screen color
constexpr int kScreenColorGreen = 0;
constexpr int kScreenColorBlue = 1;

constexpr int kDefaultSourcePassthroughEnabled = 1;
constexpr int kDefaultEdgeErode = 3;
constexpr int kDefaultEdgeBlur = 7;
constexpr int kMaxEdgeErode = 100;
constexpr int kMaxEdgeBlur = 100;
constexpr int kDefaultInputColorSpace = kInputColorAutoHostManaged;
constexpr double kResolutionScaleBaselineLongEdge = 1920.0;
constexpr int kDefaultScreenColor = kScreenColorGreen;
constexpr double kDefaultTemporalSmoothing = 0.0;

// Spill replacement method
constexpr int kSpillMethodAverage = 0;
constexpr int kSpillMethodDoubleLimit = 1;
constexpr int kSpillMethodNeutral = 2;
constexpr int kDefaultSpillMethod = kSpillMethodAverage;

inline double source_long_edge_scale_factor(int width, int height) {
    const int long_edge = std::max(width, height);
    if (long_edge <= 0) {
        return 1.0;
    }
    return static_cast<double>(long_edge) / kResolutionScaleBaselineLongEdge;
}

inline double scale_pixels_to_source_long_edge(double pixels_at_baseline, int width, int height) {
    return pixels_at_baseline * source_long_edge_scale_factor(width, height);
}

inline int scale_integer_pixels_to_source_long_edge(int pixels_at_baseline, int width, int height) {
    return std::max(0, static_cast<int>(std::lround(
                           scale_pixels_to_source_long_edge(pixels_at_baseline, width, height))));
}

}  // namespace corridorkey::ofx
