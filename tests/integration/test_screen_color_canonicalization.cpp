#include <algorithm>
#include <filesystem>

#include <catch2/catch_all.hpp>
#include <corridorkey/frame_io.hpp>

#include "plugins/ofx/ofx_screen_color.hpp"
#include "post_process/color_utils.hpp"
#include "post_process/despill.hpp"
#include "post_process/source_passthrough.hpp"

using namespace corridorkey;
using namespace corridorkey::ofx;

namespace {

std::filesystem::path fixture_path(const char* filename) {
    return std::filesystem::path(PROJECT_ROOT) / "tests" / "fixtures" / filename;
}

ImageBuffer load_fixture(const char* filename) {
    auto image = frame_io::read_frame(fixture_path(filename));
    REQUIRE(image.has_value());
    return std::move(image.value());
}

ImageBuffer copy_image(Image source) {
    ImageBuffer copy(source.width, source.height, source.channels);
    std::copy(source.data.begin(), source.data.end(), copy.view().data.begin());
    return copy;
}

void require_images_close(Image actual, Image expected, float margin = 0.02F) {
    REQUIRE(actual.width == expected.width);
    REQUIRE(actual.height == expected.height);
    REQUIRE(actual.channels == expected.channels);
    REQUIRE(actual.data.size() == expected.data.size());

    for (std::size_t index = 0; index < actual.data.size(); ++index) {
        CHECK(actual.data[index] == Catch::Approx(expected.data[index]).margin(margin));
    }
}

float mean_channel(Image image, int channel) {
    float sum = 0.0F;
    const int pixel_count = image.width * image.height;
    for (int y = 0; y < image.height; ++y) {
        for (int x = 0; x < image.width; ++x) {
            sum += image(y, x, channel);
        }
    }
    return pixel_count > 0 ? sum / static_cast<float>(pixel_count) : 0.0F;
}

ImageBuffer make_reference_alpha(int width, int height) {
    ImageBuffer alpha(width, height, 1);
    Image view = alpha.view();
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const bool interior = x > width / 4 && x < (width * 3) / 4 && y > height / 5 &&
                                  y < (height * 4) / 5;
            view(y, x, 0) = interior ? 1.0F : 0.35F;
        }
    }
    return alpha;
}

ImageBuffer make_model_foreground(Image source) {
    ImageBuffer foreground(source.width, source.height, source.channels);
    Image view = foreground.view();
    for (int y = 0; y < source.height; ++y) {
        for (int x = 0; x < source.width; ++x) {
            view(y, x, 0) = std::clamp(source(y, x, 0) * 0.82F + 0.04F, 0.0F, 1.0F);
            view(y, x, 1) = std::clamp(source(y, x, 1) * 0.88F, 0.0F, 1.0F);
            view(y, x, 2) = std::clamp(source(y, x, 2) * 1.05F, 0.0F, 1.0F);
        }
    }
    return foreground;
}

}  // namespace

TEST_CASE("real green and blue fixtures preserve rough matte equivalence through canonicalization",
          "[integration][postprocess][regression]") {
    ImageBuffer green_fixture = load_fixture("greenscreen_reference_128.png");
    ImageBuffer blue_fixture = load_fixture("bluescreen_reference_128.png");

    REQUIRE(mean_channel(green_fixture.view(), 1) > mean_channel(green_fixture.view(), 2));
    REQUIRE(mean_channel(blue_fixture.view(), 2) > mean_channel(blue_fixture.view(), 1));

    ImageBuffer green_matte(green_fixture.view().width, green_fixture.view().height, 1);
    ColorUtils::generate_rough_matte(green_fixture.view(), green_matte.view());

    canonicalize_to_green_domain(blue_fixture.view(), ScreenColorMode::Blue);
    ImageBuffer blue_matte(blue_fixture.view().width, blue_fixture.view().height, 1);
    ColorUtils::generate_rough_matte(blue_fixture.view(), blue_matte.view());

    require_images_close(blue_matte.view(), green_matte.view(), 0.01F);
}

TEST_CASE("real green and blue fixtures preserve despill equivalence through canonicalization",
          "[integration][postprocess][regression]") {
    ImageBuffer green_fixture = load_fixture("greenscreen_reference_128.png");
    ImageBuffer blue_fixture = load_fixture("bluescreen_reference_128.png");

    ImageBuffer green_result = copy_image(green_fixture.view());
    despill(green_result.view(), 0.7F, SpillMethod::Average);

    canonicalize_to_green_domain(blue_fixture.view(), ScreenColorMode::Blue);
    despill(blue_fixture.view(), 0.7F, SpillMethod::Average);
    restore_from_green_domain(blue_fixture.view(), ScreenColorMode::Blue);

    ImageBuffer expected_blue = copy_image(green_result.view());
    restore_from_green_domain(expected_blue.view(), ScreenColorMode::Blue);
    require_images_close(blue_fixture.view(), expected_blue.view(), 0.015F);
}

TEST_CASE("source passthrough followed by despill stays coherent for blue-screen input",
          "[integration][postprocess][regression]") {
    ImageBuffer green_source = load_fixture("greenscreen_reference_128.png");
    ImageBuffer blue_source = load_fixture("bluescreen_reference_128.png");
    ImageBuffer alpha = make_reference_alpha(green_source.view().width, green_source.view().height);

    ImageBuffer green_foreground = make_model_foreground(green_source.view());
    ColorUtils::State green_state;
    source_passthrough(green_source.view(), green_foreground.view(), alpha.view(), 3, 5,
                       green_state);
    despill(green_foreground.view(), 0.65F, SpillMethod::DoubleLimit);

    canonicalize_to_green_domain(blue_source.view(), ScreenColorMode::Blue);
    ImageBuffer blue_foreground = make_model_foreground(blue_source.view());

    ColorUtils::State blue_state;
    source_passthrough(blue_source.view(), blue_foreground.view(), alpha.view(), 3, 5, blue_state);
    despill(blue_foreground.view(), 0.65F, SpillMethod::DoubleLimit);
    restore_from_green_domain(blue_foreground.view(), ScreenColorMode::Blue);

    ImageBuffer expected_blue = copy_image(green_foreground.view());
    restore_from_green_domain(expected_blue.view(), ScreenColorMode::Blue);
    require_images_close(blue_foreground.view(), expected_blue.view(), 0.02F);
}
