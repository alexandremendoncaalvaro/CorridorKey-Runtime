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

    SECTION("Redistribution preserves full energy") {
        rgb.data[0] = 0.9f;
        rgb.data[1] = 1.0f;
        rgb.data[2] = 0.1f;
        despill(rgb, 1.0f);
        // limit = (0.9 + 0.1) / 2 = 0.5
        // spill = 1.0 - 0.5 = 0.5
        // R_new = 0.9 + 0.25 = 1.15 (no clamping, matches Python reference)
        // B_new = 0.1 + 0.25 = 0.35
        REQUIRE(rgb.data[0] == Catch::Approx(1.15f));
        REQUIRE(rgb.data[1] == Catch::Approx(0.5f));
        REQUIRE(rgb.data[2] == Catch::Approx(0.35f));
    }

    SECTION("Values above 1.0 are preserved after redistribution") {
        rgb.data[0] = 0.95f;
        rgb.data[1] = 1.0f;
        rgb.data[2] = 0.05f;
        despill(rgb, 1.0f);
        // limit = (0.95 + 0.05) / 2 = 0.5
        // spill = 1.0 - 0.5 = 0.5
        // R_new = 0.95 + 0.25 = 1.20
        REQUIRE(rgb.data[0] == Catch::Approx(1.20f));
        REQUIRE(rgb.data[0] > 1.0f);
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

TEST_CASE("despill DoubleLimit uses max(R,B) as limit", "[unit][despill]") {
    ImageBuffer rgb_buf(1, 1, 3);
    Image rgb = rgb_buf.view();
    rgb.data[0] = 0.6f;  // R
    rgb.data[1] = 0.9f;  // G (spill)
    rgb.data[2] = 0.2f;  // B

    despill(rgb, 1.0f, SpillMethod::DoubleLimit);
    // limit = max(0.6, 0.2) = 0.6
    // spill = 0.9 - 0.6 = 0.3
    // G_new = 0.9 - 0.3 = 0.6
    // R_new = 0.6 + 0.15 = 0.75
    // B_new = 0.2 + 0.15 = 0.35
    REQUIRE(rgb.data[0] == Catch::Approx(0.75f));
    REQUIRE(rgb.data[1] == Catch::Approx(0.6f));
    REQUIRE(rgb.data[2] == Catch::Approx(0.35f));
}

TEST_CASE("despill DoubleLimit is less aggressive than Average", "[unit][despill]") {
    ImageBuffer avg_buf(1, 1, 3);
    Image avg_rgb = avg_buf.view();
    avg_rgb.data[0] = 0.6f;
    avg_rgb.data[1] = 0.9f;
    avg_rgb.data[2] = 0.2f;

    ImageBuffer dbl_buf(1, 1, 3);
    Image dbl_rgb = dbl_buf.view();
    dbl_rgb.data[0] = 0.6f;
    dbl_rgb.data[1] = 0.9f;
    dbl_rgb.data[2] = 0.2f;

    despill(avg_rgb, 1.0f, SpillMethod::Average);
    despill(dbl_rgb, 1.0f, SpillMethod::DoubleLimit);

    // DoubleLimit removes less green (higher limit)
    REQUIRE(dbl_rgb.data[1] > avg_rgb.data[1]);
}

TEST_CASE("despill Neutral does not shift toward purple", "[unit][despill]") {
    ImageBuffer rgb_buf(1, 1, 3);
    Image rgb = rgb_buf.view();
    rgb.data[0] = 0.2f;
    rgb.data[1] = 0.9f;
    rgb.data[2] = 0.2f;

    despill(rgb, 1.0f, SpillMethod::Neutral);

    // Green should be clamped
    REQUIRE(rgb.data[1] == Catch::Approx(0.2f));

    // R and B should increase but the difference R-B should stay small
    // (unlike Average where both get equal boost causing purple tint on dark pixels)
    float r_b_diff = std::abs(rgb.data[0] - rgb.data[2]);
    REQUIRE(r_b_diff < 0.01f);
}

TEST_CASE("despill Neutral preserves no-spill pixels", "[unit][despill]") {
    ImageBuffer rgb_buf(1, 1, 3);
    Image rgb = rgb_buf.view();
    rgb.data[0] = 0.5f;
    rgb.data[1] = 0.3f;  // Green below limit
    rgb.data[2] = 0.4f;

    despill(rgb, 1.0f, SpillMethod::Neutral);

    REQUIRE(rgb.data[0] == Catch::Approx(0.5f));
    REQUIRE(rgb.data[1] == Catch::Approx(0.3f));
    REQUIRE(rgb.data[2] == Catch::Approx(0.4f));
}

TEST_CASE("despill default method parameter matches Average", "[unit][despill]") {
    ImageBuffer avg_buf(1, 1, 3);
    Image avg_rgb = avg_buf.view();
    avg_buf.view().data[0] = 0.2f;
    avg_buf.view().data[1] = 0.9f;
    avg_buf.view().data[2] = 0.2f;

    ImageBuffer def_buf(1, 1, 3);
    Image def_rgb = def_buf.view();
    def_buf.view().data[0] = 0.2f;
    def_buf.view().data[1] = 0.9f;
    def_buf.view().data[2] = 0.2f;

    despill(avg_rgb, 1.0f, SpillMethod::Average);
    despill(def_rgb, 1.0f);

    REQUIRE(def_rgb.data[0] == Catch::Approx(avg_rgb.data[0]));
    REQUIRE(def_rgb.data[1] == Catch::Approx(avg_rgb.data[1]));
    REQUIRE(def_rgb.data[2] == Catch::Approx(avg_rgb.data[2]));
}

TEST_CASE("despill handles empty images", "[unit][despill]") {
    ImageBuffer empty_rgb;
    despill(empty_rgb.view(), 1.0f);
    despill(empty_rgb.view(), 1.0f, SpillMethod::DoubleLimit);
    despill(empty_rgb.view(), 1.0f, SpillMethod::Neutral);
}
