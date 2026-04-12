#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <vector>

#include "core/gpu_resize.hpp"
#include "post_process/color_utils.hpp"

using namespace corridorkey;

TEST_CASE("GpuResizer Availability", "[unit][core]") {
    core::GpuResizer resizer;
    bool avail = resizer.available();
    SUCCEED("Queried availability: " + std::to_string(avail));
}

TEST_CASE("GpuResizer Correctness vs CPU reference", "[unit][core]") {
    core::GpuResizer resizer;
    if (!resizer.available()) {
        SKIP("GPU resize not available on this host");
    }

    const int src_w = 64;
    const int src_h = 64;
    const int dst_w = 128;
    const int dst_h = 128;

    std::vector<float> alpha_src(src_w * src_h, 0.5f);
    std::vector<float> fg_src(src_w * src_h * 3, 0.25f);

    // Add some gradient so we can verify resize
    for (int y = 0; y < src_h; ++y) {
        for (int x = 0; x < src_w; ++x) {
            float val = static_cast<float>(x) / src_w;
            alpha_src[y * src_w + x] = val;
            fg_src[y * src_w + x] = val; // R
            fg_src[src_w * src_h + y * src_w + x] = 1.0f - val; // G
            fg_src[2 * src_w * src_h + y * src_w + x] = 0.5f; // B
        }
    }

    ImageBuffer gpu_alpha(dst_w, dst_h, 1);
    ImageBuffer gpu_fg(dst_w, dst_h, 3);
    ImageBuffer cpu_alpha(dst_w, dst_h, 1);
    ImageBuffer cpu_fg(dst_w, dst_h, 3);

    auto res = resizer.resize_planar_outputs(
        alpha_src.data(), fg_src.data(),
        src_w, src_h,
        gpu_alpha.view(), gpu_fg.view()
    );

    REQUIRE(res.has_value());

    // Compute CPU reference
    ColorUtils::State state;
    ColorUtils::resize_alpha_fg_from_planar_into(
        alpha_src.data(), fg_src.data(),
        src_w, src_h, cpu_alpha.view(), cpu_fg.view()
    );

    // Compare
    double max_diff = 0.0;
    for (size_t i = 0; i < gpu_alpha.view().data.size(); ++i) {
        double diff = std::abs(gpu_alpha.view().data[i] - cpu_alpha.view().data[i]);
        if (diff > max_diff) max_diff = diff;
    }
    REQUIRE(max_diff < 0.01);

    max_diff = 0.0;
    for (size_t i = 0; i < gpu_fg.view().data.size(); ++i) {
        double diff = std::abs(gpu_fg.view().data[i] - cpu_fg.view().data[i]);
        if (diff > max_diff) max_diff = diff;
    }
    REQUIRE(max_diff < 0.01);
}
