#include <catch2/catch_all.hpp>

#include "post_process/alpha_edge.hpp"

using namespace corridorkey;

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

TEST_CASE("alpha_levels remaps black and white points", "[unit][alpha]") {
    ImageBuffer buf(4, 1, 1);
    Image alpha = buf.view();
    alpha(0, 0) = 0.0f;
    alpha(0, 1) = 0.25f;
    alpha(0, 2) = 0.5f;
    alpha(0, 3) = 1.0f;

    // Remap 0.25 -> 0.0 and 0.5 -> 1.0
    alpha_levels(alpha, 0.25f, 0.5f);

    REQUIRE(alpha(0, 0) == Catch::Approx(0.0f));
    REQUIRE(alpha(0, 1) == Catch::Approx(0.0f));
    REQUIRE(alpha(0, 2) == Catch::Approx(1.0f));
    REQUIRE(alpha(0, 3) == Catch::Approx(1.0f));
}

TEST_CASE("alpha_erode_dilate expands and shrinks alpha", "[unit][alpha]") {
    ImageBuffer buf(5, 5, 1);
    Image alpha = buf.view();

    // Create a 1x1 white pixel in the center
    for (int y = 0; y < 5; ++y) {
        for (int x = 0; x < 5; ++x) {
            alpha(y, x) = (x == 2 && y == 2) ? 1.0f : 0.0f;
        }
    }

    AlphaEdgeState state;

    SECTION("Dilate expands the 1x1 pixel to 3x3") {
        alpha_erode_dilate(alpha, 1.0f, state);
        REQUIRE(alpha(1, 1) == Catch::Approx(1.0f));
        REQUIRE(alpha(3, 3) == Catch::Approx(1.0f));
        // Outside the 3x3 should still be 0
        REQUIRE(alpha(0, 0) == Catch::Approx(0.0f));
        REQUIRE(alpha(4, 4) == Catch::Approx(0.0f));
    }

    SECTION("Erode removes the 1x1 pixel completely") {
        alpha_erode_dilate(alpha, -1.0f, state);
        REQUIRE(alpha(2, 2) == Catch::Approx(0.0f));
    }
}

TEST_CASE("alpha_blur smooths edges", "[unit][alpha]") {
    ImageBuffer buf(8, 1, 1);
    Image alpha = buf.view();

    for (int x = 0; x < 8; ++x) {
        alpha(0, x) = (x < 4) ? 0.0f : 1.0f;
    }

    AlphaEdgeState state;
    alpha_blur(alpha, 1.5f, state);

    // After blur, the edge at x=3 and x=4 should be softened
    REQUIRE(alpha(0, 3) > 0.0f);
    REQUIRE(alpha(0, 3) < 0.5f);
    REQUIRE(alpha(0, 4) > 0.5f);
    REQUIRE(alpha(0, 4) < 1.0f);

    // Far pixels should remain mostly unchanged
    REQUIRE(alpha(0, 0) < 0.01f);
    REQUIRE(alpha(0, 7) > 0.99f);
}

TEST_CASE("alpha_gamma_correct applies non-linear curve via LUT", "[unit][alpha]") {
    ImageBuffer buf(4, 1, 1);
    Image alpha = buf.view();
    alpha(0, 0) = 0.0f;
    alpha(0, 1) = 0.25f;
    alpha(0, 2) = 0.5f;
    alpha(0, 3) = 1.0f;

    SECTION("Gamma 1.0 is identity") {
        alpha_gamma_correct(alpha, 1.0f);
        REQUIRE(alpha(0, 0) == Catch::Approx(0.0f));
        REQUIRE(alpha(0, 1) == Catch::Approx(0.25f).margin(0.002f));
        REQUIRE(alpha(0, 2) == Catch::Approx(0.5f).margin(0.002f));
        REQUIRE(alpha(0, 3) == Catch::Approx(1.0f));
    }

    SECTION("Gamma > 1 brightens midtones (recovers semi-transparent areas)") {
        alpha_gamma_correct(alpha, 2.0f);
        REQUIRE(alpha(0, 0) == Catch::Approx(0.0f));
        REQUIRE(alpha(0, 1) > 0.25f);
        REQUIRE(alpha(0, 2) > 0.5f);
        REQUIRE(alpha(0, 3) == Catch::Approx(1.0f));
    }

    SECTION("Gamma < 1 darkens midtones (tightens matte)") {
        alpha_gamma_correct(alpha, 0.5f);
        REQUIRE(alpha(0, 0) == Catch::Approx(0.0f));
        REQUIRE(alpha(0, 1) < 0.25f);
        REQUIRE(alpha(0, 2) < 0.5f);
        REQUIRE(alpha(0, 3) == Catch::Approx(1.0f));
    }

    SECTION("Fully opaque and transparent pixels are unchanged") {
        alpha_gamma_correct(alpha, 3.0f);
        REQUIRE(alpha(0, 0) == Catch::Approx(0.0f));
        REQUIRE(alpha(0, 3) == Catch::Approx(1.0f));
    }
}

TEST_CASE("alpha_gamma_correct handles edge cases", "[unit][alpha]") {
    ImageBuffer buf(1, 1, 1);
    Image alpha = buf.view();
    alpha(0, 0) = 0.5f;

    SECTION("Zero gamma is treated as no-op") {
        alpha_gamma_correct(alpha, 0.0f);
        REQUIRE(alpha(0, 0) == Catch::Approx(0.5f));
    }

    SECTION("Negative gamma is treated as no-op") {
        alpha_gamma_correct(alpha, -1.0f);
        REQUIRE(alpha(0, 0) == Catch::Approx(0.5f));
    }
}

TEST_CASE("alpha operations handle empty images gracefully", "[unit][alpha]") {
    ImageBuffer buf;
    Image alpha = buf.view();
    AlphaEdgeState state;

    // Should not crash
    alpha_levels(alpha, 0.0f, 1.0f);
    alpha_gamma_correct(alpha, 2.0f);
    alpha_erode_dilate(alpha, 1.0f, state);
    alpha_blur(alpha, 1.0f, state);
}

// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,readability-identifier-length,bugprone-easily-swappable-parameters,readability-function-cognitive-complexity,readability-function-size,cppcoreguidelines-avoid-magic-numbers,modernize-use-designated-initializers,readability-uppercase-literal-suffix,readability-math-missing-parentheses,modernize-use-ranges,modernize-use-starts-ends-with,modernize-use-emplace,modernize-use-auto,modernize-loop-convert,modernize-avoid-c-style-cast,modernize-return-braced-init-list,readability-implicit-bool-conversion,readability-container-contains,readability-redundant-member-init,readability-redundant-string-init,bugprone-narrowing-conversions,cppcoreguidelines-narrowing-conversions,readability-avoid-nested-conditional-operator,modernize-use-nodiscard,readability-make-member-function-const,cppcoreguidelines-pro-type-reinterpret-cast,bugprone-implicit-widening-of-multiplication-result,readability-redundant-inline-specifier,cppcoreguidelines-prefer-member-initializer,performance-unnecessary-value-param,readability-use-concise-preprocessor-directives,readability-else-after-return,readability-string-compare,bugprone-exception-escape,cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays,bugprone-branch-clone,cert-err33-c,readability-redundant-declaration,readability-qualified-auto,modernize-use-scoped-lock,modernize-use-bool-literals,cppcoreguidelines-init-variables,cppcoreguidelines-special-member-functions,cppcoreguidelines-owning-memory,cppcoreguidelines-no-malloc,performance-enum-size,performance-avoid-endl,bugprone-unchecked-optional-access,bugprone-unchecked-string-to-number-conversion,cppcoreguidelines-pro-type-cstyle-cast,modernize-use-using,modernize-use-integer-sign-comparison,cert-dcl50-cpp,cppcoreguidelines-pro-type-const-cast,readability-identifier-naming,modernize-raw-string-literal,readability-container-size-empty,bugprone-command-processor,readability-use-std-min-max,cppcoreguidelines-avoid-non-const-global-variables,bugprone-misplaced-widening-cast,readability-misleading-indentation,cert-env33-c,performance-unnecessary-copy-initialization,readability-named-parameter,readability-isolate-declaration,cert-err34-c,modernize-avoid-variadic-functions,cppcoreguidelines-pro-bounds-constant-array-index)
