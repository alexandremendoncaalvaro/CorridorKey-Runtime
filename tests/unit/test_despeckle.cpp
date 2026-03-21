#include <catch2/catch_all.hpp>

#include "post_process/despeckle.hpp"

using namespace corridorkey;

TEST_CASE("despeckle removes small components via connected-component analysis",
          "[unit][despeckle]") {
    // 10x10 alpha: large connected region + isolated speck
    ImageBuffer alpha_buf(10, 10, 1);
    Image alpha = alpha_buf.view();
    DespeckleState state;

    SECTION("Basic despeckle") {
        ImageBuffer alpha_buf_basic(5, 5, 1);
        Image alpha_basic = alpha_buf_basic.view();
        std::fill(alpha_basic.data.begin(), alpha_basic.data.end(), 0.0f);

        // Add a speckle of size 4
        alpha_basic(1, 1) = 1.0f;
        alpha_basic(1, 2) = 1.0f;
        alpha_basic(2, 1) = 1.0f;
        alpha_basic(2, 2) = 1.0f;

        // Add a valid region of size 6
        alpha_basic(3, 3) = 1.0f;
        alpha_basic(3, 4) = 1.0f;
        alpha_basic(4, 3) = 1.0f;
        alpha_basic(4, 4) = 1.0f;
        alpha_basic(2, 4) = 1.0f;
        alpha_basic(4, 2) = 1.0f;

        despeckle(alpha_basic, 5, state, 1, 0);

        // Speckle (area 4) should be removed
        REQUIRE(alpha_basic(1, 1) == Catch::Approx(0.0f));
        REQUIRE(alpha_basic(1, 2) == Catch::Approx(0.0f));
        REQUIRE(alpha_basic(2, 1) == Catch::Approx(0.0f));
        REQUIRE(alpha_basic(2, 2) == Catch::Approx(0.0f));

        // Valid region (area 6) should be kept
        REQUIRE(alpha_basic(3, 3) > 0.0f);
        REQUIRE(alpha_basic(3, 4) > 0.0f);
        REQUIRE(alpha_basic(4, 3) > 0.0f);
        REQUIRE(alpha_basic(4, 4) > 0.0f);
        REQUIRE(alpha_basic(2, 4) > 0.0f);
        REQUIRE(alpha_basic(4, 2) > 0.0f);
    }

    SECTION("Small speck removed, large region kept") {
        // Fill with zeros
        for (size_t i = 0; i < alpha.data.size(); ++i) {
            alpha.data[i] = 0.0f;
        }
        // Create a large region (>= area_threshold) in top-left
        for (int y = 0; y < 5; ++y) {
            for (int x = 0; x < 5; ++x) {
                alpha(y, x) = 1.0f;
            }
        }
        // Create a small speck (< area_threshold) at bottom-right
        alpha(8, 8) = 1.0f;

        // area_threshold=5 means the speck (area=1) should be removed
        // Only the left blob should remain after despeckle(area=400)
        // Dilation=25, blur=5 to match defaults conceptually
        despeckle(alpha, 400, state, 25, 5);

        // Speck should be removed (multiplied by 0 safe zone)
        REQUIRE(alpha(8, 8) == Catch::Approx(0.0f));
        // Large region center should survive
        REQUIRE(alpha(2, 2) > 0.0f);
    }

    SECTION("All-zero image stays zero") {
        for (size_t i = 0; i < alpha.data.size(); ++i) {
            alpha.data[i] = 0.0f;
        }
        despeckle(alpha, 400, state, 25, 5);
        for (size_t i = 0; i < alpha.data.size(); ++i) {
            REQUIRE(alpha.data[i] == Catch::Approx(0.0f));
        }
    }

    SECTION("All-one image stays one") {
        for (size_t i = 0; i < alpha.data.size(); ++i) {
            alpha.data[i] = 1.0f;
        }
        // Single component covers all pixels (area=100), threshold=5
        despeckle(alpha, 5, state, 0, 0);
        for (size_t i = 0; i < alpha.data.size(); ++i) {
            REQUIRE(alpha.data[i] == Catch::Approx(1.0f));
        }
    }
}

TEST_CASE("despeckle handles edge cases", "[unit][despeckle]") {
    DespeckleState state;

    SECTION("Zero area_threshold is no-op") {
        ImageBuffer alpha_buf(3, 3, 1);
        Image alpha = alpha_buf.view();
        alpha.data[4] = 0.5f;
        float original = alpha.data[4];
        despeckle(alpha, 0, state);
        REQUIRE(alpha.data[4] == Catch::Approx(original));
    }

    SECTION("Empty image") {
        ImageBuffer empty_buf(0, 0, 1);
        despeckle(empty_buf.view(), 400, state);
    }
}
