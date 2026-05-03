#include <algorithm>
#include <array>
#include <catch2/catch_all.hpp>
#include <cmath>

#include "plugins/ofx/ofx_screen_color.hpp"
#include "post_process/color_utils.hpp"
#include "post_process/despill.hpp"

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

ImageBuffer make_green_domain_sample() {
    ImageBuffer image(2, 2, 3);
    Image view = image.view();
    view(0, 0, 0) = 0.14F;
    view(0, 0, 1) = 0.83F;
    view(0, 0, 2) = 0.10F;

    view(0, 1, 0) = 0.68F;
    view(0, 1, 1) = 0.24F;
    view(0, 1, 2) = 0.16F;

    view(1, 0, 0) = 0.18F;
    view(1, 0, 1) = 0.79F;
    view(1, 0, 2) = 0.14F;

    view(1, 1, 0) = 0.55F;
    view(1, 1, 1) = 0.36F;
    view(1, 1, 2) = 0.29F;
    return image;
}

ImageBuffer make_anchor_probe() {
    ImageBuffer image(3, 1, 3);
    Image view = image.view();
    view(0, 0, 0) = 1.0F;
    view(0, 0, 1) = 0.0F;
    view(0, 0, 2) = 0.0F;

    view(0, 1, 0) = 1.0F;
    view(0, 1, 1) = 1.0F;
    view(0, 1, 2) = 1.0F;

    view(0, 2, 0) = 0.12F;
    view(0, 2, 1) = 0.24F;
    view(0, 2, 2) = 0.84F;
    return image;
}

std::array<float, 3> synthetic_green_screen_reference() {
    return {0.10F, 0.82F, 0.10F};
}

std::array<float, 3> synthetic_offaxis_blue_reference() {
    return {0.12F, 0.24F, 0.84F};
}

ScreenColorTransform make_offaxis_blue_capture_transform() {
    return make_screen_mapping_transform(synthetic_green_screen_reference(),
                                         synthetic_offaxis_blue_reference());
}

ImageBuffer make_offaxis_blue_capture(Image green_domain_source) {
    ImageBuffer blue = copy_image(green_domain_source);
    const ScreenColorTransform capture_transform = make_offaxis_blue_capture_transform();
    apply_screen_color_transform(blue.view(), capture_transform.forward_matrix);
    return blue;
}

}  // namespace

TEST_CASE("screen color helpers preserve anchors and roundtrip blue input",
          "[unit][ofx][regression]") {
    SECTION("choice mapping defaults to green") {
        CHECK(screen_color_mode_from_choice(kScreenColorGreen) == ScreenColorMode::Green);
        CHECK(screen_color_mode_from_choice(kScreenColorBlue) == ScreenColorMode::Blue);
        CHECK(screen_color_mode_from_choice(99) == ScreenColorMode::Green);
    }

    SECTION("green mode stays unchanged") {
        ImageBuffer green = make_green_domain_sample();
        ImageBuffer original = copy_image(green.view());
        const ScreenColorTransform transform =
            make_screen_color_transform(green.view(), ScreenColorMode::Green);

        CHECK(transform.is_identity);
        canonicalize_to_green_domain(green.view(), transform);
        restore_from_green_domain(green.view(), transform);
        require_images_equal(green.view(), original.view());
    }

    SECTION("blue mode keeps white and red stable while mapping screen colors closer to green") {
        ImageBuffer blue = make_anchor_probe();
        ImageBuffer original = copy_image(blue.view());
        ImageBuffer swapped = copy_image(blue.view());
        swap_green_blue_channels(swapped.view());

        const ScreenColorTransform transform =
            make_screen_color_transform(blue.view(), ScreenColorMode::Blue);
        const std::array<float, 3> target =
            canonical_green_reference_from_blue(synthetic_offaxis_blue_reference());

        canonicalize_to_green_domain(blue.view(), transform);

        CHECK(blue.view()(0, 0, 0) == Catch::Approx(1.0F).margin(0.0001F));
        CHECK(blue.view()(0, 0, 1) == Catch::Approx(0.0F).margin(0.0001F));
        CHECK(blue.view()(0, 0, 2) == Catch::Approx(0.0F).margin(0.0001F));
        CHECK(blue.view()(0, 1, 0) == Catch::Approx(1.0F).margin(0.0001F));
        CHECK(blue.view()(0, 1, 1) == Catch::Approx(1.0F).margin(0.0001F));
        CHECK(blue.view()(0, 1, 2) == Catch::Approx(1.0F).margin(0.0001F));
        CHECK(blue.view()(0, 2, 1) > blue.view()(0, 2, 2));

        const float transform_error = std::abs(blue.view()(0, 2, 0) - target[0]) +
                                      std::abs(blue.view()(0, 2, 1) - target[1]) +
                                      std::abs(blue.view()(0, 2, 2) - target[2]);
        const float swap_error = std::abs(swapped.view()(0, 2, 0) - target[0]) +
                                 std::abs(swapped.view()(0, 2, 1) - target[1]) +
                                 std::abs(swapped.view()(0, 2, 2) - target[2]);
        CHECK(transform_error < swap_error);

        restore_from_green_domain(blue.view(), transform);
        require_images_equal(blue.view(), original.view(), 0.0002F);
    }
}

TEST_CASE("screen-aware canonicalization improves rough matte on off-axis blue input",
          "[unit][ofx][regression]") {
    ImageBuffer green_source = make_green_domain_sample();
    ImageBuffer green_matte(2, 2, 1);
    ColorUtils::generate_rough_matte(green_source.view(), green_matte.view());

    ImageBuffer blue_source = make_offaxis_blue_capture(green_source.view());

    ImageBuffer screen_aware = copy_image(blue_source.view());
    const ScreenColorTransform transform =
        make_screen_color_transform(screen_aware.view(), ScreenColorMode::Blue);
    canonicalize_to_green_domain(screen_aware.view(), transform);
    ImageBuffer screen_aware_matte(2, 2, 1);
    ColorUtils::generate_rough_matte(screen_aware.view(), screen_aware_matte.view());

    ImageBuffer swapped = copy_image(blue_source.view());
    swap_green_blue_channels(swapped.view());
    ImageBuffer swapped_matte(2, 2, 1);
    ColorUtils::generate_rough_matte(swapped.view(), swapped_matte.view());

    const float screen_aware_error =
        mean_absolute_error(screen_aware_matte.view(), green_matte.view());
    const float swapped_error = mean_absolute_error(swapped_matte.view(), green_matte.view());

    CHECK(screen_aware_error <= swapped_error);
    CHECK(screen_aware_error < 0.05F);
}

TEST_CASE("screen-aware canonicalization improves blue-screen despill coherence",
          "[unit][ofx][regression]") {
    constexpr float kStrength = 0.7F;
    ImageBuffer green_source = make_green_domain_sample();
    ImageBuffer expected_green = copy_image(green_source.view());
    despill(expected_green.view(), kStrength, SpillMethod::Average);

    ImageBuffer expected_blue = copy_image(expected_green.view());
    const ScreenColorTransform capture_transform = make_offaxis_blue_capture_transform();
    apply_screen_color_transform(expected_blue.view(), capture_transform.forward_matrix);

    ImageBuffer blue_source = make_offaxis_blue_capture(green_source.view());

    ImageBuffer screen_aware = copy_image(blue_source.view());
    const ScreenColorTransform screen_color_transform =
        make_screen_color_transform(screen_aware.view(), ScreenColorMode::Blue);
    canonicalize_to_green_domain(screen_aware.view(), screen_color_transform);
    despill(screen_aware.view(), kStrength, SpillMethod::Average);
    restore_from_green_domain(screen_aware.view(), screen_color_transform);

    ImageBuffer swapped = copy_image(blue_source.view());
    swap_green_blue_channels(swapped.view());
    despill(swapped.view(), kStrength, SpillMethod::Average);
    swap_green_blue_channels(swapped.view());

    const float screen_aware_error = mean_absolute_error(screen_aware.view(), expected_blue.view());
    const float swapped_error = mean_absolute_error(swapped.view(), expected_blue.view());

    CHECK(screen_aware_error < swapped_error);
    CHECK(screen_aware_error < 0.03F);
}

// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,readability-identifier-length,bugprone-easily-swappable-parameters,readability-function-cognitive-complexity,readability-function-size,cppcoreguidelines-avoid-magic-numbers,modernize-use-designated-initializers,readability-uppercase-literal-suffix,readability-math-missing-parentheses,modernize-use-ranges,modernize-use-starts-ends-with,modernize-use-emplace,modernize-use-auto,modernize-loop-convert,modernize-avoid-c-style-cast,modernize-return-braced-init-list,readability-implicit-bool-conversion,readability-container-contains,readability-redundant-member-init,readability-redundant-string-init,bugprone-narrowing-conversions,cppcoreguidelines-narrowing-conversions,readability-avoid-nested-conditional-operator,modernize-use-nodiscard,readability-make-member-function-const,cppcoreguidelines-pro-type-reinterpret-cast,bugprone-implicit-widening-of-multiplication-result,readability-redundant-inline-specifier,cppcoreguidelines-prefer-member-initializer,performance-unnecessary-value-param,readability-use-concise-preprocessor-directives,readability-else-after-return,readability-string-compare,bugprone-exception-escape,cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays,bugprone-branch-clone,cert-err33-c,readability-redundant-declaration,readability-qualified-auto,modernize-use-scoped-lock,modernize-use-bool-literals,cppcoreguidelines-init-variables,cppcoreguidelines-special-member-functions,cppcoreguidelines-owning-memory,cppcoreguidelines-no-malloc,performance-enum-size,performance-avoid-endl,bugprone-unchecked-optional-access,bugprone-unchecked-string-to-number-conversion,cppcoreguidelines-pro-type-cstyle-cast,modernize-use-using,modernize-use-integer-sign-comparison,cert-dcl50-cpp,cppcoreguidelines-pro-type-const-cast,readability-identifier-naming,modernize-raw-string-literal,readability-container-size-empty,bugprone-command-processor,readability-use-std-min-max,cppcoreguidelines-avoid-non-const-global-variables,bugprone-misplaced-widening-cast,readability-misleading-indentation,cert-env33-c,performance-unnecessary-copy-initialization,readability-named-parameter,readability-isolate-declaration,cert-err34-c,modernize-avoid-variadic-functions,cppcoreguidelines-pro-bounds-constant-array-index)
