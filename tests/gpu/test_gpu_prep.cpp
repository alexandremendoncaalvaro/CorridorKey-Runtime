#include <array>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <vector>

#include "core/gpu_prep.hpp"
#include "post_process/color_utils.hpp"

using namespace corridorkey;

TEST_CASE("GpuInputPrep Availability", "[e2e][gpu]") {
    core::GpuInputPrep prep;
    bool avail = prep.available();
    SUCCEED("Queried availability: " + std::to_string(avail));
}

TEST_CASE("GpuInputPrep Correctness vs CPU reference", "[e2e][gpu]") {
    core::GpuInputPrep prep;
    if (!prep.available()) {
        SKIP("GPU input prep not available on this host");
    }

    const int src_w = 128;
    const int src_h = 96;
    const int model_w = 64;
    const int model_h = 64;

    std::vector<float> rgb_data(static_cast<size_t>(src_w) * src_h * 3);
    std::vector<float> hint_data(static_cast<size_t>(src_w) * src_h);

    for (int y = 0; y < src_h; ++y) {
        for (int x = 0; x < src_w; ++x) {
            size_t idx = (static_cast<size_t>(y) * src_w + x) * 3;
            rgb_data[idx + 0] = static_cast<float>(x) / src_w;
            rgb_data[idx + 1] = static_cast<float>(y) / src_h;
            rgb_data[idx + 2] = 0.5f;

            hint_data[static_cast<size_t>(y) * src_w + x] =
                static_cast<float>(x + y) / (src_w + src_h);
        }
    }

    Image rgb = {src_w, src_h, 3, {rgb_data.data(), rgb_data.size()}};
    Image hint = {src_w, src_h, 1, {hint_data.data(), hint_data.size()}};

    const size_t channel_stride = static_cast<size_t>(model_w) * model_h;
    const size_t total_planar = 4 * channel_stride;

    const std::array<float, 3> mean = {0.485f, 0.456f, 0.406f};
    const std::array<float, 3> inv_stddev = {1.0f / 0.229f, 1.0f / 0.224f, 1.0f / 0.225f};

    // GPU path
    std::vector<float> gpu_planar(total_planar, 0.0f);
    auto res =
        prep.prepare_inputs(rgb, hint, gpu_planar.data(), model_w, model_h, mean, inv_stddev);
    REQUIRE(res.has_value());

    // CPU reference
    ImageBuffer cpu_rgb_buf(model_w, model_h, 3);
    ImageBuffer cpu_hint_buf(model_w, model_h, 1);
    ColorUtils::State state;

    ColorUtils::resize_area_into(rgb, cpu_rgb_buf.view(), state);
    ColorUtils::resize_area_into(hint, cpu_hint_buf.view(), state);

    std::vector<float> cpu_planar(total_planar, 0.0f);
    ColorUtils::pack_normalized_rgb_and_hint_to_planar(cpu_rgb_buf.view(), cpu_hint_buf.view(),
                                                       cpu_planar.data(), mean, inv_stddev);

    // Compare with tolerance for different resize implementations
    double max_diff = 0.0;
    for (size_t i = 0; i < total_planar; ++i) {
        double diff = std::abs(gpu_planar[i] - cpu_planar[i]);
        if (diff > max_diff) max_diff = diff;
    }

    INFO("Max difference between GPU and CPU planar output: " << max_diff);
    // NPP bilinear vs OpenCV exact area + slight blur produces differences up to 0.15
    REQUIRE(max_diff < 0.15);
}
