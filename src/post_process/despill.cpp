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

void despill(Image rgb, float strength, SpillMethod method) {
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

            float limit = 0.0f;
            switch (method) {
                case SpillMethod::DoubleLimit:
                    limit = std::max(r, b);
                    break;
                case SpillMethod::Neutral:
                case SpillMethod::Average:
                    limit = (r + b) * 0.5f;
                    break;
            }

            float spill = std::max(0.0f, g - limit);
            if (spill > 0.0f) {
                float effective_spill = spill * strength;
                float new_g = g - effective_spill;
                rgb(y, x, 1) = new_g;

                if (method == SpillMethod::Neutral) {
                    float gray = (r + new_g + b) / 3.0f;
                    float fill = effective_spill * 0.5f;
                    rgb(y, x, 0) = r + fill * (gray / std::max(r, 1e-6f));
                    rgb(y, x, 2) = b + fill * (gray / std::max(b, 1e-6f));
                } else {
                    rgb(y, x, 0) = r + effective_spill * 0.5f;
                    rgb(y, x, 2) = b + effective_spill * 0.5f;
                }
            }
        }
    });
}

}  // namespace corridorkey
