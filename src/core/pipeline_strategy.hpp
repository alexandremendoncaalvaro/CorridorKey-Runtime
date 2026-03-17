#pragma once

#include <algorithm>

namespace corridorkey::core {

/**
 * @brief Selects the preprocessing/postprocessing strategy based on the ratio between
 * input resolution and model resolution.
 *
 * Padded: fit_pad (preserves aspect ratio) + crop ROI after inference + source detail
 *         restoration. Required when the model is much smaller than input (ratio > 1.5).
 *
 * Direct: resize_area (squash to model size) + direct upscale after inference.
 *         Sufficient when the model resolution is close to input (ratio <= 1.5).
 */
enum class PipelineStrategy { Direct, Padded };

constexpr float kPaddedPipelineThreshold = 1.5F;

inline PipelineStrategy select_pipeline(int input_width, int input_height, int model_resolution) {
    float max_input = static_cast<float>(std::max(input_width, input_height));
    float ratio = max_input / static_cast<float>(model_resolution);
    return ratio > kPaddedPipelineThreshold ? PipelineStrategy::Padded : PipelineStrategy::Direct;
}

}  // namespace corridorkey::core
