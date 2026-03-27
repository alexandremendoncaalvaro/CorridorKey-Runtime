#pragma once

#include <algorithm>

#include <corridorkey/types.hpp>

namespace corridorkey::core {

struct LocalRefinementTileRegion {
    int x_start = 0;
    int y_start = 0;
    int width = 0;
    int height = 0;
};

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

[[nodiscard]] inline bool should_tile_local_refinement(const InferenceParams& params,
                                                       int model_resolution, int image_width,
                                                       int image_height) {
    if (params.refinement_mode == RefinementMode::FullFrame) {
        return false;
    }
    if (params.refinement_mode == RefinementMode::Tiled) {
        return true;
    }

    const int long_edge = std::max(image_width, image_height);
    return long_edge > std::max(model_resolution * 2, 2560);
}

[[nodiscard]] inline int local_refinement_tile_size(int model_resolution) {
    return std::clamp(model_resolution, 256, 1024);
}

[[nodiscard]] inline LocalRefinementTileRegion local_refinement_tile_region(
    int image_width, int image_height, int tile_size, int stride, int tile_x, int tile_y) {
    if (image_width <= 0 || image_height <= 0 || tile_size <= 0 || stride <= 0) {
        return {};
    }

    int x_start = tile_x * stride;
    int y_start = tile_y * stride;
    if (x_start + tile_size > image_width) {
        x_start = std::max(0, image_width - tile_size);
    }
    if (y_start + tile_size > image_height) {
        y_start = std::max(0, image_height - tile_size);
    }

    return {
        x_start,
        y_start,
        std::max(0, std::min(tile_size, image_width - x_start)),
        std::max(0, std::min(tile_size, image_height - y_start)),
    };
}

}  // namespace corridorkey::core
