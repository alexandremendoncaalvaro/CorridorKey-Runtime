#include <algorithm>
#include <catch2/catch_all.hpp>
#include <corridorkey/engine.hpp>
#include <filesystem>

#include "../test_model_artifact_utils.hpp"

using namespace corridorkey;

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

int count_stage_samples(const std::vector<StageTiming>& timings, const std::string& name) {
    return static_cast<int>(
        std::count_if(timings.begin(), timings.end(),
                      [&](const StageTiming& timing) { return timing.name == name; }));
}

std::uint64_t sum_stage_work_units(const std::vector<StageTiming>& timings,
                                   const std::string& name) {
    std::uint64_t total = 0;
    for (const auto& timing : timings) {
        if (timing.name == name) {
            total += timing.work_units;
        }
    }
    return total;
}

}  // namespace

TEST_CASE("Tiled inference preserves input resolution", "[integration][tiling]") {
    auto model_path = std::filesystem::path(PROJECT_ROOT) / "models" / "corridorkey_int8_512.onnx";
    if (auto reason = corridorkey::tests::unusable_model_artifact_reason(model_path);
        reason.has_value()) {
        SKIP(*reason);
    }

    DeviceInfo cpu_device{"Generic CPU", 0, Backend::CPU};
    auto engine_res = Engine::create(model_path, cpu_device);
    REQUIRE(engine_res.has_value());
    auto engine = std::move(*engine_res);

    const int width = 520;
    const int height = 520;

    ImageBuffer rgb(width, height, 3);
    ImageBuffer hint(width, height, 1);
    std::fill(rgb.view().data.begin(), rgb.view().data.end(), 0.5f);
    std::fill(hint.view().data.begin(), hint.view().data.end(), 1.0f);

    InferenceParams params;
    params.target_resolution = 512;
    params.enable_tiling = true;
    params.tile_padding = 32;

    auto result = engine->process_frame(rgb.view(), hint.view(), params);
    REQUIRE(result.has_value());
    REQUIRE(result->alpha.view().width == width);
    REQUIRE(result->alpha.view().height == height);
    REQUIRE(result->foreground.view().width == width);
    REQUIRE(result->foreground.view().height == height);
    REQUIRE(result->processed.view().width == width);
    REQUIRE(result->composite.view().height == height);
}

TEST_CASE("Tiled CPU inference batches tiles when batch size allows it", "[integration][tiling]") {
    auto model_path = std::filesystem::path(PROJECT_ROOT) / "models" / "corridorkey_int8_512.onnx";
    if (auto reason = corridorkey::tests::unusable_model_artifact_reason(model_path);
        reason.has_value()) {
        SKIP(*reason);
    }

    DeviceInfo cpu_device{"Generic CPU", 0, Backend::CPU};
    auto engine_res = Engine::create(model_path, cpu_device);
    REQUIRE(engine_res.has_value());
    auto engine = std::move(*engine_res);

    const int width = 520;
    const int height = 520;

    ImageBuffer rgb(width, height, 3);
    ImageBuffer hint(width, height, 1);
    std::fill(rgb.view().data.begin(), rgb.view().data.end(), 0.5f);
    std::fill(hint.view().data.begin(), hint.view().data.end(), 1.0f);

    InferenceParams params;
    params.target_resolution = 512;
    params.enable_tiling = true;
    params.tile_padding = 32;
    params.batch_size = 2;

    std::vector<StageTiming> timings;
    auto result =
        engine->process_frame(rgb.view(), hint.view(), params,
                              [&](const StageTiming& timing) { timings.push_back(timing); });
    REQUIRE(result.has_value());

    REQUIRE(count_stage_samples(timings, "tile_infer") == 2);
    REQUIRE(sum_stage_work_units(timings, "tile_infer") == 4);
}

// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,readability-identifier-length,bugprone-easily-swappable-parameters,readability-function-cognitive-complexity,readability-function-size,cppcoreguidelines-avoid-magic-numbers,modernize-use-designated-initializers,readability-uppercase-literal-suffix,readability-math-missing-parentheses,modernize-use-ranges,modernize-use-starts-ends-with,modernize-use-emplace,modernize-use-auto,modernize-loop-convert,modernize-avoid-c-style-cast,modernize-return-braced-init-list,readability-implicit-bool-conversion,readability-container-contains,readability-redundant-member-init,readability-redundant-string-init,bugprone-narrowing-conversions,cppcoreguidelines-narrowing-conversions,readability-avoid-nested-conditional-operator,modernize-use-nodiscard,readability-make-member-function-const,cppcoreguidelines-pro-type-reinterpret-cast,bugprone-implicit-widening-of-multiplication-result,readability-redundant-inline-specifier,cppcoreguidelines-prefer-member-initializer,performance-unnecessary-value-param,readability-use-concise-preprocessor-directives,readability-else-after-return,readability-string-compare,bugprone-exception-escape,cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays,bugprone-branch-clone,cert-err33-c,readability-redundant-declaration,readability-qualified-auto,modernize-use-scoped-lock,modernize-use-bool-literals,cppcoreguidelines-init-variables,cppcoreguidelines-special-member-functions,cppcoreguidelines-owning-memory,cppcoreguidelines-no-malloc,performance-enum-size,performance-avoid-endl,bugprone-unchecked-optional-access,bugprone-unchecked-string-to-number-conversion,cppcoreguidelines-pro-type-cstyle-cast,modernize-use-using,modernize-use-integer-sign-comparison,cert-dcl50-cpp,cppcoreguidelines-pro-type-const-cast,readability-identifier-naming,modernize-raw-string-literal,readability-container-size-empty,bugprone-command-processor,readability-use-std-min-max,cppcoreguidelines-avoid-non-const-global-variables,bugprone-misplaced-widening-cast,readability-misleading-indentation,cert-env33-c,performance-unnecessary-copy-initialization,readability-named-parameter,readability-isolate-declaration,cert-err34-c,modernize-avoid-variadic-functions,cppcoreguidelines-pro-bounds-constant-array-index)
