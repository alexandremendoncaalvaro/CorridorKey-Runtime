#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <string>
#include <vector>

#include "core/gpu_prep.hpp"
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

TEST_CASE("GpuInputPrep Availability", "[unit][core]") {
    core::GpuInputPrep prep;
    bool avail = prep.available();
    SUCCEED("Queried availability: " + std::to_string(avail));
}

TEST_CASE("GpuInputPrep Correctness vs CPU reference", "[unit][core]") {
    core::GpuInputPrep prep;
    if (!prep.available()) {
        SKIP("GPU input prep not available on this host");
    }

    const int src_w = 128;
    const int src_h = 96;
    const int model_w = 64;
    const int model_h = 64;

    std::vector<float> rgb_data(static_cast<size_t>(src_w) * src_h * 3);
    std::vector<float> hint_data(static_cast<size_t>(src_w) * src_h);

    for (int y = 0; y < src_h; ++y) {
        for (int x = 0; x < src_w; ++x) {
            size_t idx = (static_cast<size_t>(y) * src_w + x) * 3;
            rgb_data[idx + 0] = static_cast<float>(x) / src_w;
            rgb_data[idx + 1] = static_cast<float>(y) / src_h;
            rgb_data[idx + 2] = 0.5f;

            hint_data[static_cast<size_t>(y) * src_w + x] =
                static_cast<float>(x + y) / (src_w + src_h);
        }
    }

    Image rgb = {src_w, src_h, 3, {rgb_data.data(), rgb_data.size()}};
    Image hint = {src_w, src_h, 1, {hint_data.data(), hint_data.size()}};

    const size_t channel_stride = static_cast<size_t>(model_w) * model_h;
    const size_t total_planar = 4 * channel_stride;

    const std::array<float, 3> mean = {0.485f, 0.456f, 0.406f};
    const std::array<float, 3> inv_stddev = {1.0f / 0.229f, 1.0f / 0.224f, 1.0f / 0.225f};

    // GPU path
    std::vector<float> gpu_planar(total_planar, 0.0f);
    std::vector<std::string> stages;
    auto record_stage = [&](const StageTiming& timing) { stages.push_back(timing.name); };
    auto res = prep.prepare_inputs(rgb, hint, gpu_planar.data(), model_w, model_h, mean, inv_stddev,
                                   record_stage);
    REQUIRE(res.has_value());

    auto has_stage = [&](const std::string& name) {
        return std::find(stages.begin(), stages.end(), name) != stages.end();
    };
    CHECK(has_stage("gpu_prepare_ensure_buffers"));
    CHECK(has_stage("gpu_prepare_upload_enqueue"));
    CHECK(has_stage("gpu_prepare_rgb_resize_enqueue"));
    CHECK(has_stage("gpu_prepare_hint_resize_enqueue"));
    CHECK(has_stage("gpu_prepare_split_enqueue"));
    CHECK(has_stage("gpu_prepare_normalize_enqueue"));
    CHECK(has_stage("gpu_prepare_hint_copy_enqueue"));
    CHECK(has_stage("gpu_prepare_download_enqueue"));
    CHECK(has_stage("gpu_prepare_sync"));

    // CPU reference
    ImageBuffer cpu_rgb_buf(model_w, model_h, 3);
    ImageBuffer cpu_hint_buf(model_w, model_h, 1);
    ColorUtils::State state;

    ColorUtils::resize_area_into(rgb, cpu_rgb_buf.view(), state);
    ColorUtils::resize_area_into(hint, cpu_hint_buf.view(), state);

    std::vector<float> cpu_planar(total_planar, 0.0f);
    ColorUtils::pack_normalized_rgb_and_hint_to_planar(cpu_rgb_buf.view(), cpu_hint_buf.view(),
                                                       cpu_planar.data(), mean, inv_stddev);

    // Compare with tolerance for different resize implementations
    double max_diff = 0.0;
    for (size_t i = 0; i < total_planar; ++i) {
        double diff = std::abs(gpu_planar[i] - cpu_planar[i]);
        if (diff > max_diff) max_diff = diff;
    }

    INFO("Max difference between GPU and CPU planar output: " << max_diff);
    // NPP bilinear vs OpenCV exact area + slight blur produces differences up to 0.15
    REQUIRE(max_diff < 0.15);
}

TEST_CASE("GpuInputPrep device path records CUDA completion event", "[unit][core]") {
    core::GpuInputPrep prep;
    if (!prep.available()) {
        SKIP("GPU input prep not available on this host");
    }

    const int src_w = 64;
    const int src_h = 64;
    const int model_w = 64;
    const int model_h = 64;

    std::vector<float> rgb_data(static_cast<size_t>(src_w) * src_h * 3, 0.5f);
    std::vector<float> hint_data(static_cast<size_t>(src_w) * src_h, 1.0f);
    Image rgb = {src_w, src_h, 3, {rgb_data.data(), rgb_data.size()}};
    Image hint = {src_w, src_h, 1, {hint_data.data(), hint_data.size()}};

    const std::array<float, 3> mean = {0.485f, 0.456f, 0.406f};
    const std::array<float, 3> inv_stddev = {1.0f / 0.229f, 1.0f / 0.224f, 1.0f / 0.225f};

    std::vector<std::string> stages;
    auto record_stage = [&](const StageTiming& timing) { stages.push_back(timing.name); };

    auto res =
        prep.prepare_inputs_device(rgb, hint, model_w, model_h, mean, inv_stddev, record_stage);
    REQUIRE(res.has_value());
    REQUIRE(res->planar_device != nullptr);
    REQUIRE(res->ready_event != nullptr);

    auto has_stage = [&](const std::string& name) {
        return std::find(stages.begin(), stages.end(), name) != stages.end();
    };
    CHECK(has_stage("gpu_prepare_event_record"));
    CHECK_FALSE(has_stage("gpu_prepare_sync"));
}

// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,readability-identifier-length,bugprone-easily-swappable-parameters,readability-function-cognitive-complexity,readability-function-size,cppcoreguidelines-avoid-magic-numbers,modernize-use-designated-initializers,readability-uppercase-literal-suffix,readability-math-missing-parentheses,modernize-use-ranges,modernize-use-starts-ends-with,modernize-use-emplace,modernize-use-auto,modernize-loop-convert,modernize-avoid-c-style-cast,modernize-return-braced-init-list,readability-implicit-bool-conversion,readability-container-contains,readability-redundant-member-init,readability-redundant-string-init,bugprone-narrowing-conversions,cppcoreguidelines-narrowing-conversions,readability-avoid-nested-conditional-operator,modernize-use-nodiscard,readability-make-member-function-const,cppcoreguidelines-pro-type-reinterpret-cast,bugprone-implicit-widening-of-multiplication-result,readability-redundant-inline-specifier,cppcoreguidelines-prefer-member-initializer,performance-unnecessary-value-param,readability-use-concise-preprocessor-directives,readability-else-after-return,readability-string-compare,bugprone-exception-escape,cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays,bugprone-branch-clone,cert-err33-c,readability-redundant-declaration,readability-qualified-auto,modernize-use-scoped-lock,modernize-use-bool-literals,cppcoreguidelines-init-variables,cppcoreguidelines-special-member-functions,cppcoreguidelines-owning-memory,cppcoreguidelines-no-malloc,performance-enum-size,performance-avoid-endl,bugprone-unchecked-optional-access,bugprone-unchecked-string-to-number-conversion,cppcoreguidelines-pro-type-cstyle-cast,modernize-use-using,modernize-use-integer-sign-comparison,cert-dcl50-cpp,cppcoreguidelines-pro-type-const-cast,readability-identifier-naming,modernize-raw-string-literal,readability-container-size-empty,bugprone-command-processor,readability-use-std-min-max,cppcoreguidelines-avoid-non-const-global-variables,bugprone-misplaced-widening-cast,readability-misleading-indentation,cert-env33-c,performance-unnecessary-copy-initialization,readability-named-parameter,readability-isolate-declaration,cert-err34-c,modernize-avoid-variadic-functions,cppcoreguidelines-pro-bounds-constant-array-index)
