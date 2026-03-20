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

// Alpha hint source mode
constexpr int kAlphaHintAuto = 0;
constexpr int kAlphaHintExternalOnly = 1;

// Input color space
constexpr int kInputColorAuto = 0;
constexpr int kInputColorSrgb = 1;
constexpr int kInputColorLinear = 2;

// Quantization mode
constexpr int kQuantizationAuto = 0;
constexpr int kQuantizationFp16 = 1;
constexpr int kQuantizationInt8 = 2;

// Screen color
constexpr int kScreenColorGreen = 0;
constexpr int kScreenColorBlue = 1;

constexpr int kDefaultSourcePassthroughEnabled = 0;
constexpr int kDefaultEdgeErode = 3;
constexpr int kDefaultEdgeBlur = 7;
constexpr int kMaxEdgeErode = 32;
constexpr int kMaxEdgeBlur = 64;
constexpr int kDefaultAlphaHintMode = kAlphaHintAuto;
constexpr int kDefaultInputColorSpace = kInputColorSrgb;
constexpr int kDefaultQuantizationMode = kQuantizationAuto;
constexpr int kDefaultScreenColor = kScreenColorGreen;
constexpr double kDefaultTemporalSmoothing = 0.0;

}  // namespace corridorkey::ofx
