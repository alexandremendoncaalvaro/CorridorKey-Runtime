#include <catch2/catch_all.hpp>

#include "post_process/source_passthrough.hpp"

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

TEST_CASE("source_passthrough is no-op for transparent regions", "[unit][passthrough]") {
    // All alpha = 0.0 -> model foreground should be unchanged
    ImageBuffer src_buf(8, 8, 3);
    ImageBuffer fg_buf(8, 8, 3);
    ImageBuffer alpha_buf(8, 8, 1);

    Image src = src_buf.view();
    Image fg = fg_buf.view();
    Image alpha = alpha_buf.view();

    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) {
            src(y, x, 0) = 1.0F;
            src(y, x, 1) = 0.0F;
            src(y, x, 2) = 0.0F;
            fg(y, x, 0) = 0.0F;
            fg(y, x, 1) = 1.0F;
            fg(y, x, 2) = 0.0F;
            alpha(y, x) = 0.0F;
        }
    }

    ColorUtils::State state;
    source_passthrough(src, fg, alpha, 0, 0, state);

    // Foreground should remain green (source not blended in)
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) {
            CHECK(fg(y, x, 0) == Catch::Approx(0.0F));
            CHECK(fg(y, x, 1) == Catch::Approx(1.0F));
            CHECK(fg(y, x, 2) == Catch::Approx(0.0F));
        }
    }
}

TEST_CASE("source_passthrough blends source in fully opaque regions", "[unit][passthrough]") {
    // Large image with all alpha = 1.0, zero erode/blur -> output should equal source
    ImageBuffer src_buf(32, 32, 3);
    ImageBuffer fg_buf(32, 32, 3);
    ImageBuffer alpha_buf(32, 32, 1);

    Image src = src_buf.view();
    Image fg = fg_buf.view();
    Image alpha = alpha_buf.view();

    for (int y = 0; y < 32; ++y) {
        for (int x = 0; x < 32; ++x) {
            src(y, x, 0) = 0.8F;
            src(y, x, 1) = 0.2F;
            src(y, x, 2) = 0.1F;
            fg(y, x, 0) = 0.3F;
            fg(y, x, 1) = 0.5F;
            fg(y, x, 2) = 0.4F;
            alpha(y, x) = 1.0F;
        }
    }

    ColorUtils::State state;
    source_passthrough(src, fg, alpha, 0, 0, state);

    // With zero erode and zero blur, all pixels above threshold should be source
    for (int y = 0; y < 32; ++y) {
        for (int x = 0; x < 32; ++x) {
            CHECK(fg(y, x, 0) == Catch::Approx(0.8F));
            CHECK(fg(y, x, 1) == Catch::Approx(0.2F));
            CHECK(fg(y, x, 2) == Catch::Approx(0.1F));
        }
    }
}

TEST_CASE("source_passthrough respects threshold at 0.95", "[unit][passthrough]") {
    ImageBuffer src_buf(4, 4, 3);
    ImageBuffer fg_buf(4, 4, 3);
    ImageBuffer alpha_buf(4, 4, 1);

    Image src = src_buf.view();
    Image fg = fg_buf.view();
    Image alpha = alpha_buf.view();

    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            src(y, x, 0) = 1.0F;
            src(y, x, 1) = 1.0F;
            src(y, x, 2) = 1.0F;
            fg(y, x, 0) = 0.0F;
            fg(y, x, 1) = 0.0F;
            fg(y, x, 2) = 0.0F;
            alpha(y, x) = 0.94F;  // Just below threshold
        }
    }

    ColorUtils::State state;
    source_passthrough(src, fg, alpha, 0, 0, state);

    // Below threshold -> model fg unchanged (black)
    CHECK(fg(0, 0, 0) == Catch::Approx(0.0F));

    // Now set above threshold
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            fg(y, x, 0) = 0.0F;
            fg(y, x, 1) = 0.0F;
            fg(y, x, 2) = 0.0F;
            alpha(y, x) = 0.96F;  // Above threshold
        }
    }

    source_passthrough(src, fg, alpha, 0, 0, state);

    // Above threshold -> source (white)
    CHECK(fg(0, 0, 0) == Catch::Approx(1.0F));
}

TEST_CASE("source_passthrough handles empty images", "[unit][passthrough]") {
    ImageBuffer empty;
    ImageBuffer fg_buf(4, 4, 3);
    ImageBuffer alpha_buf(4, 4, 1);

    ColorUtils::State state;
    // Should not crash
    source_passthrough(empty.view(), fg_buf.view(), alpha_buf.view(), 3, 7, state);
    source_passthrough(fg_buf.view(), empty.view(), alpha_buf.view(), 3, 7, state);
    source_passthrough(fg_buf.view(), fg_buf.view(), empty.view(), 3, 7, state);
}

TEST_CASE("source_passthrough with erosion shrinks interior", "[unit][passthrough]") {
    // 32x32 image with opaque center circle (r=12) surrounded by transparent background
    ImageBuffer src_buf(32, 32, 3);
    ImageBuffer fg_buf(32, 32, 3);
    ImageBuffer alpha_buf(32, 32, 1);

    Image src = src_buf.view();
    Image fg = fg_buf.view();
    Image alpha = alpha_buf.view();

    for (int y = 0; y < 32; ++y) {
        for (int x = 0; x < 32; ++x) {
            src(y, x, 0) = 1.0F;
            src(y, x, 1) = 0.0F;
            src(y, x, 2) = 0.0F;
            fg(y, x, 0) = 0.0F;
            fg(y, x, 1) = 0.0F;
            fg(y, x, 2) = 1.0F;

            float dy = static_cast<float>(y) - 16.0F;
            float dx = static_cast<float>(x) - 16.0F;
            alpha(y, x) = (dy * dy + dx * dx < 12.0F * 12.0F) ? 1.0F : 0.0F;
        }
    }

    ColorUtils::State state;
    source_passthrough(src, fg, alpha, 3, 0, state);

    // Center pixel (16,16) should be source (red) -- well inside the circle
    CHECK(fg(16, 16, 0) == Catch::Approx(1.0F));
    CHECK(fg(16, 16, 2) == Catch::Approx(0.0F));

    // Corner pixel (0,0) is transparent -> model fg (blue) unchanged
    CHECK(fg(0, 0, 0) == Catch::Approx(0.0F));
    CHECK(fg(0, 0, 2) == Catch::Approx(1.0F));

    // Edge of circle (~radius 12, after erode_px=3 the interior shrinks by ~3px)
    // Pixel at (16, 27) is at distance 11 from center -- near the edge
    // After erosion, should be eroded away -> model fg (blue)
    CHECK(fg(16, 27, 2) == Catch::Approx(1.0F));
}

TEST_CASE("source_passthrough preserves behavior when cached erosion footprint changes",
          "[unit][passthrough][regression]") {
    ImageBuffer src_buf(32, 32, 3);
    ImageBuffer fg_buf(32, 32, 3);
    ImageBuffer alpha_buf(32, 32, 1);

    Image src = src_buf.view();
    Image fg = fg_buf.view();
    Image alpha = alpha_buf.view();

    for (int y = 0; y < 32; ++y) {
        for (int x = 0; x < 32; ++x) {
            src(y, x, 0) = 0.9F;
            src(y, x, 1) = 0.1F;
            src(y, x, 2) = 0.0F;
            fg(y, x, 0) = 0.0F;
            fg(y, x, 1) = 0.0F;
            fg(y, x, 2) = 1.0F;

            float dy = static_cast<float>(y) - 16.0F;
            float dx = static_cast<float>(x) - 16.0F;
            alpha(y, x) = (dy * dy + dx * dx < 13.0F * 13.0F) ? 1.0F : 0.0F;
        }
    }

    ColorUtils::State state;
    source_passthrough(src, fg, alpha, 3, 0, state);
    CHECK(fg(16, 16, 0) == Catch::Approx(0.9F));
    CHECK(fg(16, 28, 2) == Catch::Approx(1.0F));

    for (int y = 0; y < 32; ++y) {
        for (int x = 0; x < 32; ++x) {
            fg(y, x, 0) = 0.0F;
            fg(y, x, 1) = 0.0F;
            fg(y, x, 2) = 1.0F;
        }
    }

    source_passthrough(src, fg, alpha, 5, 0, state);
    CHECK(fg(16, 16, 0) == Catch::Approx(0.9F));
    CHECK(fg(16, 27, 2) == Catch::Approx(1.0F));
}

// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,readability-identifier-length,bugprone-easily-swappable-parameters,readability-function-cognitive-complexity,readability-function-size,cppcoreguidelines-avoid-magic-numbers,modernize-use-designated-initializers,readability-uppercase-literal-suffix,readability-math-missing-parentheses,modernize-use-ranges,modernize-use-starts-ends-with,modernize-use-emplace,modernize-use-auto,modernize-loop-convert,modernize-avoid-c-style-cast,modernize-return-braced-init-list,readability-implicit-bool-conversion,readability-container-contains,readability-redundant-member-init,readability-redundant-string-init,bugprone-narrowing-conversions,cppcoreguidelines-narrowing-conversions,readability-avoid-nested-conditional-operator,modernize-use-nodiscard,readability-make-member-function-const,cppcoreguidelines-pro-type-reinterpret-cast,bugprone-implicit-widening-of-multiplication-result,readability-redundant-inline-specifier,cppcoreguidelines-prefer-member-initializer,performance-unnecessary-value-param,readability-use-concise-preprocessor-directives,readability-else-after-return,readability-string-compare,bugprone-exception-escape,cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays,bugprone-branch-clone,cert-err33-c,readability-redundant-declaration,readability-qualified-auto,modernize-use-scoped-lock,modernize-use-bool-literals,cppcoreguidelines-init-variables,cppcoreguidelines-special-member-functions,cppcoreguidelines-owning-memory,cppcoreguidelines-no-malloc,performance-enum-size,performance-avoid-endl,bugprone-unchecked-optional-access,bugprone-unchecked-string-to-number-conversion,cppcoreguidelines-pro-type-cstyle-cast,modernize-use-using,modernize-use-integer-sign-comparison,cert-dcl50-cpp,cppcoreguidelines-pro-type-const-cast,readability-identifier-naming,modernize-raw-string-literal,readability-container-size-empty,bugprone-command-processor,readability-use-std-min-max,cppcoreguidelines-avoid-non-const-global-variables,bugprone-misplaced-widening-cast,readability-misleading-indentation,cert-env33-c,performance-unnecessary-copy-initialization,readability-named-parameter,readability-isolate-declaration,cert-err34-c,modernize-avoid-variadic-functions,cppcoreguidelines-pro-bounds-constant-array-index)
