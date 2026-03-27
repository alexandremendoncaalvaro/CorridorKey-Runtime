#pragma once

#include <corridorkey/types.hpp>

namespace corridorkey::core {

[[nodiscard]] inline int requested_quality_resolution(const InferenceParams& params,
                                                      int model_resolution) {
    const int target_resolution =
        params.target_resolution > 0 ? params.target_resolution : model_resolution;
    if (params.requested_quality_resolution > 0) {
        return params.requested_quality_resolution;
    }
    return target_resolution;
}

[[nodiscard]] inline bool should_use_coarse_to_fine_path(const InferenceParams& params,
                                                         int model_resolution) {
    if (params.quality_fallback_mode == QualityFallbackMode::Direct) {
        return false;
    }
    if (params.quality_fallback_mode == QualityFallbackMode::CoarseToFine) {
        return true;
    }
    return requested_quality_resolution(params, model_resolution) > model_resolution;
}

[[nodiscard]] inline InferenceParams coarse_inference_params(const InferenceParams& params,
                                                             int coarse_resolution) {
    InferenceParams coarse_params = params;
    coarse_params.target_resolution = coarse_resolution;
    coarse_params.enable_tiling = false;
    return coarse_params;
}

}  // namespace corridorkey::core
