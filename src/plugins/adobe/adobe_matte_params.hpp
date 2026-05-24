#pragma once

namespace corridorkey::adobe {

struct AdobeMatteParams {
    double black_point = 0.0;
    double white_point = 1.0;
    double shrink_grow_pixels = 0.0;
    double edge_blur_pixels = 0.0;
    double gamma = 1.0;
};

}  // namespace corridorkey::adobe
