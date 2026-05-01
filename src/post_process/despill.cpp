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

void despill(Image rgb, float strength, SpillMethod method, int screen_channel) {
    if (rgb.empty() || strength <= 0.0f) return;
    if (screen_channel < 0 || screen_channel > 2) return;

    int h = rgb.height;
    int w = rgb.width;
    std::vector<int> rows(h);
    std::iota(rows.begin(), rows.end(), 0);

    const int other_a = (screen_channel == 0) ? 1 : 0;
    const int other_b = (screen_channel == 2) ? 1 : 2;

    std::for_each(EXEC_POLICY rows.begin(), rows.end(), [&](int y) {
        for (int x = 0; x < w; ++x) {
            float screen = rgb(y, x, screen_channel);
            float a = rgb(y, x, other_a);
            float b = rgb(y, x, other_b);

            float limit = 0.0f;
            switch (method) {
                case SpillMethod::DoubleLimit:
                    limit = std::max(a, b);
                    break;
                case SpillMethod::Neutral:
                case SpillMethod::Average:
                    limit = (a + b) * 0.5f;
                    break;
            }

            float spill = std::max(0.0f, screen - limit);
            if (spill > 0.0f) {
                float effective_spill = spill * strength;
                float new_screen = screen - effective_spill;
                rgb(y, x, screen_channel) = new_screen;

                if (method == SpillMethod::Neutral) {
                    float gray = (a + new_screen + b) / 3.0f;
                    float fill = effective_spill * 0.5f;
                    rgb(y, x, other_a) = a + fill * (gray / std::max(a, 1e-6f));
                    rgb(y, x, other_b) = b + fill * (gray / std::max(b, 1e-6f));
                } else {
                    rgb(y, x, other_a) = a + effective_spill * 0.5f;
                    rgb(y, x, other_b) = b + effective_spill * 0.5f;
                }
            }
        }
    });
}

}  // namespace corridorkey
