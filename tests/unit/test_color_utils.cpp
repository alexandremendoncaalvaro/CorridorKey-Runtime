#include <catch2/catch_all.hpp>
#include <cmath>

#include "post_process/color_utils.hpp"

using namespace corridorkey;

TEST_CASE("ColorUtils::srgb_to_linear and linear_to_srgb roundtrip", "[unit][color]") {
    ImageBuffer buffer(1, 1, 1);
    Image img = buffer.view();

    // Test a wide range of values to ensure LUT precision
    std::vector<float> test_values = {0.0f, 0.01f, 0.1f, 0.25f, 0.5f, 0.75f, 0.9f, 0.99f, 1.0f};

    for (float val : test_values) {
        img.data[0] = val;

        ColorUtils::srgb_to_linear(img);

        ColorUtils::linear_to_srgb(img);
        float back = img.data[0];

        // 12-bit LUT (4096 steps) should be accurate within 0.0005 for roundtrips
        REQUIRE(back == Catch::Approx(val).margin(0.0005));
    }
}

TEST_CASE("ColorUtils::premultiply and unpremultiply", "[unit][color]") {
    ImageBuffer rgb_buf(1, 1, 3);
    Image rgb = rgb_buf.view();
    rgb.data[0] = 1.0f;
    rgb.data[1] = 0.5f;
    rgb.data[2] = 0.2f;

    ImageBuffer alpha_buf(1, 1, 1);
    Image alpha = alpha_buf.view();
    alpha.data[0] = 0.5f;

    SECTION("Premultiply") {
        ColorUtils::premultiply(rgb, alpha);
        REQUIRE(rgb.data[0] == Catch::Approx(0.5f));
        REQUIRE(rgb.data[1] == Catch::Approx(0.25f));
        REQUIRE(rgb.data[2] == Catch::Approx(0.1f));
    }

    SECTION("Unpremultiply") {
        rgb.data[0] = 0.5f;
        rgb.data[1] = 0.25f;
        rgb.data[2] = 0.1f;
        ColorUtils::unpremultiply(rgb, alpha);
        REQUIRE(rgb.data[0] == Catch::Approx(1.0f));
        REQUIRE(rgb.data[1] == Catch::Approx(0.5f));
        REQUIRE(rgb.data[2] == Catch::Approx(0.2f));
    }
}

TEST_CASE("ColorUtils::resize bilinear", "[unit][color]") {
    ImageBuffer buf(2, 2, 1);
    Image img = buf.view();
    // 2x2 gradient:
    // 0.0  1.0
    // 1.0  0.0
    img.data[0] = 0.0f;
    img.data[1] = 1.0f;
    img.data[2] = 1.0f;
    img.data[3] = 0.0f;

    SECTION("Upscale to 3x3") {
        ImageBuffer resized_buf = ColorUtils::resize(img, 3, 3);
        Image resized = resized_buf.view();
        REQUIRE(resized.width == 3);
        REQUIRE(resized.height == 3);
        REQUIRE(resized.data.size() == 9);

        // The center pixel of 3x3 (1,1) should be average of all 4 pixels of 2x2
        REQUIRE(resized.data[4] == Catch::Approx(0.5f));
    }

    SECTION("Downscale to 1x1") {
        ImageBuffer resized_buf = ColorUtils::resize(img, 1, 1);
        Image resized = resized_buf.view();
        REQUIRE(resized.width == 1);
        REQUIRE(resized.height == 1);
        REQUIRE(resized.data[0] == Catch::Approx(0.5f));
    }
}

TEST_CASE("ColorUtils::generate_rough_matte", "[unit][color]") {
    ImageBuffer rgb_buf(2, 1, 3);
    Image rgb = rgb_buf.view();

    // Pixel 0: Pure green
    rgb(0, 0, 0) = 0.0f;
    rgb(0, 0, 1) = 1.0f;
    rgb(0, 0, 2) = 0.0f;
    // Pixel 1: Pure red
    rgb(0, 1, 0) = 1.0f;
    rgb(0, 1, 1) = 0.0f;
    rgb(0, 1, 2) = 0.0f;

    ImageBuffer hint_buf(2, 1, 1);
    Image hint = hint_buf.view();

    ColorUtils::generate_rough_matte(rgb, hint);

    // Pure green should be transparent (alpha 0) in hint
    REQUIRE(hint(0, 0, 0) == Catch::Approx(0.0f));
    // Pure red should be opaque (alpha 1) in hint
    REQUIRE(hint(0, 1, 0) == Catch::Approx(1.0f));
}
