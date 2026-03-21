#include <catch2/catch_all.hpp>

#include "post_process/source_passthrough.hpp"

using namespace corridorkey;

TEST_CASE("source_passthrough is no-op for transparent regions", "[unit][passthrough]") {
    // All alpha = 0.0 -> model foreground should be unchanged
    ImageBuffer src_buf(8, 8, 3);
    ImageBuffer fg_buf(8, 8, 3);
    ImageBuffer alpha_buf(8, 8, 1);

    Image src = src_buf.view();
    Image fg = fg_buf.view();
    Image alpha = alpha_buf.view();

    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) {
            src(y, x, 0) = 1.0F;
            src(y, x, 1) = 0.0F;
            src(y, x, 2) = 0.0F;
            fg(y, x, 0) = 0.0F;
            fg(y, x, 1) = 1.0F;
            fg(y, x, 2) = 0.0F;
            alpha(y, x) = 0.0F;
        }
    }

    ColorUtils::State state;
    source_passthrough(src, fg, alpha, 0, 0, state);

    // Foreground should remain green (source not blended in)
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) {
            CHECK(fg(y, x, 0) == Catch::Approx(0.0F));
            CHECK(fg(y, x, 1) == Catch::Approx(1.0F));
            CHECK(fg(y, x, 2) == Catch::Approx(0.0F));
        }
    }
}

TEST_CASE("source_passthrough blends source in fully opaque regions", "[unit][passthrough]") {
    // Large image with all alpha = 1.0, zero erode/blur -> output should equal source
    ImageBuffer src_buf(32, 32, 3);
    ImageBuffer fg_buf(32, 32, 3);
    ImageBuffer alpha_buf(32, 32, 1);

    Image src = src_buf.view();
    Image fg = fg_buf.view();
    Image alpha = alpha_buf.view();

    for (int y = 0; y < 32; ++y) {
        for (int x = 0; x < 32; ++x) {
            src(y, x, 0) = 0.8F;
            src(y, x, 1) = 0.2F;
            src(y, x, 2) = 0.1F;
            fg(y, x, 0) = 0.3F;
            fg(y, x, 1) = 0.5F;
            fg(y, x, 2) = 0.4F;
            alpha(y, x) = 1.0F;
        }
    }

    ColorUtils::State state;
    source_passthrough(src, fg, alpha, 0, 0, state);

    // With zero erode and zero blur, all pixels above threshold should be source
    for (int y = 0; y < 32; ++y) {
        for (int x = 0; x < 32; ++x) {
            CHECK(fg(y, x, 0) == Catch::Approx(0.8F));
            CHECK(fg(y, x, 1) == Catch::Approx(0.2F));
            CHECK(fg(y, x, 2) == Catch::Approx(0.1F));
        }
    }
}

TEST_CASE("source_passthrough respects threshold at 0.95", "[unit][passthrough]") {
    ImageBuffer src_buf(4, 4, 3);
    ImageBuffer fg_buf(4, 4, 3);
    ImageBuffer alpha_buf(4, 4, 1);

    Image src = src_buf.view();
    Image fg = fg_buf.view();
    Image alpha = alpha_buf.view();

    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            src(y, x, 0) = 1.0F;
            src(y, x, 1) = 1.0F;
            src(y, x, 2) = 1.0F;
            fg(y, x, 0) = 0.0F;
            fg(y, x, 1) = 0.0F;
            fg(y, x, 2) = 0.0F;
            alpha(y, x) = 0.94F;  // Just below threshold
        }
    }

    ColorUtils::State state;
    source_passthrough(src, fg, alpha, 0, 0, state);

    // Below threshold -> model fg unchanged (black)
    CHECK(fg(0, 0, 0) == Catch::Approx(0.0F));

    // Now set above threshold
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            fg(y, x, 0) = 0.0F;
            fg(y, x, 1) = 0.0F;
            fg(y, x, 2) = 0.0F;
            alpha(y, x) = 0.96F;  // Above threshold
        }
    }

    source_passthrough(src, fg, alpha, 0, 0, state);

    // Above threshold -> source (white)
    CHECK(fg(0, 0, 0) == Catch::Approx(1.0F));
}

TEST_CASE("source_passthrough handles empty images", "[unit][passthrough]") {
    ImageBuffer empty;
    ImageBuffer fg_buf(4, 4, 3);
    ImageBuffer alpha_buf(4, 4, 1);

    ColorUtils::State state;
    // Should not crash
    source_passthrough(empty.view(), fg_buf.view(), alpha_buf.view(), 3, 7, state);
    source_passthrough(fg_buf.view(), empty.view(), alpha_buf.view(), 3, 7, state);
    source_passthrough(fg_buf.view(), fg_buf.view(), empty.view(), 3, 7, state);
}

TEST_CASE("source_passthrough with erosion shrinks interior", "[unit][passthrough]") {
    // 32x32 image with opaque center circle (r=12) surrounded by transparent background
    ImageBuffer src_buf(32, 32, 3);
    ImageBuffer fg_buf(32, 32, 3);
    ImageBuffer alpha_buf(32, 32, 1);

    Image src = src_buf.view();
    Image fg = fg_buf.view();
    Image alpha = alpha_buf.view();

    for (int y = 0; y < 32; ++y) {
        for (int x = 0; x < 32; ++x) {
            src(y, x, 0) = 1.0F;
            src(y, x, 1) = 0.0F;
            src(y, x, 2) = 0.0F;
            fg(y, x, 0) = 0.0F;
            fg(y, x, 1) = 0.0F;
            fg(y, x, 2) = 1.0F;

            float dy = static_cast<float>(y) - 16.0F;
            float dx = static_cast<float>(x) - 16.0F;
            alpha(y, x) = (dy * dy + dx * dx < 12.0F * 12.0F) ? 1.0F : 0.0F;
        }
    }

    ColorUtils::State state;
    source_passthrough(src, fg, alpha, 3, 0, state);

    // Center pixel (16,16) should be source (red) -- well inside the circle
    CHECK(fg(16, 16, 0) == Catch::Approx(1.0F));
    CHECK(fg(16, 16, 2) == Catch::Approx(0.0F));

    // Corner pixel (0,0) is transparent -> model fg (blue) unchanged
    CHECK(fg(0, 0, 0) == Catch::Approx(0.0F));
    CHECK(fg(0, 0, 2) == Catch::Approx(1.0F));

    // Edge of circle (~radius 12, after erode_px=3 the interior shrinks by ~3px)
    // Pixel at (16, 27) is at distance 11 from center -- near the edge
    // After erosion, should be eroded away -> model fg (blue)
    CHECK(fg(16, 27, 2) == Catch::Approx(1.0F));
}
