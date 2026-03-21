#pragma once

#include <corridorkey/api_export.hpp>
#include <corridorkey/types.hpp>

namespace corridorkey {

struct AlphaEdgeState {
    std::vector<float> temp;
    std::vector<float> weights;
};

CORRIDORKEY_API void alpha_levels(Image alpha, float black_point, float white_point);
CORRIDORKEY_API void alpha_erode_dilate(Image alpha, float radius, AlphaEdgeState& state);
CORRIDORKEY_API void alpha_blur(Image alpha, float radius, AlphaEdgeState& state);

}  // namespace corridorkey
