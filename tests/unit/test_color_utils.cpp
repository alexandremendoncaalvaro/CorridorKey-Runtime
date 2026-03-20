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

TEST_CASE("ColorUtils::resize_into matches resize output", "[unit][color][regression]") {
    ImageBuffer source_buf(2, 2, 1);
    Image source = source_buf.view();
    source.data[0] = 0.0f;
    source.data[1] = 1.0f;
    source.data[2] = 1.0f;
    source.data[3] = 0.0f;

    ImageBuffer expected = ColorUtils::resize(source, 3, 3);
    ImageBuffer actual(3, 3, 1);
    ColorUtils::resize_into(source, actual.view());

    for (size_t index = 0; index < actual.view().data.size(); ++index) {
        REQUIRE(actual.view().data[index] ==
                Catch::Approx(expected.view().data[index]).margin(0.0001f));
    }
}

TEST_CASE("ColorUtils::resize_lanczos upscale", "[unit][color]") {
    ImageBuffer buf(2, 2, 1);
    Image img = buf.view();
    // Uniform value -- Lanczos should preserve it exactly
    img.data[0] = 0.5f;
    img.data[1] = 0.5f;
    img.data[2] = 0.5f;
    img.data[3] = 0.5f;

    SECTION("Upscale uniform 2x2 to 4x4") {
        ImageBuffer resized_buf = ColorUtils::resize_lanczos(img, 4, 4);
        Image resized = resized_buf.view();
        REQUIRE(resized.width == 4);
        REQUIRE(resized.height == 4);
        for (size_t i = 0; i < resized.data.size(); ++i) {
            REQUIRE(resized.data[i] == Catch::Approx(0.5f).margin(0.01f));
        }
    }

    SECTION("Upscale to same size is identity") {
        ImageBuffer resized_buf = ColorUtils::resize_lanczos(img, 2, 2);
        Image resized = resized_buf.view();
        for (size_t i = 0; i < resized.data.size(); ++i) {
            REQUIRE(resized.data[i] == Catch::Approx(img.data[i]).margin(0.001f));
        }
    }
}

TEST_CASE("ColorUtils::resize_lanczos edge boundary reflect_101", "[unit][color][regression]") {
    // Regression: Lanczos4 used to truncate the kernel at image edges,
    // producing asymmetric weights and visible halos/stepping artifacts.
    // The fix uses BORDER_REFLECT_101 to maintain kernel symmetry.

    SECTION("Single-pixel image upscale preserves value") {
        ImageBuffer buf(1, 1, 1);
        buf.view().data[0] = 0.8f;
        ImageBuffer result = ColorUtils::resize_lanczos(buf.view(), 4, 4);
        Image out = result.view();
        for (size_t i = 0; i < out.data.size(); ++i) {
            REQUIRE(out.data[i] == Catch::Approx(0.8f).margin(0.01f));
        }
    }

    SECTION("Horizontal edge: left/right columns match on symmetric input") {
        ImageBuffer buf(8, 1, 1);
        Image img = buf.view();
        for (int x = 0; x < 8; ++x) {
            img(0, x) = static_cast<float>(x) / 7.0f;
        }

        ImageBuffer result = ColorUtils::resize_lanczos(img, 16, 1);
        Image out = result.view();

        // With reflect_101, an ascending ramp from 0..1 should produce a
        // monotonically non-decreasing output (no negative overshoots at left edge)
        for (int x = 1; x < 16; ++x) {
            REQUIRE(out(0, x) >= out(0, x - 1) - 0.05f);
        }
    }

    SECTION("Hard alpha edge: output stays in [0,1] range") {
        ImageBuffer buf(8, 8, 1);
        Image img = buf.view();
        for (int y = 0; y < 8; ++y) {
            for (int x = 0; x < 8; ++x) {
                img(y, x) = x < 4 ? 0.0f : 1.0f;
            }
        }

        ImageBuffer result = ColorUtils::resize_lanczos(img, 16, 16);
        Image out = result.view();
        for (size_t i = 0; i < out.data.size(); ++i) {
            // Lanczos ringing may produce slight overshoot, but reflect_101
            // should keep it much smaller than the old truncation artifacts
            REQUIRE(out.data[i] >= -0.15f);
            REQUIRE(out.data[i] <= 1.15f);
        }
    }
}

TEST_CASE("ColorUtils::resize_lanczos_into matches resize_lanczos output",
          "[unit][color][regression]") {
    ImageBuffer source_buf(4, 4, 1);
    Image source = source_buf.view();
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            source(y, x) = static_cast<float>(x + y) / 6.0f;
        }
    }

    ImageBuffer expected = ColorUtils::resize_lanczos(source, 6, 6);
    ImageBuffer actual(6, 6, 1);
    ColorUtils::resize_lanczos_into(source, actual.view());

    for (size_t index = 0; index < actual.view().data.size(); ++index) {
        REQUIRE(actual.view().data[index] ==
                Catch::Approx(expected.view().data[index]).margin(0.0001f));
    }
}

TEST_CASE("ColorUtils::clamp_image", "[unit][color]") {
    ImageBuffer buf(3, 1, 1);
    Image img = buf.view();
    img.data[0] = -0.1f;
    img.data[1] = 0.5f;
    img.data[2] = 1.2f;

    ColorUtils::clamp_image(img, 0.0f, 1.0f);

    REQUIRE(img.data[0] == Catch::Approx(0.0f));
    REQUIRE(img.data[1] == Catch::Approx(0.5f));
    REQUIRE(img.data[2] == Catch::Approx(1.0f));
}

TEST_CASE("ColorUtils::gaussian_blur smooths alpha", "[unit][color]") {
    SECTION("Uniform image is unchanged") {
        ImageBuffer buf(8, 8, 1);
        Image img = buf.view();
        std::fill(img.data.begin(), img.data.end(), 0.7f);

        ColorUtils::gaussian_blur(img, 1.0f);
        for (size_t i = 0; i < img.data.size(); ++i) {
            REQUIRE(img.data[i] == Catch::Approx(0.7f).margin(0.001f));
        }
    }

    SECTION("Hard edge is softened") {
        ImageBuffer buf(16, 1, 1);
        Image img = buf.view();
        for (int x = 0; x < 16; ++x) {
            img(0, x) = x < 8 ? 0.0f : 1.0f;
        }

        ColorUtils::gaussian_blur(img, 1.5f);

        // Pixels far from edge should stay near original
        REQUIRE(img(0, 0) < 0.05f);
        REQUIRE(img(0, 15) > 0.95f);
        // Transition region should now be gradual
        REQUIRE(img(0, 7) > 0.1f);
        REQUIRE(img(0, 7) < 0.5f);
        REQUIRE(img(0, 8) > 0.5f);
        REQUIRE(img(0, 8) < 0.9f);
    }

    SECTION("Zero sigma is no-op") {
        ImageBuffer buf(4, 4, 1);
        Image img = buf.view();
        img(0, 0) = 1.0f;
        img(0, 1) = 0.0f;

        ColorUtils::gaussian_blur(img, 0.0f);
        REQUIRE(img(0, 0) == 1.0f);
        REQUIRE(img(0, 1) == 0.0f);
    }
}

TEST_CASE("ColorUtils::resize_area anti-aliased downscale", "[unit][color]") {
    SECTION("Small downscale matches bilinear") {
        ImageBuffer buf(4, 4, 1);
        Image img = buf.view();
        std::fill(img.data.begin(), img.data.end(), 0.5f);

        ImageBuffer result = ColorUtils::resize_area(img, 3, 3);
        ImageBuffer ref = ColorUtils::resize(img, 3, 3);

        for (size_t i = 0; i < result.view().data.size(); ++i) {
            REQUIRE(result.view().data[i] == Catch::Approx(ref.view().data[i]).margin(0.001f));
        }
    }

    SECTION("Large downscale pre-filters high frequencies") {
        // Create a fine stripe pattern at 32x1 (alternating 0/1 every pixel)
        // Bilinear at 5x downscale will alias; area should smooth
        ImageBuffer buf(32, 1, 1);
        Image img = buf.view();
        for (int x = 0; x < 32; ++x) {
            img(0, x) = (x % 2 == 0) ? 0.0f : 1.0f;
        }

        // Downscale 32 -> 6 = 5.3x factor (well above 1.5x threshold)
        ImageBuffer area_result = ColorUtils::resize_area(img, 6, 1);
        Image area = area_result.view();

        // After pre-filtering, all output values should be near 0.5
        for (int x = 0; x < 6; ++x) {
            REQUIRE(area(0, x) > 0.3f);
            REQUIRE(area(0, x) < 0.7f);
        }
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
