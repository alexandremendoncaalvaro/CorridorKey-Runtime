#include <array>
#include <catch2/catch_all.hpp>

#include "plugins/ofx/ofx_image_utils.hpp"

using namespace corridorkey;
using namespace corridorkey::ofx;

TEST_CASE("alpha hint uses the RGBA alpha channel", "[unit][ofx][regression]") {
    ImageBuffer hint_buffer(2, 1, 1);
    std::array<float, 8> rgba_pixels = {
        0.10F, 0.20F, 0.30F, 0.40F, 0.50F, 0.60F, 0.70F, 0.80F,
    };

    copy_alpha_hint(hint_buffer.view(), rgba_pixels.data(), static_cast<int>(4 * sizeof(float) * 2),
                    kOfxBitDepthFloat, kOfxImageComponentRGBA);

    REQUIRE(hint_buffer.view()(0, 0) == Catch::Approx(0.40F));
    REQUIRE(hint_buffer.view()(0, 1) == Catch::Approx(0.80F));
    REQUIRE(alpha_hint_interpretation_label(kOfxImageComponentRGBA) == "alpha_channel");
}

TEST_CASE("alpha hint uses the single alpha channel directly", "[unit][ofx][regression]") {
    ImageBuffer hint_buffer(2, 1, 1);
    std::array<unsigned char, 2> alpha_pixels = {64, 191};

    copy_alpha_hint(hint_buffer.view(), alpha_pixels.data(), 2, kOfxBitDepthByte,
                    kOfxImageComponentAlpha);

    REQUIRE(hint_buffer.view()(0, 0) == Catch::Approx(64.0F / 255.0F));
    REQUIRE(hint_buffer.view()(0, 1) == Catch::Approx(191.0F / 255.0F));
    REQUIRE(alpha_hint_interpretation_label(kOfxImageComponentAlpha) == "single_channel");
}

TEST_CASE("alpha hint falls back to the red channel for RGB inputs", "[unit][ofx][regression]") {
    ImageBuffer hint_buffer(2, 1, 1);
    std::array<unsigned char, 6> rgb_pixels = {32, 200, 10, 128, 40, 240};

    copy_alpha_hint(hint_buffer.view(), rgb_pixels.data(), 6, kOfxBitDepthByte,
                    kOfxImageComponentRGB);

    REQUIRE(hint_buffer.view()(0, 0) == Catch::Approx(32.0F / 255.0F));
    REQUIRE(hint_buffer.view()(0, 1) == Catch::Approx(128.0F / 255.0F));
    REQUIRE(alpha_hint_interpretation_label(kOfxImageComponentRGB) == "red_channel");
}
