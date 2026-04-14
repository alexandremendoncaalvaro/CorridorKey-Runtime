#include <algorithm>

#include <catch2/catch_all.hpp>

#include "plugins/ofx/ofx_screen_color.hpp"
#include "post_process/color_utils.hpp"
#include "post_process/despill.hpp"

using namespace corridorkey;
using namespace corridorkey::ofx;

namespace {

ImageBuffer copy_image(Image source) {
    ImageBuffer copy(source.width, source.height, source.channels);
    std::copy(source.data.begin(), source.data.end(), copy.view().data.begin());
    return copy;
}

void require_images_equal(Image actual, Image expected, float margin = 0.0001F) {
    REQUIRE(actual.width == expected.width);
    REQUIRE(actual.height == expected.height);
    REQUIRE(actual.channels == expected.channels);
    REQUIRE(actual.data.size() == expected.data.size());

    for (std::size_t index = 0; index < actual.data.size(); ++index) {
        CHECK(actual.data[index] == Catch::Approx(expected.data[index]).margin(margin));
    }
}

ImageBuffer make_green_domain_sample() {
    ImageBuffer image(2, 2, 3);
    Image view = image.view();
    view(0, 0, 0) = 0.15F;
    view(0, 0, 1) = 0.82F;
    view(0, 0, 2) = 0.10F;

    view(0, 1, 0) = 0.70F;
    view(0, 1, 1) = 0.20F;
    view(0, 1, 2) = 0.15F;

    view(1, 0, 0) = 0.18F;
    view(1, 0, 1) = 0.77F;
    view(1, 0, 2) = 0.12F;

    view(1, 1, 0) = 0.61F;
    view(1, 1, 1) = 0.32F;
    view(1, 1, 2) = 0.28F;
    return image;
}

}  // namespace

TEST_CASE("screen color helpers canonicalize and restore blue input", "[unit][ofx][regression]") {
    ImageBuffer source = make_green_domain_sample();

    SECTION("choice mapping defaults to green") {
        CHECK(screen_color_mode_from_choice(kScreenColorGreen) == ScreenColorMode::Green);
        CHECK(screen_color_mode_from_choice(kScreenColorBlue) == ScreenColorMode::Blue);
        CHECK(screen_color_mode_from_choice(99) == ScreenColorMode::Green);
    }

    SECTION("green mode stays unchanged") {
        ImageBuffer green = copy_image(source.view());

        canonicalize_to_green_domain(green.view(), ScreenColorMode::Green);
        restore_from_green_domain(green.view(), ScreenColorMode::Green);

        require_images_equal(green.view(), source.view());
    }

    SECTION("blue mode swaps green and blue exactly once") {
        ImageBuffer blue = copy_image(source.view());

        canonicalize_to_green_domain(blue.view(), ScreenColorMode::Blue);
        CHECK(blue.view()(0, 0, 1) == Catch::Approx(source.view()(0, 0, 2)));
        CHECK(blue.view()(0, 0, 2) == Catch::Approx(source.view()(0, 0, 1)));

        restore_from_green_domain(blue.view(), ScreenColorMode::Blue);
        require_images_equal(blue.view(), source.view());
    }
}

TEST_CASE("despill stays equivalent for blue screen via canonicalization",
          "[unit][ofx][regression]") {
    ImageBuffer green_source = make_green_domain_sample();
    constexpr float kStrength = 0.7F;

    auto run_case = [&](SpillMethod method) {
        ImageBuffer green_result = copy_image(green_source.view());
        despill(green_result.view(), kStrength, method);

        ImageBuffer blue_input = copy_image(green_source.view());
        restore_from_green_domain(blue_input.view(), ScreenColorMode::Blue);
        canonicalize_to_green_domain(blue_input.view(), ScreenColorMode::Blue);
        despill(blue_input.view(), kStrength, method);
        restore_from_green_domain(blue_input.view(), ScreenColorMode::Blue);

        ImageBuffer expected_blue = copy_image(green_result.view());
        restore_from_green_domain(expected_blue.view(), ScreenColorMode::Blue);
        require_images_equal(blue_input.view(), expected_blue.view());
    };

    SECTION("average spill replacement") {
        run_case(SpillMethod::Average);
    }

    SECTION("double limit spill replacement") {
        run_case(SpillMethod::DoubleLimit);
    }

    SECTION("neutral spill replacement") {
        run_case(SpillMethod::Neutral);
    }
}

TEST_CASE("rough matte fallback stays equivalent for blue screen via canonicalization",
          "[unit][ofx][regression]") {
    ImageBuffer green_source = make_green_domain_sample();
    ImageBuffer green_matte(2, 2, 1);
    ColorUtils::generate_rough_matte(green_source.view(), green_matte.view());

    ImageBuffer blue_source = copy_image(green_source.view());
    restore_from_green_domain(blue_source.view(), ScreenColorMode::Blue);
    canonicalize_to_green_domain(blue_source.view(), ScreenColorMode::Blue);

    ImageBuffer blue_matte(2, 2, 1);
    ColorUtils::generate_rough_matte(blue_source.view(), blue_matte.view());

    require_images_equal(blue_matte.view(), green_matte.view());
}
