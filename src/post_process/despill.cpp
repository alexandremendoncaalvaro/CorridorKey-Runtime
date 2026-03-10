#include "despill.hpp"

#include <algorithm>
#include <cmath>
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
                float r_new = r + spill * 0.5f;
                float g_new = g - spill;
                float b_new = b + spill * 0.5f;

                if (strength < 1.0f) {
                    rgb(y, x, 0) = r + (r_new - r) * strength;
                    rgb(y, x, 1) = g + (g_new - g) * strength;
                    rgb(y, x, 2) = b + (b_new - b) * strength;
                } else {
                    rgb(y, x, 0) = r_new;
                    rgb(y, x, 1) = g_new;
                    rgb(y, x, 2) = b_new;
                }
            }
        }
    });
}

}  // namespace corridorkey
