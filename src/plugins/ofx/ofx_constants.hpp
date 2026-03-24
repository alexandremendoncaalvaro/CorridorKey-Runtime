#pragma once

namespace corridorkey::ofx {

// Quality mode choice indices
constexpr int kQualityAuto = 0;
constexpr int kQualityPreview = 1;
constexpr int kQualityStandard = 2;
constexpr int kQualityHigh = 3;
constexpr int kQualityUltra = 4;
constexpr int kQualityMaximum = 5;

inline const char* quality_mode_ui_label(int quality_mode) {
    switch (quality_mode) {
        case kQualityPreview:
            return "Draft (512)";
        case kQualityStandard:
            return "Standard (768)";
        case kQualityHigh:
            return "High (1024)";
        case kQualityUltra:
            return "Ultra (1536)";
        case kQualityMaximum:
            return "Maximum (2048)";
        default:
            return "Auto";
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

inline bool should_apply_srgb_to_output(int output_mode, bool input_is_linear) {
    if (output_mode == kOutputMatteOnly) {
        return false;
    }
    return !output_mode_uses_linear_premultiplied_rgba(output_mode) && !input_is_linear;
}

// Alpha hint source mode
constexpr int kAlphaHintAuto = 0;
constexpr int kAlphaHintExternalOnly = 1;

// Input color space
constexpr int kInputColorSrgb = 0;
constexpr int kInputColorLinear = 1;

// Quantization mode
constexpr int kQuantizationFp16 = 0;
constexpr int kQuantizationInt8 = 1;

// Screen color
constexpr int kScreenColorGreen = 0;
constexpr int kScreenColorBlue = 1;

constexpr int kDefaultSourcePassthroughEnabled = 1;
constexpr int kDefaultEdgeErode = 3;
constexpr int kDefaultEdgeBlur = 7;
constexpr int kMaxEdgeErode = 32;
constexpr int kMaxEdgeBlur = 64;
constexpr int kDefaultInputColorSpace = kInputColorLinear;
constexpr int kDefaultQuantizationMode = kQuantizationFp16;
constexpr int kDefaultScreenColor = kScreenColorGreen;
constexpr double kDefaultTemporalSmoothing = 0.0;

// Spill replacement method
constexpr int kSpillMethodAverage = 0;
constexpr int kSpillMethodDoubleLimit = 1;
constexpr int kSpillMethodNeutral = 2;
constexpr int kDefaultSpillMethod = kSpillMethodAverage;

}  // namespace corridorkey::ofx
