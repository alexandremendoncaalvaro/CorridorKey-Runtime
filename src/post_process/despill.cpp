#include "despill.hpp"

#include <algorithm>
#include <numeric>
#include <vector>

#if __has_include(<execution>) && (defined(__cpp_lib_execution) || !defined(__clang__))
#include <execution>
#define EXEC_POLICY std::execution::par_unseq,
#else
#define EXEC_POLICY
#endif

namespace corridorkey {

void despill(Image rgb, float strength) {
    if (rgb.empty() || strength <= 0.0f) return;

    int h = rgb.height;
    int w = rgb.width;
    std::vector<int> rows(h);
    std::iota(rows.begin(), rows.end(), 0);

    std::for_each(EXEC_POLICY rows.begin(), rows.end(), [&](int y) {
        for (int x = 0; x < w; ++x) {
            float r = rgb(y, x, 0);
            float g = rgb(y, x, 1);
            float b = rgb(y, x, 2);

            float limit = (r + b) * 0.5f;
            float spill = std::max(0.0f, g - limit);

            if (spill > 0.0f) {
                float effective_spill = spill * strength;
                rgb(y, x, 0) = r + effective_spill * 0.5f;
                rgb(y, x, 1) = g - effective_spill;
                rgb(y, x, 2) = b + effective_spill * 0.5f;
            }
        }
    });
}

}  // namespace corridorkey
