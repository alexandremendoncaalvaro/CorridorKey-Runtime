#include <catch2/catch_all.hpp>
#include "post_process/despill.hpp"

using namespace corridorkey;

TEST_CASE("despill reduces green spill with R/B redistribution", "[unit][despill]") {
    // 1x1 pixel: bright green with R and B
    ImageBuffer rgb_buf(1, 1, 3);
    Image rgb = rgb_buf.view();
    rgb.data[0] = 0.2f; // R
    rgb.data[1] = 0.9f; // G (spill)
    rgb.data[2] = 0.2f; // B

    SECTION("Full strength despill redistributes spill to R and B") {
        despill(rgb, 1.0f);
        // spill = max(0, 0.9 - (0.2+0.2)/2) = max(0, 0.9 - 0.2) = 0.7
        // G_new = 0.9 - 0.7 = 0.2
        // R_new = 0.2 + 0.7 * 0.5 = 0.55
        // B_new = 0.2 + 0.7 * 0.5 = 0.55
        REQUIRE(rgb.data[0] == Catch::Approx(0.55f));
        REQUIRE(rgb.data[1] == Catch::Approx(0.2f));
        REQUIRE(rgb.data[2] == Catch::Approx(0.55f));
    }

    SECTION("Zero strength despill is no-op") {
        despill(rgb, 0.0f);
        REQUIRE(rgb.data[0] == Catch::Approx(0.2f));
        REQUIRE(rgb.data[1] == Catch::Approx(0.9f));
        REQUIRE(rgb.data[2] == Catch::Approx(0.2f));
    }

    SECTION("Half strength blends original and despilled") {
        despill(rgb, 0.5f);
        // despilled: R=0.55, G=0.2, B=0.55
        // blended: original * 0.5 + despilled * 0.5
        REQUIRE(rgb.data[0] == Catch::Approx(0.2f + (0.55f - 0.2f) * 0.5f)); // 0.375
        REQUIRE(rgb.data[1] == Catch::Approx(0.9f + (0.2f - 0.9f) * 0.5f));  // 0.55
        REQUIRE(rgb.data[2] == Catch::Approx(0.2f + (0.55f - 0.2f) * 0.5f)); // 0.375
    }

    SECTION("No spill when green <= average(R,B)") {
        rgb.data[1] = 0.1f; // Green below average
        despill(rgb, 1.0f);
        // No change since spill = max(0, 0.1 - 0.2) = 0
        REQUIRE(rgb.data[0] == Catch::Approx(0.2f));
        REQUIRE(rgb.data[1] == Catch::Approx(0.1f));
        REQUIRE(rgb.data[2] == Catch::Approx(0.2f));
    }
}

TEST_CASE("despill handles empty images", "[unit][despill]") {
    ImageBuffer empty_rgb;
    despill(empty_rgb.view(), 1.0f);
}
