#include <catch2/catch_all.hpp>

#include "post_process/despill.hpp"

using namespace corridorkey;

TEST_CASE("despill removes green spill with redistribution", "[unit][despill]") {
    // 1x1 pixel: bright green with R and B
    ImageBuffer rgb_buf(1, 1, 3);
    Image rgb = rgb_buf.view();
    rgb.data[0] = 0.2f;  // R
    rgb.data[1] = 0.9f;  // G (spill)
    rgb.data[2] = 0.2f;  // B

    SECTION("Full strength clamps green and redistributes to R and B") {
        despill(rgb, 1.0f);
        // limit = (0.2 + 0.2) / 2 = 0.2
        // spill = 0.9 - 0.2 = 0.7
        // G_new = 0.9 - 0.7 = 0.2
        // R_new = 0.2 + 0.7 * 0.5 = 0.55
        // B_new = 0.2 + 0.7 * 0.5 = 0.55
        REQUIRE(rgb.data[0] == Catch::Approx(0.55f));
        REQUIRE(rgb.data[1] == Catch::Approx(0.2f));
        REQUIRE(rgb.data[2] == Catch::Approx(0.55f));
    }

    SECTION("Redistribution is clamped to 1.0") {
        rgb.data[0] = 0.9f;
        rgb.data[1] = 1.0f;
        rgb.data[2] = 0.1f;
        despill(rgb, 1.0f);
        // limit = (0.9 + 0.1) / 2 = 0.5
        // spill = 1.0 - 0.5 = 0.5
        // R_new = min(1.0, 0.9 + 0.25) = 1.0
        // B_new = min(1.0, 0.1 + 0.25) = 0.35
        REQUIRE(rgb.data[0] == Catch::Approx(1.0f));
        REQUIRE(rgb.data[1] == Catch::Approx(0.5f));
        REQUIRE(rgb.data[2] == Catch::Approx(0.35f));
    }

    SECTION("No color shift toward purple on dark pixels") {
        // Dark smoke pixel with green contamination
        rgb.data[0] = 0.10f;
        rgb.data[1] = 0.15f;
        rgb.data[2] = 0.08f;
        despill(rgb, 1.0f);
        // limit = (0.10 + 0.08) / 2 = 0.09
        // spill = 0.15 - 0.09 = 0.06
        // G_new = 0.15 - 0.06 = 0.09
        // R_new = 0.10 + 0.03 = 0.13
        // B_new = 0.08 + 0.03 = 0.11
        REQUIRE(rgb.data[0] == Catch::Approx(0.13f));
        REQUIRE(rgb.data[1] == Catch::Approx(0.09f));
        REQUIRE(rgb.data[2] == Catch::Approx(0.11f));
    }

    SECTION("Zero strength despill is no-op") {
        despill(rgb, 0.0f);
        REQUIRE(rgb.data[0] == Catch::Approx(0.2f));
        REQUIRE(rgb.data[1] == Catch::Approx(0.9f));
        REQUIRE(rgb.data[2] == Catch::Approx(0.2f));
    }

    SECTION("Half strength applies partial redistribution") {
        despill(rgb, 0.5f);
        // spill = 0.7, effective_spill = 0.35
        // R_new = 0.2 + 0.175 = 0.375
        // G_new = 0.9 - 0.35 = 0.55
        // B_new = 0.2 + 0.175 = 0.375
        REQUIRE(rgb.data[0] == Catch::Approx(0.375f));
        REQUIRE(rgb.data[1] == Catch::Approx(0.55f));
        REQUIRE(rgb.data[2] == Catch::Approx(0.375f));
    }

    SECTION("No spill when green <= average(R,B)") {
        rgb.data[1] = 0.1f;
        despill(rgb, 1.0f);
        REQUIRE(rgb.data[0] == Catch::Approx(0.2f));
        REQUIRE(rgb.data[1] == Catch::Approx(0.1f));
        REQUIRE(rgb.data[2] == Catch::Approx(0.2f));
    }
}

TEST_CASE("despill handles empty images", "[unit][despill]") {
    ImageBuffer empty_rgb;
    despill(empty_rgb.view(), 1.0f);
}
