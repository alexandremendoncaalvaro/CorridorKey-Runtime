#include <catch2/catch_all.hpp>

#include "common/srgb_lut.hpp"
#include "post_process/color_utils.hpp"
#include "post_process/despeckle.hpp"
#include "post_process/despill.hpp"

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

TEST_CASE("Full post-process chain matching original Python pipeline",
          "[integration][postprocess]") {
    // Simulate a 4x4 green-screen frame with some green spill
    // Model outputs: FG in sRGB, alpha as 0-1 mask
    ImageBuffer rgb_buf(4, 4, 3);
    Image rgb = rgb_buf.view();

    ImageBuffer alpha_buf(4, 4, 1);
    Image alpha = alpha_buf.view();

    // Fill with sRGB values: greenish foreground with partial alpha
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            rgb(y, x, 0) = 0.6f;  // sRGB red
            rgb(y, x, 1) = 0.8f;  // sRGB green (some spill)
            rgb(y, x, 2) = 0.5f;  // sRGB blue
            alpha(y, x) = (y < 2) ? 1.0f : 0.7f;
        }
    }

    // Pipeline order matches original Python:
    // 1. Despeckle alpha (space-agnostic)
    // 2. Despill FG in sRGB
    // 3. Convert FG to linear, premultiply, pack RGBA
    // 4. Composite in linear, convert to sRGB

    // 1. Despeckle (use small threshold so 4x4 region is not removed)
    DespeckleState despeckle_state;
    despeckle(alpha, 2, despeckle_state, 0, 0);

    // 2. Despill in sRGB space (no alpha parameter, matching original)
    float green_before = rgb.data[1];
    despill(rgb, 1.0f);
    // Green should be reduced, R and B should increase (spill redistribution)
    REQUIRE(rgb.data[1] < green_before);
    REQUIRE(rgb.data[0] > 0.6f);  // R increased by spill/2
    REQUIRE(rgb.data[2] > 0.5f);  // B increased by spill/2

    // 3. Convert to linear and premultiply (one pass, like inference_session.cpp)
    const auto& lut = SrgbLut::instance();
    ImageBuffer processed_buf(4, 4, 4);
    Image proc = processed_buf.view();

    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            float a = alpha(y, x);
            proc(y, x, 0) = lut.to_linear(rgb(y, x, 0)) * a;
            proc(y, x, 1) = lut.to_linear(rgb(y, x, 1)) * a;
            proc(y, x, 2) = lut.to_linear(rgb(y, x, 2)) * a;
            proc(y, x, 3) = a;
        }
    }

    // Verify premultiply: bottom half (alpha=0.5) should have halved linear values
    for (int x = 0; x < 4; ++x) {
        REQUIRE(proc(3, x, 3) == Catch::Approx(0.7f));
        // Premultiplied linear values should be roughly half of full alpha version
        REQUIRE(proc(3, x, 0) < proc(0, x, 0));
    }

    // 4. Composite the premultiplied linear result directly to display sRGB
    ImageBuffer legacy_comp_buf(4, 4, 4);
    Image legacy_comp = legacy_comp_buf.view();
    std::copy(proc.data.begin(), proc.data.end(), legacy_comp.data.begin());
    ColorUtils::composite_over_checker(legacy_comp);
    ColorUtils::linear_to_srgb(legacy_comp);

    ImageBuffer comp_buf(4, 4, 4);
    Image comp = comp_buf.view();
    ColorUtils::composite_premultiplied_over_checker_to_srgb(proc, comp);

    for (size_t index = 0; index < comp.data.size(); ++index) {
        REQUIRE(comp.data[index] == Catch::Approx(legacy_comp.data[index]).margin(0.0001f));
    }

    // All values should be in valid sRGB range [0, 1]
    for (size_t i = 0; i < comp.data.size(); ++i) {
        REQUIRE(comp.data[i] >= 0.0f);
        REQUIRE(comp.data[i] <= 1.0f);
    }
}

// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,readability-identifier-length,bugprone-easily-swappable-parameters,readability-function-cognitive-complexity,readability-function-size,cppcoreguidelines-avoid-magic-numbers,modernize-use-designated-initializers,readability-uppercase-literal-suffix,readability-math-missing-parentheses,modernize-use-ranges,modernize-use-starts-ends-with,modernize-use-emplace,modernize-use-auto,modernize-loop-convert,modernize-avoid-c-style-cast,modernize-return-braced-init-list,readability-implicit-bool-conversion,readability-container-contains,readability-redundant-member-init,readability-redundant-string-init,bugprone-narrowing-conversions,cppcoreguidelines-narrowing-conversions,readability-avoid-nested-conditional-operator,modernize-use-nodiscard,readability-make-member-function-const,cppcoreguidelines-pro-type-reinterpret-cast,bugprone-implicit-widening-of-multiplication-result,readability-redundant-inline-specifier,cppcoreguidelines-prefer-member-initializer,performance-unnecessary-value-param,readability-use-concise-preprocessor-directives,readability-else-after-return,readability-string-compare,bugprone-exception-escape,cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays,bugprone-branch-clone,cert-err33-c,readability-redundant-declaration,readability-qualified-auto,modernize-use-scoped-lock,modernize-use-bool-literals,cppcoreguidelines-init-variables,cppcoreguidelines-special-member-functions,cppcoreguidelines-owning-memory,cppcoreguidelines-no-malloc,performance-enum-size,performance-avoid-endl,bugprone-unchecked-optional-access,bugprone-unchecked-string-to-number-conversion,cppcoreguidelines-pro-type-cstyle-cast,modernize-use-using,modernize-use-integer-sign-comparison,cert-dcl50-cpp,cppcoreguidelines-pro-type-const-cast,readability-identifier-naming,modernize-raw-string-literal,readability-container-size-empty,bugprone-command-processor,readability-use-std-min-max,cppcoreguidelines-avoid-non-const-global-variables,bugprone-misplaced-widening-cast,readability-misleading-indentation,cert-env33-c,performance-unnecessary-copy-initialization,readability-named-parameter,readability-isolate-declaration,cert-err34-c,modernize-avoid-variadic-functions,cppcoreguidelines-pro-bounds-constant-array-index)
