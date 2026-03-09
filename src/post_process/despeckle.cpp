#include "despeckle.hpp"
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

void despeckle(Image alpha, int size_threshold) {
    if (alpha.empty() || size_threshold <= 0) return;

    int w = alpha.width;
    int h = alpha.height;
    ImageBuffer temp_buffer(w, h, 1);
    Image temp = temp_buffer.view();

    std::vector<int> rows(h);
    std::iota(rows.begin(), rows.end(), 0);

    // Erosion (parallelized by rows)
    std::for_each(EXEC_POLICY rows.begin(), rows.end(), [&](int y) {
        for (int x = 0; x < w; ++x) {
            float min_v = 1.0f;
            for (int dy : {-1, 0, 1}) {
                int ny = std::clamp(y + dy, 0, h - 1);
                for (int dx : {-1, 0, 1}) {
                    min_v = std::min(min_v, alpha(ny, std::clamp(x + dx, 0, w - 1)));
                }
            }
            temp(y, x) = min_v;
        }
    });

    // Dilation (parallelized by rows)
    std::for_each(EXEC_POLICY rows.begin(), rows.end(), [&](int y) {
        for (int x = 0; x < w; ++x) {
            float max_v = 0.0f;
            for (int dy : {-1, 0, 1}) {
                int ny = std::clamp(y + dy, 0, h - 1);
                for (int dx : {-1, 0, 1}) {
                    max_v = std::max(max_v, temp(ny, std::clamp(x + dx, 0, w - 1)));
                }
            }
            alpha(y, x) = max_v;
        }
    });
}

} // namespace corridorkey
