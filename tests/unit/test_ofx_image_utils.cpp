#include <array>
#include <catch2/catch_all.hpp>

#include "plugins/ofx/ofx_image_utils.hpp"

using namespace corridorkey;
using namespace corridorkey::ofx;

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

// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,readability-identifier-length,bugprone-easily-swappable-parameters,readability-function-cognitive-complexity,readability-function-size,cppcoreguidelines-avoid-magic-numbers,modernize-use-designated-initializers,readability-uppercase-literal-suffix,readability-math-missing-parentheses,modernize-use-ranges,modernize-use-starts-ends-with,modernize-use-emplace,modernize-use-auto,modernize-loop-convert,modernize-avoid-c-style-cast,modernize-return-braced-init-list,readability-implicit-bool-conversion,readability-container-contains,readability-redundant-member-init,readability-redundant-string-init,bugprone-narrowing-conversions,cppcoreguidelines-narrowing-conversions,readability-avoid-nested-conditional-operator,modernize-use-nodiscard,readability-make-member-function-const,cppcoreguidelines-pro-type-reinterpret-cast,bugprone-implicit-widening-of-multiplication-result,readability-redundant-inline-specifier,cppcoreguidelines-prefer-member-initializer,performance-unnecessary-value-param,readability-use-concise-preprocessor-directives,readability-else-after-return,readability-string-compare,bugprone-exception-escape,cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays,bugprone-branch-clone,cert-err33-c,readability-redundant-declaration,readability-qualified-auto,modernize-use-scoped-lock,modernize-use-bool-literals,cppcoreguidelines-init-variables,cppcoreguidelines-special-member-functions,cppcoreguidelines-owning-memory,cppcoreguidelines-no-malloc,performance-enum-size,performance-avoid-endl,bugprone-unchecked-optional-access,bugprone-unchecked-string-to-number-conversion,cppcoreguidelines-pro-type-cstyle-cast,modernize-use-using,modernize-use-integer-sign-comparison,cert-dcl50-cpp,cppcoreguidelines-pro-type-const-cast,readability-identifier-naming,modernize-raw-string-literal,readability-container-size-empty,bugprone-command-processor,readability-use-std-min-max,cppcoreguidelines-avoid-non-const-global-variables,bugprone-misplaced-widening-cast,readability-misleading-indentation,cert-env33-c,performance-unnecessary-copy-initialization,readability-named-parameter,readability-isolate-declaration,cert-err34-c,modernize-avoid-variadic-functions,cppcoreguidelines-pro-bounds-constant-array-index)
