#include "despill.hpp"
#include <cmath>
#include <algorithm>
#include <numeric>
#include <vector>

#if __has_include(<execution>) && (defined(__cpp_lib_execution) || !defined(__clang__))
#include <execution>
#define EXEC_POLICY std::execution::par_unseq ,
#else
#define EXEC_POLICY
#endif

namespace corridorkey {

void despill(Image rgb, const Image alpha, float strength) {
    if (rgb.empty() || alpha.empty()) return;

    int h = rgb.height;
    int w = rgb.width;
    std::vector<int> rows(h);
    std::iota(rows.begin(), rows.end(), 0);

    std::for_each(EXEC_POLICY rows.begin(), rows.end(), [&](int y) {
        for (int x = 0; x < w; ++x) {
            float a = alpha(y, x);
            float target_g = (rgb(y, x, 0) + rgb(y, x, 2)) * 0.5f;
            float diff = std::max(0.0f, rgb(y, x, 1) - target_g);
            rgb(y, x, 1) -= diff * strength * std::min(1.0f, a);
        }
    });
}

} // namespace corridorkey
