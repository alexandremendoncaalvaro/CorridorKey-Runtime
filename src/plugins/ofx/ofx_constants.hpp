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

constexpr int kDefaultSourcePassthroughEnabled = 0;
constexpr int kDefaultEdgeErode = 3;
constexpr int kDefaultEdgeBlur = 7;

}  // namespace corridorkey::ofx
