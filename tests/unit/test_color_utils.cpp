#include <array>
#include <catch2/catch_all.hpp>
#include <cmath>
#include <limits>
#include <vector>

#include "common/srgb_lut.hpp"
#include "post_process/color_utils.hpp"

using namespace corridorkey;

namespace {

std::vector<float> planar_copy(Image image) {
    std::vector<float> planar(image.data.size());
    ColorUtils::to_planar(image, planar.data());
    return planar;
}

}  // namespace

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

TEST_CASE("SrgbLut handles non-finite input safely", "[unit][color][regression]") {
    const auto& lut = SrgbLut::instance();

    REQUIRE(lut.to_linear(std::nanf("")) == Catch::Approx(0.0F));
    REQUIRE(lut.to_linear(std::numeric_limits<float>::infinity()) == Catch::Approx(1.0F));
    REQUIRE(lut.to_srgb(std::nanf("")) == Catch::Approx(0.0F));
    REQUIRE(lut.to_srgb(-std::numeric_limits<float>::infinity()) == Catch::Approx(0.0F));
    REQUIRE(lut.to_srgb(std::numeric_limits<float>::infinity()) == Catch::Approx(1.0F));
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

TEST_CASE("ColorUtils::composite_premultiplied_over_checker_to_srgb matches legacy pipeline",
          "[unit][color][regression]") {
    ImageBuffer processed_buf(4, 4, 4);
    Image processed = processed_buf.view();

    for (int y_pos = 0; y_pos < processed.height; ++y_pos) {
        for (int x_pos = 0; x_pos < processed.width; ++x_pos) {
            processed(y_pos, x_pos, 0) = static_cast<float>(x_pos + 1) / 10.0F;
            processed(y_pos, x_pos, 1) = static_cast<float>(y_pos + 2) / 10.0F;
            processed(y_pos, x_pos, 2) = static_cast<float>(x_pos + y_pos + 3) / 12.0F;
            processed(y_pos, x_pos, 3) = static_cast<float>((x_pos + y_pos) % 4) / 3.0F;
        }
    }

    ImageBuffer legacy_buf(4, 4, 4);
    std::copy(processed.data.begin(), processed.data.end(), legacy_buf.view().data.begin());
    ColorUtils::composite_over_checker(legacy_buf.view());
    ColorUtils::linear_to_srgb(legacy_buf.view());

    ImageBuffer fused_buf(4, 4, 4);
    ColorUtils::composite_premultiplied_over_checker_to_srgb(processed, fused_buf.view());

    for (size_t index = 0; index < fused_buf.view().data.size(); ++index) {
        REQUIRE(fused_buf.view().data[index] ==
                Catch::Approx(legacy_buf.view().data[index]).margin(0.0001F));
    }
}

TEST_CASE("ColorUtils::resize_from_planar_into matches resize output",
          "[unit][color][regression]") {
    ImageBuffer source_buf(4, 3, 3);
    Image source = source_buf.view();
    for (int y_pos = 0; y_pos < source.height; ++y_pos) {
        for (int x_pos = 0; x_pos < source.width; ++x_pos) {
            source(y_pos, x_pos, 0) = static_cast<float>(x_pos + y_pos) / 8.0f;
            source(y_pos, x_pos, 1) = static_cast<float>((x_pos * 2) + y_pos) / 10.0f;
            source(y_pos, x_pos, 2) = static_cast<float>(x_pos + (y_pos * 3)) / 12.0f;
        }
    }

    const std::vector<float> planar = planar_copy(source);

    SECTION("Resample to a different size") {
        ImageBuffer expected = ColorUtils::resize(source, 7, 5);
        ImageBuffer actual(7, 5, 3);
        ColorUtils::resize_from_planar_into(planar.data(), source.width, source.height,
                                            source.channels, actual.view());

        for (size_t index = 0; index < actual.view().data.size(); ++index) {
            REQUIRE(actual.view().data[index] ==
                    Catch::Approx(expected.view().data[index]).margin(0.0001f));
        }
    }

    SECTION("Identity resize matches source pixels") {
        ImageBuffer actual(source.width, source.height, source.channels);
        ColorUtils::resize_from_planar_into(planar.data(), source.width, source.height,
                                            source.channels, actual.view());

        for (size_t index = 0; index < actual.view().data.size(); ++index) {
            REQUIRE(actual.view().data[index] == Catch::Approx(source.data[index]).margin(0.0001f));
        }
    }
}

TEST_CASE("ColorUtils::resize_alpha_fg_from_planar_into matches separate resizes",
          "[unit][color][regression]") {
    ImageBuffer alpha_source_buf(4, 3, 1);
    Image alpha_source = alpha_source_buf.view();
    ImageBuffer fg_source_buf(4, 3, 3);
    Image fg_source = fg_source_buf.view();

    for (int y_pos = 0; y_pos < alpha_source.height; ++y_pos) {
        for (int x_pos = 0; x_pos < alpha_source.width; ++x_pos) {
            alpha_source(y_pos, x_pos, 0) = static_cast<float>(x_pos + y_pos) / 6.0f;
            fg_source(y_pos, x_pos, 0) = static_cast<float>((x_pos * 2) + y_pos) / 9.0f;
            fg_source(y_pos, x_pos, 1) = static_cast<float>(x_pos + (y_pos * 2)) / 10.0f;
            fg_source(y_pos, x_pos, 2) = static_cast<float>((x_pos * 3) + y_pos) / 12.0f;
        }
    }

    const std::vector<float> alpha_planar = planar_copy(alpha_source);
    const std::vector<float> fg_planar = planar_copy(fg_source);

    ImageBuffer expected_alpha = ColorUtils::resize(alpha_source, 7, 5);
    ImageBuffer expected_fg = ColorUtils::resize(fg_source, 7, 5);
    ImageBuffer actual_alpha(7, 5, 1);
    ImageBuffer actual_fg(7, 5, 3);

    ColorUtils::resize_alpha_fg_from_planar_into(alpha_planar.data(), fg_planar.data(),
                                                 alpha_source.width, alpha_source.height,
                                                 actual_alpha.view(), actual_fg.view());

    for (size_t index = 0; index < actual_alpha.view().data.size(); ++index) {
        REQUIRE(actual_alpha.view().data[index] ==
                Catch::Approx(expected_alpha.view().data[index]).margin(0.0001f));
    }
    for (size_t index = 0; index < actual_fg.view().data.size(); ++index) {
        REQUIRE(actual_fg.view().data[index] ==
                Catch::Approx(expected_fg.view().data[index]).margin(0.0001f));
    }
}

TEST_CASE("ColorUtils::pack_normalized_rgb_and_hint_to_planar matches manual packing",
          "[unit][color][regression]") {
    ImageBuffer rgb_buf(2, 2, 3);
    Image rgb = rgb_buf.view();
    ImageBuffer hint_buf(2, 2, 1);
    Image hint = hint_buf.view();

    rgb(0, 0, 0) = 0.1F;
    rgb(0, 0, 1) = 0.2F;
    rgb(0, 0, 2) = 0.3F;
    rgb(0, 1, 0) = 0.4F;
    rgb(0, 1, 1) = 0.5F;
    rgb(0, 1, 2) = 0.6F;
    rgb(1, 0, 0) = 0.7F;
    rgb(1, 0, 1) = 0.8F;
    rgb(1, 0, 2) = 0.9F;
    rgb(1, 1, 0) = 0.2F;
    rgb(1, 1, 1) = 0.3F;
    rgb(1, 1, 2) = 0.4F;

    hint(0, 0, 0) = 0.9F;
    hint(0, 1, 0) = 0.7F;
    hint(1, 0, 0) = 0.5F;
    hint(1, 1, 0) = 0.3F;

    constexpr std::array<float, 3> mean = {0.485F, 0.456F, 0.406F};
    constexpr std::array<float, 3> inv_stddev = {
        1.0F / 0.229F,
        1.0F / 0.224F,
        1.0F / 0.225F,
    };

    std::vector<float> actual(16, 0.0F);
    std::vector<float> expected(16, 0.0F);

    ColorUtils::pack_normalized_rgb_and_hint_to_planar(rgb, hint, actual.data(), mean, inv_stddev);

    const size_t channel_stride = 4;
    for (int y_pos = 0; y_pos < rgb.height; ++y_pos) {
        for (int x_pos = 0; x_pos < rgb.width; ++x_pos) {
            const size_t idx = static_cast<size_t>(y_pos) * static_cast<size_t>(rgb.width) +
                               static_cast<size_t>(x_pos);
            expected[0 * channel_stride + idx] = (rgb(y_pos, x_pos, 0) - mean[0]) * inv_stddev[0];
            expected[1 * channel_stride + idx] = (rgb(y_pos, x_pos, 1) - mean[1]) * inv_stddev[1];
            expected[2 * channel_stride + idx] = (rgb(y_pos, x_pos, 2) - mean[2]) * inv_stddev[2];
            expected[3 * channel_stride + idx] = hint(y_pos, x_pos, 0);
        }
    }

    for (size_t index = 0; index < actual.size(); ++index) {
        REQUIRE(actual[index] == Catch::Approx(expected[index]).margin(0.0001F));
    }
}

TEST_CASE("ColorUtils::resize_area_into safely drops alpha for RGB model inputs",
          "[unit][color][regression]") {
    ImageBuffer rgba_source_buf(2, 2, 4);
    Image rgba_source = rgba_source_buf.view();

    const std::array<float, 16> rgba_values = {
        0.0F, 0.1F, 0.2F, 0.3F, 0.4F, 0.5F, 0.6F, 0.7F,
        0.8F, 0.9F, 1.0F, 0.2F, 0.3F, 0.2F, 0.1F, 0.0F,
    };
    for (size_t index = 0; index < rgba_values.size(); ++index) {
        rgba_source.data[index] = rgba_values[index];
    }

    ImageBuffer rgb_source_buf(2, 2, 3);
    Image rgb_source = rgb_source_buf.view();
    for (int y_pos = 0; y_pos < rgba_source.height; ++y_pos) {
        for (int x_pos = 0; x_pos < rgba_source.width; ++x_pos) {
            for (int channel = 0; channel < 3; ++channel) {
                rgb_source(y_pos, x_pos, channel) = rgba_source(y_pos, x_pos, channel);
            }
        }
    }

    ColorUtils::State state;
    ImageBuffer actual_buf(3, 3, 3);
    ColorUtils::resize_area_into(rgba_source, actual_buf.view(), state);

    ImageBuffer expected_buf = ColorUtils::resize(rgb_source, 3, 3);
    for (size_t index = 0; index < actual_buf.view().data.size(); ++index) {
        REQUIRE(actual_buf.view().data[index] ==
                Catch::Approx(expected_buf.view().data[index]).margin(0.0001F));
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
        ColorUtils::State state;
        ImageBuffer resized_buf = ColorUtils::resize_lanczos(img, 4, 4, state);
        Image resized = resized_buf.view();
        REQUIRE(resized.width == 4);
        REQUIRE(resized.height == 4);
        for (size_t i = 0; i < resized.data.size(); ++i) {
            REQUIRE(resized.data[i] == Catch::Approx(0.5f).margin(0.01f));
        }
    }

    SECTION("Upscale to same size is identity") {
        ColorUtils::State state;
        ImageBuffer resized_buf = ColorUtils::resize_lanczos(img, 2, 2, state);
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
        ColorUtils::State state;
        ImageBuffer result = ColorUtils::resize_lanczos(buf.view(), 4, 4, state);
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

        ColorUtils::State state;
        ImageBuffer result = ColorUtils::resize_lanczos(img, 16, 1, state);
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

        ColorUtils::State state;
        ImageBuffer result = ColorUtils::resize_lanczos(img, 16, 16, state);
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

    ColorUtils::State state;
    ImageBuffer expected = ColorUtils::resize_lanczos(source, 6, 6, state);
    ImageBuffer actual(6, 6, 1);
    ColorUtils::resize_lanczos_into(source, actual.view(), state);

    for (size_t index = 0; index < actual.view().data.size(); ++index) {
        REQUIRE(actual.view().data[index] ==
                Catch::Approx(expected.view().data[index]).margin(0.0001f));
    }
}

TEST_CASE("ColorUtils::resize_lanczos_from_planar_into matches resize_lanczos output",
          "[unit][color][regression]") {
    ImageBuffer source_buf(4, 4, 1);
    Image source = source_buf.view();
    for (int y_pos = 0; y_pos < source.height; ++y_pos) {
        for (int x_pos = 0; x_pos < source.width; ++x_pos) {
            source(y_pos, x_pos) = static_cast<float>((x_pos * 2) + y_pos) / 9.0f;
        }
    }

    const std::vector<float> planar = planar_copy(source);
    ColorUtils::State state;
    ImageBuffer expected = ColorUtils::resize_lanczos(source, 6, 5, state);
    ImageBuffer actual(6, 5, 1);
    ColorUtils::resize_lanczos_from_planar_into(planar.data(), source.width, source.height,
                                                source.channels, actual.view(), state);

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

        ColorUtils::State state;
        ColorUtils::gaussian_blur(img, 1.0f, state);
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

        ColorUtils::State state;
        ColorUtils::gaussian_blur(img, 1.5f, state);

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

        ColorUtils::State state;
        ColorUtils::gaussian_blur(img, 0.0f, state);
        REQUIRE(img(0, 0) == 1.0f);
        REQUIRE(img(0, 1) == 0.0f);
    }
}

TEST_CASE("ColorUtils::resize_area anti-aliased downscale", "[unit][color]") {
    SECTION("Small downscale matches bilinear") {
        ImageBuffer buf(4, 4, 1);
        Image img = buf.view();
        std::fill(img.data.begin(), img.data.end(), 0.5f);

        ColorUtils::State state;
        ImageBuffer result = ColorUtils::resize_area(img, 3, 3, state);
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
        ColorUtils::State state;
        ImageBuffer area_result = ColorUtils::resize_area(img, 6, 1, state);
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
