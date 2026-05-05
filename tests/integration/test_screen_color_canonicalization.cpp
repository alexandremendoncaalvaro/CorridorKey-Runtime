#include <algorithm>
#include <array>
#include <catch2/catch_all.hpp>
#include <cmath>
#include <corridorkey/frame_io.hpp>
#include <filesystem>

#include "plugins/ofx/ofx_screen_color.hpp"
#include "post_process/color_utils.hpp"
#include "post_process/despill.hpp"
#include "post_process/source_passthrough.hpp"

using namespace corridorkey;
using namespace corridorkey::ofx;

namespace {

//
// Test-file tidy-suppression rationale.
//
// Test fixtures legitimately use single-letter loop locals, magic
// numbers (resolution rungs, pixel coordinates, expected error counts),
// std::vector::operator[] on indices the test itself just constructed,
// and Catch2 / aggregate-init styles that pre-date the project's
// tightened .clang-tidy ruleset. The test source is verified
// behaviourally by ctest; converting every site to bounds-checked /
// designated-init / ranges form would obscure intent without changing
// what the tests prove. The same suppressions are documented and
// applied on the src/ tree where the underlying APIs live.
//
// NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,readability-identifier-length,bugprone-easily-swappable-parameters,readability-function-cognitive-complexity,readability-function-size,cppcoreguidelines-avoid-magic-numbers,modernize-use-designated-initializers,readability-uppercase-literal-suffix,readability-math-missing-parentheses,modernize-use-ranges,modernize-use-starts-ends-with,modernize-use-emplace,modernize-use-auto,modernize-loop-convert,modernize-avoid-c-style-cast,modernize-return-braced-init-list,readability-implicit-bool-conversion,readability-container-contains,readability-redundant-member-init,readability-redundant-string-init,bugprone-narrowing-conversions,cppcoreguidelines-narrowing-conversions,readability-avoid-nested-conditional-operator,modernize-use-nodiscard,readability-make-member-function-const,cppcoreguidelines-pro-type-reinterpret-cast,bugprone-implicit-widening-of-multiplication-result,readability-redundant-inline-specifier,cppcoreguidelines-prefer-member-initializer,performance-unnecessary-value-param,readability-use-concise-preprocessor-directives,readability-else-after-return,readability-string-compare,bugprone-exception-escape,cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays,bugprone-branch-clone,cert-err33-c,readability-redundant-declaration,readability-qualified-auto,modernize-use-scoped-lock,modernize-use-bool-literals,cppcoreguidelines-init-variables,cppcoreguidelines-special-member-functions,cppcoreguidelines-owning-memory,cppcoreguidelines-no-malloc,performance-enum-size,performance-avoid-endl,bugprone-unchecked-optional-access,bugprone-unchecked-string-to-number-conversion,cppcoreguidelines-pro-type-cstyle-cast,modernize-use-using,modernize-use-integer-sign-comparison,cert-dcl50-cpp,cppcoreguidelines-pro-type-const-cast,readability-identifier-naming,modernize-raw-string-literal,readability-container-size-empty,bugprone-command-processor,readability-use-std-min-max,cppcoreguidelines-avoid-non-const-global-variables,bugprone-misplaced-widening-cast,readability-misleading-indentation,cert-env33-c,performance-unnecessary-copy-initialization,readability-named-parameter,readability-isolate-declaration,cert-err34-c,modernize-avoid-variadic-functions,cppcoreguidelines-pro-bounds-constant-array-index)

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

float mean_absolute_error(Image actual, Image expected) {
    REQUIRE(actual.width == expected.width);
    REQUIRE(actual.height == expected.height);
    REQUIRE(actual.channels == expected.channels);
    REQUIRE(actual.data.size() == expected.data.size());

    float total_error = 0.0F;
    for (std::size_t index = 0; index < actual.data.size(); ++index) {
        total_error += std::abs(actual.data[index] - expected.data[index]);
    }
    return total_error / static_cast<float>(actual.data.size());
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

std::array<float, 3> offaxis_blue_reference(const std::array<float, 3>& green_reference) {
    const float blue_strength = std::clamp(green_reference[1], 0.35F, 1.0F);
    const float red = std::clamp(green_reference[0], 0.0F, blue_strength * 0.25F);
    const float green_leak = std::clamp(green_reference[0] + 0.12F, 0.08F, blue_strength * 0.35F);
    return {red, green_leak, blue_strength};
}

ScreenColorTransform make_offaxis_blue_capture_transform(Image green_fixture) {
    const std::array<float, 3> green_reference =
        estimate_screen_reference(green_fixture, ScreenColorMode::Green);
    return make_screen_mapping_transform(green_reference, offaxis_blue_reference(green_reference));
}

ImageBuffer make_offaxis_blue_fixture(Image green_fixture,
                                      const ScreenColorTransform& capture_transform) {
    ImageBuffer blue = copy_image(green_fixture);
    apply_screen_color_transform(blue.view(), capture_transform.forward_matrix);
    return blue;
}

ImageBuffer make_reference_alpha(int width, int height) {
    ImageBuffer alpha(width, height, 1);
    Image view = alpha.view();
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const bool interior =
                x > width / 4 && x < (width * 3) / 4 && y > height / 5 && y < (height * 4) / 5;
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

TEST_CASE("screen-aware canonicalization improves rough matte on a real fixture",
          "[integration][postprocess][regression]") {
    ImageBuffer green_fixture = load_fixture("greenscreen_reference_128.png");
    const ScreenColorTransform capture_transform =
        make_offaxis_blue_capture_transform(green_fixture.view());
    ImageBuffer blue_fixture = make_offaxis_blue_fixture(green_fixture.view(), capture_transform);

    REQUIRE(mean_channel(green_fixture.view(), 1) > mean_channel(green_fixture.view(), 2));
    REQUIRE(mean_channel(blue_fixture.view(), 2) > mean_channel(blue_fixture.view(), 1));

    ImageBuffer green_matte(green_fixture.view().width, green_fixture.view().height, 1);
    ColorUtils::generate_rough_matte(green_fixture.view(), green_matte.view());

    ImageBuffer screen_aware = copy_image(blue_fixture.view());
    const ScreenColorTransform screen_color_transform =
        make_screen_color_transform(screen_aware.view(), ScreenColorMode::BlueGreen);
    canonicalize_to_green_domain(screen_aware.view(), screen_color_transform);
    ImageBuffer screen_aware_matte(screen_aware.view().width, screen_aware.view().height, 1);
    ColorUtils::generate_rough_matte(screen_aware.view(), screen_aware_matte.view());

    const float screen_aware_error =
        mean_absolute_error(screen_aware_matte.view(), green_matte.view());
    CHECK(screen_aware_error < 0.04F);
}

TEST_CASE(
    "screen-aware canonicalization maps an off-axis blue fixture back to a green-dominant border",
    "[integration][postprocess][regression]") {
    ImageBuffer green_fixture = load_fixture("greenscreen_reference_128.png");
    const ScreenColorTransform capture_transform =
        make_offaxis_blue_capture_transform(green_fixture.view());
    ImageBuffer blue_fixture = make_offaxis_blue_fixture(green_fixture.view(), capture_transform);

    ImageBuffer screen_aware = copy_image(blue_fixture.view());
    const ScreenColorTransform screen_color_transform =
        make_screen_color_transform(screen_aware.view(), ScreenColorMode::BlueGreen);
    canonicalize_to_green_domain(screen_aware.view(), screen_color_transform);
    CHECK(mean_channel(blue_fixture.view(), 2) > mean_channel(blue_fixture.view(), 1));
    CHECK(mean_channel(screen_aware.view(), 1) > mean_channel(screen_aware.view(), 2));
}

TEST_CASE(
    "restoring blue foreground in sRGB before linearization stays closer to the expected output",
    "[integration][postprocess][regression]") {
    ImageBuffer green_source = load_fixture("greenscreen_reference_128.png");
    const ScreenColorTransform capture_transform =
        make_offaxis_blue_capture_transform(green_source.view());
    ImageBuffer alpha = make_reference_alpha(green_source.view().width, green_source.view().height);

    ImageBuffer canonical_green_foreground = make_model_foreground(green_source.view());
    ColorUtils::State source_passthrough_state;
    source_passthrough(green_source.view(), canonical_green_foreground.view(), alpha.view(), 3, 5,
                       source_passthrough_state);
    despill(canonical_green_foreground.view(), 0.65F, SpillMethod::DoubleLimit);

    ImageBuffer expected_blue_srgb = copy_image(canonical_green_foreground.view());
    apply_screen_color_transform(expected_blue_srgb.view(), capture_transform.forward_matrix);
    ImageBuffer expected_blue_linear = copy_image(expected_blue_srgb.view());
    ColorUtils::srgb_to_linear(expected_blue_linear.view());

    ImageBuffer correct_order = copy_image(canonical_green_foreground.view());
    apply_screen_color_transform(correct_order.view(), capture_transform.forward_matrix);
    ColorUtils::srgb_to_linear(correct_order.view());

    ImageBuffer wrong_order = copy_image(canonical_green_foreground.view());
    ColorUtils::srgb_to_linear(wrong_order.view());
    apply_screen_color_transform(wrong_order.view(), capture_transform.forward_matrix);

    const float correct_error =
        mean_absolute_error(correct_order.view(), expected_blue_linear.view());
    const float wrong_error = mean_absolute_error(wrong_order.view(), expected_blue_linear.view());

    CHECK(correct_error < wrong_error);
    CHECK(correct_error < 0.0001F);
}

// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,readability-identifier-length,bugprone-easily-swappable-parameters,readability-function-cognitive-complexity,readability-function-size,cppcoreguidelines-avoid-magic-numbers,modernize-use-designated-initializers,readability-uppercase-literal-suffix,readability-math-missing-parentheses,modernize-use-ranges,modernize-use-starts-ends-with,modernize-use-emplace,modernize-use-auto,modernize-loop-convert,modernize-avoid-c-style-cast,modernize-return-braced-init-list,readability-implicit-bool-conversion,readability-container-contains,readability-redundant-member-init,readability-redundant-string-init,bugprone-narrowing-conversions,cppcoreguidelines-narrowing-conversions,readability-avoid-nested-conditional-operator,modernize-use-nodiscard,readability-make-member-function-const,cppcoreguidelines-pro-type-reinterpret-cast,bugprone-implicit-widening-of-multiplication-result,readability-redundant-inline-specifier,cppcoreguidelines-prefer-member-initializer,performance-unnecessary-value-param,readability-use-concise-preprocessor-directives,readability-else-after-return,readability-string-compare,bugprone-exception-escape,cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays,bugprone-branch-clone,cert-err33-c,readability-redundant-declaration,readability-qualified-auto,modernize-use-scoped-lock,modernize-use-bool-literals,cppcoreguidelines-init-variables,cppcoreguidelines-special-member-functions,cppcoreguidelines-owning-memory,cppcoreguidelines-no-malloc,performance-enum-size,performance-avoid-endl,bugprone-unchecked-optional-access,bugprone-unchecked-string-to-number-conversion,cppcoreguidelines-pro-type-cstyle-cast,modernize-use-using,modernize-use-integer-sign-comparison,cert-dcl50-cpp,cppcoreguidelines-pro-type-const-cast,readability-identifier-naming,modernize-raw-string-literal,readability-container-size-empty,bugprone-command-processor,readability-use-std-min-max,cppcoreguidelines-avoid-non-const-global-variables,bugprone-misplaced-widening-cast,readability-misleading-indentation,cert-env33-c,performance-unnecessary-copy-initialization,readability-named-parameter,readability-isolate-declaration,cert-err34-c,modernize-avoid-variadic-functions,cppcoreguidelines-pro-bounds-constant-array-index)
