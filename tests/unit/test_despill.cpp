#include <catch2/catch_all.hpp>
#include "post_process/despill.hpp"

using namespace corridorkey;

TEST_CASE("despill reduces green spill", "[unit][despill]") {
    // 1x1 pixel: bright green with partial alpha
    ImageBuffer rgb_buf(1, 1, 3);
    Image rgb = rgb_buf.view();
    rgb.data[0] = 0.2f; // R
    rgb.data[1] = 0.9f; // G (spill)
    rgb.data[2] = 0.2f; // B

    ImageBuffer alpha_buf(1, 1, 1);
    Image alpha = alpha_buf.view();
    alpha.data[0] = 0.8f;

    SECTION("Full strength despill") {
        despill(rgb, alpha, 1.0f);
        // Green should be reduced toward (R+B)/2 = 0.2
        REQUIRE(rgb.data[1] < 0.9f);
        REQUIRE(rgb.data[1] >= 0.2f);
        // R and B should be unchanged
        REQUIRE(rgb.data[0] == Catch::Approx(0.2f));
        REQUIRE(rgb.data[2] == Catch::Approx(0.2f));
    }

    SECTION("Zero strength despill is no-op") {
        despill(rgb, alpha, 0.0f);
        REQUIRE(rgb.data[1] == Catch::Approx(0.9f));
    }

    SECTION("No spill when green <= average(R,B)") {
        rgb.data[1] = 0.1f; // Green below average
        float original_g = rgb.data[1];
        despill(rgb, alpha, 1.0f);
        REQUIRE(rgb.data[1] == Catch::Approx(original_g));
    }
}

TEST_CASE("despill handles empty images", "[unit][despill]") {
    ImageBuffer empty_rgb;
    ImageBuffer empty_alpha;
    // Should not crash
    despill(empty_rgb.view(), empty_alpha.view(), 1.0f);
}
