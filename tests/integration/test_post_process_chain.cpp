#include <catch2/catch_all.hpp>

#include "common/srgb_lut.hpp"
#include "post_process/color_utils.hpp"
#include "post_process/despeckle.hpp"
#include "post_process/despill.hpp"

using namespace corridorkey;

TEST_CASE("Full post-process chain matching original Python pipeline",
          "[integration][postprocess]") {
    // Simulate a 4x4 green-screen frame with some green spill
    // Model outputs: FG in sRGB, alpha as 0-1 mask
    ImageBuffer rgb_buf(4, 4, 3);
    Image rgb = rgb_buf.view();

    ImageBuffer alpha_buf(4, 4, 1);
    Image alpha = alpha_buf.view();

    // Fill with sRGB values: greenish foreground with partial alpha
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            rgb(y, x, 0) = 0.6f;  // sRGB red
            rgb(y, x, 1) = 0.8f;  // sRGB green (some spill)
            rgb(y, x, 2) = 0.5f;  // sRGB blue
            alpha(y, x) = (y < 2) ? 1.0f : 0.7f;
        }
    }

    // Pipeline order matches original Python:
    // 1. Despeckle alpha (space-agnostic)
    // 2. Despill FG in sRGB
    // 3. Convert FG to linear, premultiply, pack RGBA
    // 4. Composite in linear, convert to sRGB

    // 1. Despeckle (use small threshold so 4x4 region is not removed)
    DespeckleState despeckle_state;
    despeckle(alpha, 2, despeckle_state, 0, 0);

    // 2. Despill in sRGB space (no alpha parameter, matching original)
    float green_before = rgb.data[1];
    despill(rgb, 1.0f);
    // Green should be reduced, R and B should increase (spill redistribution)
    REQUIRE(rgb.data[1] < green_before);
    REQUIRE(rgb.data[0] > 0.6f);  // R increased by spill/2
    REQUIRE(rgb.data[2] > 0.5f);  // B increased by spill/2

    // 3. Convert to linear and premultiply (one pass, like inference_session.cpp)
    const auto& lut = SrgbLut::instance();
    ImageBuffer processed_buf(4, 4, 4);
    Image proc = processed_buf.view();

    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            float a = alpha(y, x);
            proc(y, x, 0) = lut.to_linear(rgb(y, x, 0)) * a;
            proc(y, x, 1) = lut.to_linear(rgb(y, x, 1)) * a;
            proc(y, x, 2) = lut.to_linear(rgb(y, x, 2)) * a;
            proc(y, x, 3) = a;
        }
    }

    // Verify premultiply: bottom half (alpha=0.5) should have halved linear values
    for (int x = 0; x < 4; ++x) {
        REQUIRE(proc(3, x, 3) == Catch::Approx(0.7f));
        // Premultiplied linear values should be roughly half of full alpha version
        REQUIRE(proc(3, x, 0) < proc(0, x, 0));
    }

    // 4. Composite the premultiplied linear result directly to display sRGB
    ImageBuffer legacy_comp_buf(4, 4, 4);
    Image legacy_comp = legacy_comp_buf.view();
    std::copy(proc.data.begin(), proc.data.end(), legacy_comp.data.begin());
    ColorUtils::composite_over_checker(legacy_comp);
    ColorUtils::linear_to_srgb(legacy_comp);

    ImageBuffer comp_buf(4, 4, 4);
    Image comp = comp_buf.view();
    ColorUtils::composite_premultiplied_over_checker_to_srgb(proc, comp);

    for (size_t index = 0; index < comp.data.size(); ++index) {
        REQUIRE(comp.data[index] == Catch::Approx(legacy_comp.data[index]).margin(0.0001f));
    }

    // All values should be in valid sRGB range [0, 1]
    for (size_t i = 0; i < comp.data.size(); ++i) {
        REQUIRE(comp.data[i] >= 0.0f);
        REQUIRE(comp.data[i] <= 1.0f);
    }
}
