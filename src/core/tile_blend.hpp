#pragma once

#include <algorithm>

namespace corridorkey::core {

[[nodiscard]] inline int tile_stride(int tile_size, int overlap) {
    return std::max(1, tile_size - std::max(0, overlap));
}

[[nodiscard]] inline float tile_blend_axis_weight(int coordinate, int tile_size, int overlap,
                                                  bool touches_min_edge, bool touches_max_edge) {
    if (overlap <= 0) {
        return 1.0F;
    }

    if (!touches_min_edge && coordinate < overlap) {
        return static_cast<float>(coordinate) / static_cast<float>(overlap);
    }

    if (!touches_max_edge && coordinate >= tile_size - overlap) {
        return static_cast<float>(tile_size - 1 - coordinate) / static_cast<float>(overlap);
    }

    return 1.0F;
}

[[nodiscard]] inline float edge_aware_tile_weight(int local_x, int local_y, int tile_size,
                                                  int overlap, bool touches_left,
                                                  bool touches_right, bool touches_top,
                                                  bool touches_bottom) {
    const float wx =
        tile_blend_axis_weight(local_x, tile_size, overlap, touches_left, touches_right);
    const float wy =
        tile_blend_axis_weight(local_y, tile_size, overlap, touches_top, touches_bottom);
    return std::min(wx, wy);
}

}  // namespace corridorkey::core
