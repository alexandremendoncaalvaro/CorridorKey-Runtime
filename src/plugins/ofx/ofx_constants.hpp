#pragma once

namespace corridorkey::ofx {

// Quality mode choice indices
constexpr int kQualityAuto = 0;
constexpr int kQualityPreview = 1;
constexpr int kQualityStandard = 2;
constexpr int kQualityHigh = 3;
constexpr int kQualityUltra = 4;
constexpr int kQualityMaximum = 5;

// Upscale method choice indices
constexpr int kUpscaleLanczos4 = 0;
constexpr int kUpscaleBilinear = 1;

// Output mode choice indices
constexpr int kOutputProcessed = 0;
constexpr int kOutputMatteOnly = 1;
constexpr int kOutputForegroundOnly = 2;
constexpr int kOutputSourceMatte = 3;
// FG+Matte: model foreground premultiplied by AI matte, alpha in A channel.
// Never applies sRGB correction -- always outputs linear premultiplied for manual compositing.
constexpr int kOutputFGMatte = 4;

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
