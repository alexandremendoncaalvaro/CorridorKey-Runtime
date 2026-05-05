#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <vector>

#include "core/gpu_resize.hpp"
#include "post_process/color_utils.hpp"

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

TEST_CASE("GpuResizer Availability", "[unit][core]") {
    core::GpuResizer resizer;
    bool avail = resizer.available();
    SUCCEED("Queried availability: " + std::to_string(avail));
}

TEST_CASE("GpuResizer Correctness vs CPU reference", "[unit][core]") {
    core::GpuResizer resizer;
    if (!resizer.available()) {
        SKIP("GPU resize not available on this host");
    }

    const int src_w = 64;
    const int src_h = 64;
    const int dst_w = 128;
    const int dst_h = 128;

    std::vector<float> alpha_src(src_w * src_h, 0.5f);
    std::vector<float> fg_src(src_w * src_h * 3, 0.25f);

    // Add some gradient so we can verify resize
    for (int y = 0; y < src_h; ++y) {
        for (int x = 0; x < src_w; ++x) {
            float val = static_cast<float>(x) / src_w;
            alpha_src[y * src_w + x] = val;
            fg_src[y * src_w + x] = val;                         // R
            fg_src[src_w * src_h + y * src_w + x] = 1.0f - val;  // G
            fg_src[2 * src_w * src_h + y * src_w + x] = 0.5f;    // B
        }
    }

    ImageBuffer gpu_alpha(dst_w, dst_h, 1);
    ImageBuffer gpu_fg(dst_w, dst_h, 3);
    ImageBuffer cpu_alpha(dst_w, dst_h, 1);
    ImageBuffer cpu_fg(dst_w, dst_h, 3);

    auto res = resizer.resize_planar_outputs(alpha_src.data(), fg_src.data(), src_w, src_h,
                                             gpu_alpha.view(), gpu_fg.view());

    REQUIRE(res.has_value());

    // Compute CPU reference
    ColorUtils::State state;
    ColorUtils::resize_alpha_fg_from_planar_into(alpha_src.data(), fg_src.data(), src_w, src_h,
                                                 cpu_alpha.view(), cpu_fg.view());

    // Compare
    double max_diff = 0.0;
    for (size_t i = 0; i < gpu_alpha.view().data.size(); ++i) {
        double diff = std::abs(gpu_alpha.view().data[i] - cpu_alpha.view().data[i]);
        if (diff > max_diff) max_diff = diff;
    }
    REQUIRE(max_diff < 0.01);

    max_diff = 0.0;
    for (size_t i = 0; i < gpu_fg.view().data.size(); ++i) {
        double diff = std::abs(gpu_fg.view().data[i] - cpu_fg.view().data[i]);
        if (diff > max_diff) max_diff = diff;
    }
    REQUIRE(max_diff < 0.01);
}

// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,readability-identifier-length,bugprone-easily-swappable-parameters,readability-function-cognitive-complexity,readability-function-size,cppcoreguidelines-avoid-magic-numbers,modernize-use-designated-initializers,readability-uppercase-literal-suffix,readability-math-missing-parentheses,modernize-use-ranges,modernize-use-starts-ends-with,modernize-use-emplace,modernize-use-auto,modernize-loop-convert,modernize-avoid-c-style-cast,modernize-return-braced-init-list,readability-implicit-bool-conversion,readability-container-contains,readability-redundant-member-init,readability-redundant-string-init,bugprone-narrowing-conversions,cppcoreguidelines-narrowing-conversions,readability-avoid-nested-conditional-operator,modernize-use-nodiscard,readability-make-member-function-const,cppcoreguidelines-pro-type-reinterpret-cast,bugprone-implicit-widening-of-multiplication-result,readability-redundant-inline-specifier,cppcoreguidelines-prefer-member-initializer,performance-unnecessary-value-param,readability-use-concise-preprocessor-directives,readability-else-after-return,readability-string-compare,bugprone-exception-escape,cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays,bugprone-branch-clone,cert-err33-c,readability-redundant-declaration,readability-qualified-auto,modernize-use-scoped-lock,modernize-use-bool-literals,cppcoreguidelines-init-variables,cppcoreguidelines-special-member-functions,cppcoreguidelines-owning-memory,cppcoreguidelines-no-malloc,performance-enum-size,performance-avoid-endl,bugprone-unchecked-optional-access,bugprone-unchecked-string-to-number-conversion,cppcoreguidelines-pro-type-cstyle-cast,modernize-use-using,modernize-use-integer-sign-comparison,cert-dcl50-cpp,cppcoreguidelines-pro-type-const-cast,readability-identifier-naming,modernize-raw-string-literal,readability-container-size-empty,bugprone-command-processor,readability-use-std-min-max,cppcoreguidelines-avoid-non-const-global-variables,bugprone-misplaced-widening-cast,readability-misleading-indentation,cert-env33-c,performance-unnecessary-copy-initialization,readability-named-parameter,readability-isolate-declaration,cert-err34-c,modernize-avoid-variadic-functions,cppcoreguidelines-pro-bounds-constant-array-index)
