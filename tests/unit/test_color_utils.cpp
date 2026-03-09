#include <catch2/catch_all.hpp>
#include "post_process/color_utils.hpp"
#include <cmath>

using namespace corridorkey;

TEST_CASE("ColorUtils::srgb_to_linear and linear_to_srgb roundtrip", "[unit][color]") {
    Image img;
    img.width = 1;
    img.height = 1;
    img.channels = 1;
    
    // Test a wide range of values to ensure LUT precision
    std::vector<float> test_values = { 0.0f, 0.01f, 0.1f, 0.25f, 0.5f, 0.75f, 0.9f, 0.99f, 1.0f };
    
    for (float val : test_values) {
        img.data = { val };
        
        ColorUtils::srgb_to_linear(img);
        float linear = img.data[0];
        
        ColorUtils::linear_to_srgb(img);
        float back = img.data[0];
        
        // 12-bit LUT (4096 steps) should be accurate within 0.0005 for roundtrips
        REQUIRE(back == Catch::Approx(val).margin(0.0005));
    }
}

TEST_CASE("ColorUtils::premultiply and unpremultiply", "[unit][color]") {
    Image rgb;
    rgb.width = 1;
    rgb.height = 1;
    rgb.channels = 3;
    rgb.data = { 1.0f, 0.5f, 0.2f }; // Linear straight color
    
    Image alpha;
    alpha.width = 1;
    alpha.height = 1;
    alpha.channels = 1;
    alpha.data = { 0.5f }; // Semi-transparent
    
    SECTION("Premultiply") {
        ColorUtils::premultiply(rgb, alpha);
        REQUIRE(rgb.data[0] == Catch::Approx(0.5f));
        REQUIRE(rgb.data[1] == Catch::Approx(0.25f));
        REQUIRE(rgb.data[2] == Catch::Approx(0.1f));
    }
    
    SECTION("Unpremultiply") {
        rgb.data = { 0.5f, 0.25f, 0.1f }; // Premultiplied
        ColorUtils::unpremultiply(rgb, alpha);
        REQUIRE(rgb.data[0] == Catch::Approx(1.0f));
        REQUIRE(rgb.data[1] == Catch::Approx(0.5f));
        REQUIRE(rgb.data[2] == Catch::Approx(0.2f));
    }
}

TEST_CASE("ColorUtils::resize bilinear", "[unit][color]") {
    Image img;
    img.width = 2;
    img.height = 2;
    img.channels = 1;
    // 2x2 gradient:
    // 0.0  1.0
    // 1.0  0.0
    img.data = { 0.0f, 1.0f, 1.0f, 0.0f };
    
    SECTION("Upscale to 3x3") {
        Image resized = ColorUtils::resize(img, 3, 3);
        REQUIRE(resized.width == 3);
        REQUIRE(resized.height == 3);
        REQUIRE(resized.data.size() == 9);
        
        // The center pixel of 3x3 (1,1) should be average of all 4 pixels of 2x2
        // src_x = (1 + 0.5) * (2/3) - 0.5 = 1.5 * 0.666... - 0.5 = 1.0 - 0.5 = 0.5
        // src_y = (1 + 0.5) * (2/3) - 0.5 = 0.5
        // Bilinear at (0.5, 0.5) should be 0.5
        REQUIRE(resized.data[4] == Catch::Approx(0.5f));
    }
    
    SECTION("Downscale to 1x1") {
        Image resized = ColorUtils::resize(img, 1, 1);
        REQUIRE(resized.width == 1);
        REQUIRE(resized.height == 1);
        // src_x = (0 + 0.5) * 2 - 0.5 = 0.5
        // src_y = 0.5
        REQUIRE(resized.data[0] == Catch::Approx(0.5f));
    }
}
