#include <catch2/catch_all.hpp>
#include "post_process/despeckle.hpp"

using namespace corridorkey;

TEST_CASE("despeckle removes small components via connected-component analysis", "[unit][despeckle]") {
    // 10x10 alpha: large connected region + isolated speck
    ImageBuffer alpha_buf(10, 10, 1);
    Image alpha = alpha_buf.view();

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
        // Large region (area=25) should be kept
        // Use small dilation and blur for test predictability
        despeckle(alpha, 5, 1, 0);

        // Speck should be removed (multiplied by 0 safe zone)
        REQUIRE(alpha(8, 8) == Catch::Approx(0.0f));
        // Large region center should survive
        REQUIRE(alpha(2, 2) > 0.0f);
    }

    SECTION("All-zero image stays zero") {
        for (size_t i = 0; i < alpha.data.size(); ++i) {
            alpha.data[i] = 0.0f;
        }
        despeckle(alpha, 400, 25, 5);
        for (size_t i = 0; i < alpha.data.size(); ++i) {
            REQUIRE(alpha.data[i] == Catch::Approx(0.0f));
        }
    }

    SECTION("All-one image stays one") {
        for (size_t i = 0; i < alpha.data.size(); ++i) {
            alpha.data[i] = 1.0f;
        }
        // Single component covers all pixels (area=100), threshold=5
        despeckle(alpha, 5, 0, 0);
        for (size_t i = 0; i < alpha.data.size(); ++i) {
            REQUIRE(alpha.data[i] == Catch::Approx(1.0f));
        }
    }
}

TEST_CASE("despeckle handles edge cases", "[unit][despeckle]") {
    SECTION("Zero area_threshold is no-op") {
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
