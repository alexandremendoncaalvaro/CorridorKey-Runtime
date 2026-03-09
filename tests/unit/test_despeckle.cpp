#include <catch2/catch_all.hpp>
#include "post_process/despeckle.hpp"

using namespace corridorkey;

TEST_CASE("despeckle removes small specks via erosion+dilation", "[unit][despeckle]") {
    // 5x5 alpha: all white except a single speck at center
    ImageBuffer alpha_buf(5, 5, 1);
    Image alpha = alpha_buf.view();

    // Fill with 1.0 (opaque)
    for (size_t i = 0; i < alpha.data.size(); ++i) {
        alpha.data[i] = 1.0f;
    }

    SECTION("Single white pixel speck gets removed by erosion") {
        // Start with all-black, add a single white speck
        for (size_t i = 0; i < alpha.data.size(); ++i) {
            alpha.data[i] = 0.0f;
        }
        alpha(2, 2) = 1.0f; // Single white speck in black background
        despeckle(alpha, 400);
        // Erosion removes the isolated white pixel, dilation cannot restore it
        REQUIRE(alpha(2, 2) == Catch::Approx(0.0f));
    }

    SECTION("All-zero image stays zero") {
        for (size_t i = 0; i < alpha.data.size(); ++i) {
            alpha.data[i] = 0.0f;
        }
        despeckle(alpha, 400);
        for (size_t i = 0; i < alpha.data.size(); ++i) {
            REQUIRE(alpha.data[i] == Catch::Approx(0.0f));
        }
    }

    SECTION("All-one image stays one") {
        despeckle(alpha, 400);
        for (size_t i = 0; i < alpha.data.size(); ++i) {
            REQUIRE(alpha.data[i] == Catch::Approx(1.0f));
        }
    }
}

TEST_CASE("despeckle handles edge cases", "[unit][despeckle]") {
    SECTION("Zero size_threshold is no-op") {
        ImageBuffer alpha_buf(3, 3, 1);
        Image alpha = alpha_buf.view();
        alpha.data[4] = 0.5f;
        float original = alpha.data[4];
        despeckle(alpha, 0);
        REQUIRE(alpha.data[4] == Catch::Approx(original));
    }

    SECTION("Empty image does not crash") {
        ImageBuffer empty_buf;
        despeckle(empty_buf.view(), 400);
    }
}
