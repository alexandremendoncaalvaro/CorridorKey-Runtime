#include <catch2/catch_all.hpp>

#include "post_process/alpha_edge.hpp"

using namespace corridorkey;

TEST_CASE("alpha_levels remaps black and white points", "[unit][alpha]") {
    ImageBuffer buf(4, 1, 1);
    Image alpha = buf.view();
    alpha(0, 0) = 0.0f;
    alpha(0, 1) = 0.25f;
    alpha(0, 2) = 0.5f;
    alpha(0, 3) = 1.0f;

    // Remap 0.25 -> 0.0 and 0.5 -> 1.0
    alpha_levels(alpha, 0.25f, 0.5f);

    REQUIRE(alpha(0, 0) == Catch::Approx(0.0f));
    REQUIRE(alpha(0, 1) == Catch::Approx(0.0f));
    REQUIRE(alpha(0, 2) == Catch::Approx(1.0f));
    REQUIRE(alpha(0, 3) == Catch::Approx(1.0f));
}

TEST_CASE("alpha_erode_dilate expands and shrinks alpha", "[unit][alpha]") {
    ImageBuffer buf(5, 5, 1);
    Image alpha = buf.view();

    // Create a 1x1 white pixel in the center
    for (int y = 0; y < 5; ++y) {
        for (int x = 0; x < 5; ++x) {
            alpha(y, x) = (x == 2 && y == 2) ? 1.0f : 0.0f;
        }
    }

    AlphaEdgeState state;

    SECTION("Dilate expands the 1x1 pixel to 3x3") {
        alpha_erode_dilate(alpha, 1.0f, state);
        REQUIRE(alpha(1, 1) == Catch::Approx(1.0f));
        REQUIRE(alpha(3, 3) == Catch::Approx(1.0f));
        // Outside the 3x3 should still be 0
        REQUIRE(alpha(0, 0) == Catch::Approx(0.0f));
        REQUIRE(alpha(4, 4) == Catch::Approx(0.0f));
    }

    SECTION("Erode removes the 1x1 pixel completely") {
        alpha_erode_dilate(alpha, -1.0f, state);
        REQUIRE(alpha(2, 2) == Catch::Approx(0.0f));
    }
}

TEST_CASE("alpha_blur smooths edges", "[unit][alpha]") {
    ImageBuffer buf(8, 1, 1);
    Image alpha = buf.view();

    for (int x = 0; x < 8; ++x) {
        alpha(0, x) = (x < 4) ? 0.0f : 1.0f;
    }

    AlphaEdgeState state;
    alpha_blur(alpha, 1.5f, state);

    // After blur, the edge at x=3 and x=4 should be softened
    REQUIRE(alpha(0, 3) > 0.0f);
    REQUIRE(alpha(0, 3) < 0.5f);
    REQUIRE(alpha(0, 4) > 0.5f);
    REQUIRE(alpha(0, 4) < 1.0f);

    // Far pixels should remain mostly unchanged
    REQUIRE(alpha(0, 0) < 0.01f);
    REQUIRE(alpha(0, 7) > 0.99f);
}

TEST_CASE("alpha_gamma_correct applies non-linear curve via LUT", "[unit][alpha]") {
    ImageBuffer buf(4, 1, 1);
    Image alpha = buf.view();
    alpha(0, 0) = 0.0f;
    alpha(0, 1) = 0.25f;
    alpha(0, 2) = 0.5f;
    alpha(0, 3) = 1.0f;

    SECTION("Gamma 1.0 is identity") {
        alpha_gamma_correct(alpha, 1.0f);
        REQUIRE(alpha(0, 0) == Catch::Approx(0.0f));
        REQUIRE(alpha(0, 1) == Catch::Approx(0.25f).margin(0.002f));
        REQUIRE(alpha(0, 2) == Catch::Approx(0.5f).margin(0.002f));
        REQUIRE(alpha(0, 3) == Catch::Approx(1.0f));
    }

    SECTION("Gamma > 1 brightens midtones (recovers semi-transparent areas)") {
        alpha_gamma_correct(alpha, 2.0f);
        REQUIRE(alpha(0, 0) == Catch::Approx(0.0f));
        REQUIRE(alpha(0, 1) > 0.25f);
        REQUIRE(alpha(0, 2) > 0.5f);
        REQUIRE(alpha(0, 3) == Catch::Approx(1.0f));
    }

    SECTION("Gamma < 1 darkens midtones (tightens matte)") {
        alpha_gamma_correct(alpha, 0.5f);
        REQUIRE(alpha(0, 0) == Catch::Approx(0.0f));
        REQUIRE(alpha(0, 1) < 0.25f);
        REQUIRE(alpha(0, 2) < 0.5f);
        REQUIRE(alpha(0, 3) == Catch::Approx(1.0f));
    }

    SECTION("Fully opaque and transparent pixels are unchanged") {
        alpha_gamma_correct(alpha, 3.0f);
        REQUIRE(alpha(0, 0) == Catch::Approx(0.0f));
        REQUIRE(alpha(0, 3) == Catch::Approx(1.0f));
    }
}

TEST_CASE("alpha_gamma_correct handles edge cases", "[unit][alpha]") {
    ImageBuffer buf(1, 1, 1);
    Image alpha = buf.view();
    alpha(0, 0) = 0.5f;

    SECTION("Zero gamma is treated as no-op") {
        alpha_gamma_correct(alpha, 0.0f);
        REQUIRE(alpha(0, 0) == Catch::Approx(0.5f));
    }

    SECTION("Negative gamma is treated as no-op") {
        alpha_gamma_correct(alpha, -1.0f);
        REQUIRE(alpha(0, 0) == Catch::Approx(0.5f));
    }
}

TEST_CASE("alpha operations handle empty images gracefully", "[unit][alpha]") {
    ImageBuffer buf;
    Image alpha = buf.view();
    AlphaEdgeState state;

    // Should not crash
    alpha_levels(alpha, 0.0f, 1.0f);
    alpha_gamma_correct(alpha, 2.0f);
    alpha_erode_dilate(alpha, 1.0f, state);
    alpha_blur(alpha, 1.0f, state);
}
