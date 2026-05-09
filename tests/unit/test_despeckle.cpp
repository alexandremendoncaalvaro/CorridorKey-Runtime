#include <catch2/catch_all.hpp>

#include <cstdlib>

#include "post_process/despeckle.hpp"

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

TEST_CASE("despeckle removes small components via connected-component analysis",
          "[unit][despeckle]") {
    // 10x10 alpha: large connected region + isolated speck
    ImageBuffer alpha_buf(10, 10, 1);
    Image alpha = alpha_buf.view();
    DespeckleState state;

    SECTION("Basic despeckle") {
        ImageBuffer alpha_buf_basic(6, 6, 1);
        Image alpha_basic = alpha_buf_basic.view();
        std::fill(alpha_basic.data.begin(), alpha_basic.data.end(), 0.0f);

        // Add a speckle of size 4
        alpha_basic(1, 1) = 1.0f;
        alpha_basic(1, 2) = 1.0f;
        alpha_basic(2, 1) = 1.0f;
        alpha_basic(2, 2) = 1.0f;

        // Add a valid region of size 6. Isolated by ensuring x>=3, y>=4 or similar.
        alpha_basic(4, 3) = 1.0f;
        alpha_basic(4, 4) = 1.0f;
        alpha_basic(4, 5) = 1.0f;
        alpha_basic(5, 3) = 1.0f;
        alpha_basic(5, 4) = 1.0f;
        alpha_basic(5, 5) = 1.0f;

        despeckle(alpha_basic, 5, state, 0, 0);

        // Speckle (area 4) should be removed
        REQUIRE(alpha_basic(1, 1) == Catch::Approx(0.0f));
        REQUIRE(alpha_basic(1, 2) == Catch::Approx(0.0f));
        REQUIRE(alpha_basic(2, 1) == Catch::Approx(0.0f));
        REQUIRE(alpha_basic(2, 2) == Catch::Approx(0.0f));

        // Valid region (area 6) should be kept
        REQUIRE(alpha_basic(4, 3) > 0.0f);
        REQUIRE(alpha_basic(4, 4) > 0.0f);
        REQUIRE(alpha_basic(4, 5) > 0.0f);
        REQUIRE(alpha_basic(5, 3) > 0.0f);
        REQUIRE(alpha_basic(5, 4) > 0.0f);
        REQUIRE(alpha_basic(5, 5) > 0.0f);
    }

    SECTION("Small speck removed, large region kept") {
        // Fill with zeros
        for (size_t i = 0; i < alpha.data.size(); ++i) {
            alpha.data[i] = 0.0f;
        }
        // Create a large region (>= area_threshold) in top-left
        for (int y = 0; y < 5; ++y) {
            for (int x = 0; x < 5; ++x) {
                alpha(y, x) = 1.0f;
            }
        }
        // Create a small speck (< area_threshold) at bottom-right
        alpha(8, 8) = 1.0f;

        // area_threshold=5 means the speck (area=1) should be removed
        // Only the left blob should remain after despeckle(area=10)
        despeckle(alpha, 10, state, 2, 1);

        // Speck should be removed (multiplied by 0 safe zone)
        REQUIRE(alpha(8, 8) == Catch::Approx(0.0f));
        // Large region center should survive
        REQUIRE(alpha(2, 2) > 0.0f);
    }

    SECTION("All-zero image stays zero") {
        for (size_t i = 0; i < alpha.data.size(); ++i) {
            alpha.data[i] = 0.0f;
        }
        despeckle(alpha, 400, state, 25, 5);
        for (size_t i = 0; i < alpha.data.size(); ++i) {
            REQUIRE(alpha.data[i] == Catch::Approx(0.0f));
        }
    }

    SECTION("All-one image stays one") {
        for (size_t i = 0; i < alpha.data.size(); ++i) {
            alpha.data[i] = 1.0f;
        }
        // Single component covers all pixels (area=100), threshold=5
        despeckle(alpha, 5, state, 0, 0);
        for (size_t i = 0; i < alpha.data.size(); ++i) {
            REQUIRE(alpha.data[i] == Catch::Approx(1.0f));
        }
    }

    SECTION("All-foreground matte above threshold is unchanged") {
        for (size_t i = 0; i < alpha.data.size(); ++i) {
            alpha.data[i] = 0.6f + (static_cast<float>(i % 4) * 0.1f);
        }
        std::vector<float> before(alpha.data.begin(), alpha.data.end());
        despeckle(alpha, 400, state, 25, 5);
        for (size_t i = 0; i < alpha.data.size(); ++i) {
            REQUIRE(alpha.data[i] == Catch::Approx(before[i]));
        }
    }

    SECTION("All-background matte below threshold is zeroed") {
        for (size_t i = 0; i < alpha.data.size(); ++i) {
            alpha.data[i] = 0.25f;
        }
        despeckle(alpha, 400, state, 25, 5);
        for (size_t i = 0; i < alpha.data.size(); ++i) {
            REQUIRE(alpha.data[i] == Catch::Approx(0.0f));
        }
    }

    SECTION("Diagonal foreground remains 8-connected") {
        ImageBuffer diagonal_buf(4, 4, 1);
        Image diagonal = diagonal_buf.view();
        std::fill(diagonal.data.begin(), diagonal.data.end(), 0.0f);
        diagonal(0, 0) = 1.0f;
        diagonal(1, 1) = 1.0f;
        diagonal(2, 2) = 1.0f;

        despeckle(diagonal, 3, state, 0, 0);

        REQUIRE(diagonal(0, 0) == Catch::Approx(1.0f));
        REQUIRE(diagonal(1, 1) == Catch::Approx(1.0f));
        REQUIRE(diagonal(2, 2) == Catch::Approx(1.0f));
    }

    SECTION("Dilation preserves exact elliptical support before blur") {
        ImageBuffer dilated_buf(7, 7, 1);
        Image dilated = dilated_buf.view();
        std::fill(dilated.data.begin(), dilated.data.end(), 0.25f);
        dilated(3, 3) = 1.0f;

        despeckle(dilated, 1, state, 1, 0);

        for (int y = 0; y < 7; ++y) {
            for (int x = 0; x < 7; ++x) {
                const bool inside = std::abs(y - 3) <= 1 && std::abs(x - 3) <= 1;
                const float expected = inside ? (y == 3 && x == 3 ? 1.0f : 0.25f) : 0.0f;
                REQUIRE(dilated(y, x) == Catch::Approx(expected));
            }
        }
    }
}

TEST_CASE("despeckle handles edge cases", "[unit][despeckle]") {
    DespeckleState state;

    SECTION("Zero area_threshold is no-op") {
        ImageBuffer alpha_buf(3, 3, 1);
        Image alpha = alpha_buf.view();
        alpha.data[4] = 0.5f;
        float original = alpha.data[4];
        despeckle(alpha, 0, state);
        REQUIRE(alpha.data[4] == Catch::Approx(original));
    }

    SECTION("Empty image") {
        ImageBuffer empty_buf(0, 0, 1);
        despeckle(empty_buf.view(), 400, state);
    }
}

// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,readability-identifier-length,bugprone-easily-swappable-parameters,readability-function-cognitive-complexity,readability-function-size,cppcoreguidelines-avoid-magic-numbers,modernize-use-designated-initializers,readability-uppercase-literal-suffix,readability-math-missing-parentheses,modernize-use-ranges,modernize-use-starts-ends-with,modernize-use-emplace,modernize-use-auto,modernize-loop-convert,modernize-avoid-c-style-cast,modernize-return-braced-init-list,readability-implicit-bool-conversion,readability-container-contains,readability-redundant-member-init,readability-redundant-string-init,bugprone-narrowing-conversions,cppcoreguidelines-narrowing-conversions,readability-avoid-nested-conditional-operator,modernize-use-nodiscard,readability-make-member-function-const,cppcoreguidelines-pro-type-reinterpret-cast,bugprone-implicit-widening-of-multiplication-result,readability-redundant-inline-specifier,cppcoreguidelines-prefer-member-initializer,performance-unnecessary-value-param,readability-use-concise-preprocessor-directives,readability-else-after-return,readability-string-compare,bugprone-exception-escape,cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays,bugprone-branch-clone,cert-err33-c,readability-redundant-declaration,readability-qualified-auto,modernize-use-scoped-lock,modernize-use-bool-literals,cppcoreguidelines-init-variables,cppcoreguidelines-special-member-functions,cppcoreguidelines-owning-memory,cppcoreguidelines-no-malloc,performance-enum-size,performance-avoid-endl,bugprone-unchecked-optional-access,bugprone-unchecked-string-to-number-conversion,cppcoreguidelines-pro-type-cstyle-cast,modernize-use-using,modernize-use-integer-sign-comparison,cert-dcl50-cpp,cppcoreguidelines-pro-type-const-cast,readability-identifier-naming,modernize-raw-string-literal,readability-container-size-empty,bugprone-command-processor,readability-use-std-min-max,cppcoreguidelines-avoid-non-const-global-variables,bugprone-misplaced-widening-cast,readability-misleading-indentation,cert-env33-c,performance-unnecessary-copy-initialization,readability-named-parameter,readability-isolate-declaration,cert-err34-c,modernize-avoid-variadic-functions,cppcoreguidelines-pro-bounds-constant-array-index)
