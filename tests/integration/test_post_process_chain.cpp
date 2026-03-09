#include <catch2/catch_all.hpp>
#include "post_process/color_utils.hpp"
#include "post_process/despill.hpp"
#include "post_process/despeckle.hpp"

using namespace corridorkey;

TEST_CASE("Full post-process chain: srgb -> despill -> despeckle -> premultiply -> srgb", "[integration][postprocess]") {
    // Simulate a 4x4 green-screen frame with some green spill
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
            alpha(y, x) = (y < 2) ? 1.0f : 0.5f;  // Top half opaque, bottom half semi
        }
    }

    // 1. Convert sRGB to linear
    ColorUtils::srgb_to_linear(rgb);
    REQUIRE(rgb.data[0] < 0.6f); // Linear values should be darker

    // 2. Despill — green spill should be reduced
    float green_before = rgb.data[1]; // Save one pixel's green
    despill(rgb, alpha, 1.0f);
    REQUIRE(rgb.data[1] <= green_before); // Green should not increase

    // 3. Despeckle (should be essentially no-op on uniform alpha)
    despeckle(alpha, 400);

    // 4. Premultiply
    ColorUtils::premultiply(rgb, alpha);
    // Bottom half (alpha=0.5) RGB values should be halved
    for (int x = 0; x < 4; ++x) {
        float premult_r = rgb(3, x, 0);
        REQUIRE(premult_r < 0.3f); // Was ~0.33 linear * 0.5 alpha
    }

    // 5. Convert back to sRGB
    ColorUtils::linear_to_srgb(rgb);
    // Values should be back in perceptual sRGB range
    for (size_t i = 0; i < rgb.data.size(); ++i) {
        REQUIRE(rgb.data[i] >= 0.0f);
        REQUIRE(rgb.data[i] <= 1.0f);
    }
}
