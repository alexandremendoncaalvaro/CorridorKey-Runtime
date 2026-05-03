#include <catch2/catch_all.hpp>
#include <corridorkey/engine.hpp>
#include <filesystem>

#include "../test_model_artifact_utils.hpp"

using namespace corridorkey;

namespace {

// Sprint 0 produced these green Torch-TensorRT engines under
// temp/blue-diagnose/. They are scratch (not committed, not in fetch_models)
// so this test can only run on a workstation that has executed the Sprint 0
// compile or staged equivalent artifacts. The test SKIPs cleanly when the
// fixture is missing, mirroring how test_engine_mlx.cpp handles a missing
// MLX bridge.
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

std::filesystem::path sprint0_torchtrt_artifact(int resolution) {
    return std::filesystem::path(PROJECT_ROOT) / "temp" / "blue-diagnose" /
           "green-torchtrt-local-windows" /
           ("corridorkey_torchtrt_fp16_" + std::to_string(resolution) + ".ts");
}

}  // namespace

TEST_CASE("TorchTRT session loads and runs a green .ts engine end-to-end",
          "[integration][torchtrt]") {
#if !defined(_WIN32)
    SUCCEED("TorchTRT in-process backend is Windows-only in Sprint 1.");
#else
    const auto model_path = sprint0_torchtrt_artifact(512);
    if (auto reason = corridorkey::tests::unusable_model_artifact_reason(
            model_path, "TorchTRT engine (Sprint 0 fixture)");
        reason.has_value()) {
        SKIP(*reason);
    }

    auto engine = Engine::create(model_path, DeviceInfo{"TorchTRT", 10240, Backend::TorchTRT});
    if (!engine.has_value()) {
        // Common skip path: no CUDA-capable GPU, or vendor/torchtrt-windows
        // not staged. Surface the underlying reason rather than treating
        // missing GPU as a hard test failure.
        SKIP("Engine::create failed: " + engine.error().message);
    }
    REQUIRE(engine.value()->current_device().backend == Backend::TorchTRT);
    REQUIRE(engine.value()->recommended_resolution() == 512);

    constexpr int kRes = 512;
    ImageBuffer rgb(kRes, kRes, 3);
    ImageBuffer hint(kRes, kRes, 1);

    // Synthetic green-screen input: uniform mid-green plus a centre-square
    // hint mask. Same shape as test_engine_mlx.cpp uses, scaled to 512.
    for (int y_pos = 0; y_pos < kRes; ++y_pos) {
        for (int x_pos = 0; x_pos < kRes; ++x_pos) {
            rgb.view()(y_pos, x_pos, 0) = 0.1F;
            rgb.view()(y_pos, x_pos, 1) = 0.8F;
            rgb.view()(y_pos, x_pos, 2) = 0.1F;
            hint.view()(y_pos, x_pos, 0) = (x_pos > kRes / 4 && x_pos < (3 * kRes) / 4 &&
                                            y_pos > kRes / 4 && y_pos < (3 * kRes) / 4)
                                               ? 1.0F
                                               : 0.0F;
        }
    }

    auto result = engine.value()->process_frame(rgb.view(), hint.view(), {});
    REQUIRE(result.has_value());
    REQUIRE(result->alpha.view().width == kRes);
    REQUIRE(result->alpha.view().height == kRes);
    REQUIRE(result->foreground.view().width == kRes);
    REQUIRE(result->foreground.view().height == kRes);

    // Numeric sanity: alpha must be finite and inside [0, 1] per Sprint 0
    // results in temp/blue-diagnose/SPRINT0_RESULTS.md.
    const auto alpha = result->alpha.view();
    float min_alpha = 1.0F;
    float max_alpha = 0.0F;
    bool has_nan = false;
    for (const float value : alpha.data) {
        if (std::isnan(value)) {
            has_nan = true;
            continue;
        }
        min_alpha = std::min(min_alpha, value);
        max_alpha = std::max(max_alpha, value);
    }
    REQUIRE_FALSE(has_nan);
    REQUIRE(min_alpha >= 0.0F);
    REQUIRE(max_alpha <= 1.0F + 1e-3F);
#endif
}

TEST_CASE("TorchTRT session honours output_alpha_only by skipping foreground materialisation",
          "[integration][torchtrt][regression]") {
#if !defined(_WIN32)
    SUCCEED("TorchTRT in-process backend is Windows-only in Sprint 1.");
#else
    const auto model_path = sprint0_torchtrt_artifact(512);
    if (auto reason = corridorkey::tests::unusable_model_artifact_reason(
            model_path, "TorchTRT engine (Sprint 0 fixture)");
        reason.has_value()) {
        SKIP(*reason);
    }

    auto engine = Engine::create(model_path, DeviceInfo{"TorchTRT", 10240, Backend::TorchTRT});
    if (!engine.has_value()) {
        SKIP("Engine::create failed: " + engine.error().message);
    }

    constexpr int kRes = 512;
    ImageBuffer rgb(kRes, kRes, 3);
    ImageBuffer hint(kRes, kRes, 1);
    std::fill(rgb.view().data.begin(), rgb.view().data.end(), 0.5F);
    std::fill(hint.view().data.begin(), hint.view().data.end(), 1.0F);

    InferenceParams params;
    params.output_alpha_only = true;

    auto result = engine.value()->process_frame(rgb.view(), hint.view(), params);
    REQUIRE(result.has_value());
    REQUIRE(result->alpha.view().width == kRes);
    // Foreground is intentionally unfilled when output_alpha_only is set.
    REQUIRE(result->foreground.view().data.empty());
#endif
}

// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,readability-identifier-length,bugprone-easily-swappable-parameters,readability-function-cognitive-complexity,readability-function-size,cppcoreguidelines-avoid-magic-numbers,modernize-use-designated-initializers,readability-uppercase-literal-suffix,readability-math-missing-parentheses,modernize-use-ranges,modernize-use-starts-ends-with,modernize-use-emplace,modernize-use-auto,modernize-loop-convert,modernize-avoid-c-style-cast,modernize-return-braced-init-list,readability-implicit-bool-conversion,readability-container-contains,readability-redundant-member-init,readability-redundant-string-init,bugprone-narrowing-conversions,cppcoreguidelines-narrowing-conversions,readability-avoid-nested-conditional-operator,modernize-use-nodiscard,readability-make-member-function-const,cppcoreguidelines-pro-type-reinterpret-cast,bugprone-implicit-widening-of-multiplication-result,readability-redundant-inline-specifier,cppcoreguidelines-prefer-member-initializer,performance-unnecessary-value-param,readability-use-concise-preprocessor-directives,readability-else-after-return,readability-string-compare,bugprone-exception-escape,cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays,bugprone-branch-clone,cert-err33-c,readability-redundant-declaration,readability-qualified-auto,modernize-use-scoped-lock,modernize-use-bool-literals,cppcoreguidelines-init-variables,cppcoreguidelines-special-member-functions,cppcoreguidelines-owning-memory,cppcoreguidelines-no-malloc,performance-enum-size,performance-avoid-endl,bugprone-unchecked-optional-access,bugprone-unchecked-string-to-number-conversion,cppcoreguidelines-pro-type-cstyle-cast,modernize-use-using,modernize-use-integer-sign-comparison,cert-dcl50-cpp,cppcoreguidelines-pro-type-const-cast,readability-identifier-naming,modernize-raw-string-literal,readability-container-size-empty,bugprone-command-processor,readability-use-std-min-max,cppcoreguidelines-avoid-non-const-global-variables,bugprone-misplaced-widening-cast,readability-misleading-indentation,cert-env33-c,performance-unnecessary-copy-initialization,readability-named-parameter,readability-isolate-declaration,cert-err34-c,modernize-avoid-variadic-functions,cppcoreguidelines-pro-bounds-constant-array-index)
