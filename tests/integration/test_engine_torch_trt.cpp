#include <algorithm>
#include <catch2/catch_all.hpp>
#include <corridorkey/engine.hpp>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

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

std::filesystem::path dynamic_torchscript_artifact() {
    return std::filesystem::path(PROJECT_ROOT) / "temp" / "dynamic-rtx" /
           "corridorkey_dynamic_green_fp16.ts";
}

std::filesystem::path dynamic_torchtrt_external_pos_artifact() {
    return std::filesystem::path(PROJECT_ROOT) / "temp" / "torchtrt_external_pos_probe" /
           "corridorkey_dynamic_green_external_pos_fp16.ts";
}

bool has_stage(const std::vector<StageTiming>& timings, std::string_view name) {
    return std::any_of(timings.begin(), timings.end(),
                       [&](const StageTiming& timing) { return timing.name == name; });
}

bool has_stage_prefix(const std::vector<StageTiming>& timings, std::string_view prefix) {
    return std::any_of(timings.begin(), timings.end(), [&](const StageTiming& timing) {
        return timing.name.starts_with(prefix);
    });
}

std::optional<std::string> environment_variable_copy(const char* name) {
#if defined(_WIN32)
    char* value = nullptr;
    std::size_t size = 0;
    if (_dupenv_s(&value, &size, name) != 0 || value == nullptr) {
        return std::nullopt;
    }
    std::string result(value);
    std::free(value);
    return result;
#else
    const char* value = std::getenv(name);
    if (value == nullptr) {
        return std::nullopt;
    }
    return std::string(value);
#endif
}

class ScopedEnvVar {
   public:
    ScopedEnvVar(const char* name, std::string value) : m_name(name) {
        m_previous = environment_variable_copy(name);
#if defined(_WIN32)
        _putenv_s(m_name.c_str(), value.c_str());
#else
        setenv(m_name.c_str(), value.c_str(), 1);
#endif
    }

    ~ScopedEnvVar() {
#if defined(_WIN32)
        _putenv_s(m_name.c_str(), m_previous.value_or("").c_str());
#else
        if (!m_previous.has_value()) {
            unsetenv(m_name.c_str());
        } else {
            setenv(m_name.c_str(), m_previous->c_str(), 1);
        }
#endif
    }

   private:
    std::string m_name;
    std::optional<std::string> m_previous;
};

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

TEST_CASE("TorchTRT session runs a dynamic TorchScript artifact at multiple resolutions",
          "[integration][torchtrt][dynamic]") {
#if !defined(_WIN32)
    SUCCEED("TorchTRT in-process backend is Windows-only in Sprint 1.");
#else
    const auto model_path = dynamic_torchscript_artifact();
    if (auto reason = corridorkey::tests::unusable_model_artifact_reason(
            model_path, "dynamic TorchScript RTX artifact");
        reason.has_value()) {
        SKIP(*reason);
    }

    auto engine = Engine::create(model_path, DeviceInfo{"TorchTRT", 10240, Backend::TorchTRT});
    if (!engine.has_value()) {
        SKIP("Engine::create failed: " + engine.error().message);
    }
    REQUIRE(engine.value()->current_device().backend == Backend::TorchTRT);
    REQUIRE(engine.value()->recommended_resolution() == 0);

    struct ResolutionCase {
        int width;
        int height;
    };

    for (const auto resolution :
         {ResolutionCase{512, 512}, ResolutionCase{1024, 1024}, ResolutionCase{640, 360}}) {
        ImageBuffer rgb(resolution.width, resolution.height, 3);
        ImageBuffer hint(resolution.width, resolution.height, 1);
        std::fill(rgb.view().data.begin(), rgb.view().data.end(), 0.5F);
        std::fill(hint.view().data.begin(), hint.view().data.end(), 1.0F);

        InferenceParams params;
        params.target_resolution = 512;
        params.upscale_method = UpscaleMethod::Bilinear;
        std::vector<StageTiming> timings;
        auto result = engine.value()->process_frame(
            rgb.view(), hint.view(), params,
            [&](const StageTiming& timing) { timings.push_back(timing); });
        REQUIRE(result.has_value());
        REQUIRE(result->alpha.view().width == resolution.width);
        REQUIRE(result->alpha.view().height == resolution.height);
        REQUIRE(result->foreground.view().width == resolution.width);
        REQUIRE(result->foreground.view().height == resolution.height);
        CHECK(has_stage(timings, "frame_prepare_inputs"));
        const bool used_host_upload = has_stage(timings, "torchtrt_prepare_upload");
        const bool used_device_wrap = has_stage(timings, "torchtrt_prepare_device_wrap");
        CHECK((used_host_upload || used_device_wrap));
        if (used_device_wrap) {
            CHECK_FALSE(has_stage(timings, "torchtrt_prepare_planar_copy"));
        }
        if (resolution.width == params.target_resolution &&
            resolution.height == params.target_resolution) {
            CHECK(used_device_wrap);
            CHECK_FALSE(has_stage(timings, "torchtrt_prepare_pack"));
            CHECK_FALSE(used_host_upload);
            CHECK_FALSE(has_stage(timings, "frame_extract_outputs_finalize"));
        }
        CHECK(has_stage(timings, "torchtrt_forward"));
        CHECK(has_stage(timings, "torchtrt_extract_outputs"));
        CHECK(has_stage(timings, "frame_extract_outputs_resize"));
        if (resolution.width != params.target_resolution ||
            resolution.height != params.target_resolution) {
            CHECK(has_stage(timings, "frame_extract_outputs_finalize"));
        }
    }
#endif
}

TEST_CASE("TorchTRT session caches embedded external positional grids",
          "[integration][torchtrt][dynamic][regression]") {
#if !defined(_WIN32)
    SUCCEED("TorchTRT in-process backend is Windows-only in Sprint 1.");
#else
    const auto model_path = dynamic_torchtrt_external_pos_artifact();
    if (auto reason = corridorkey::tests::unusable_model_artifact_reason(
            model_path, "dynamic TorchTRT external-pos artifact");
        reason.has_value()) {
        SKIP(*reason);
    }

    auto engine = Engine::create(model_path, DeviceInfo{"TorchTRT", 10240, Backend::TorchTRT});
    if (!engine.has_value()) {
        SKIP("Engine::create failed: " + engine.error().message);
    }
    REQUIRE(engine.value()->recommended_resolution() == 0);

    constexpr int kRes = 512;
    ImageBuffer rgb(kRes, kRes, 3);
    ImageBuffer hint(kRes, kRes, 1);
    std::fill(rgb.view().data.begin(), rgb.view().data.end(), 0.5F);
    std::fill(hint.view().data.begin(), hint.view().data.end(), 1.0F);

    InferenceParams params;
    params.target_resolution = kRes;

    std::vector<StageTiming> first_timings;
    auto first = engine.value()->process_frame(
        rgb.view(), hint.view(), params,
        [&](const StageTiming& timing) { first_timings.push_back(timing); });
    REQUIRE(first.has_value());
    CHECK(has_stage(first_timings, "torchtrt_prepare_pos_grid"));

    std::vector<StageTiming> second_timings;
    auto second = engine.value()->process_frame(
        rgb.view(), hint.view(), params,
        [&](const StageTiming& timing) { second_timings.push_back(timing); });
    REQUIRE(second.has_value());
    CHECK_FALSE(has_stage(second_timings, "torchtrt_prepare_pos_grid"));
#endif
}

TEST_CASE("TorchTRT CUDA graph path emits replay or explicit fallback telemetry",
          "[integration][torchtrt][dynamic][regression]") {
#if !defined(_WIN32)
    SUCCEED("TorchTRT in-process backend is Windows-only in Sprint 1.");
#else
    const auto model_path = dynamic_torchtrt_external_pos_artifact();
    if (auto reason = corridorkey::tests::unusable_model_artifact_reason(
            model_path, "dynamic TorchTRT external-pos artifact");
        reason.has_value()) {
        SKIP(*reason);
    }

    ScopedEnvVar graph_env("CORRIDORKEY_TRT_CUDA_GRAPH", "1");
    std::vector<StageTiming> create_timings;
    auto engine = Engine::create(model_path, DeviceInfo{"TorchTRT", 10240, Backend::TorchTRT},
                                 [&](const StageTiming& timing) {
                                     create_timings.push_back(timing);
                                 });
    if (!engine.has_value()) {
        SKIP("Engine::create failed: " + engine.error().message);
    }
    REQUIRE(has_stage(create_timings, "torchtrt_cuda_graph_config_enabled"));

    constexpr int kRes = 512;
    ImageBuffer rgb(kRes, kRes, 3);
    ImageBuffer hint(kRes, kRes, 1);
    std::fill(rgb.view().data.begin(), rgb.view().data.end(), 0.5F);
    std::fill(hint.view().data.begin(), hint.view().data.end(), 1.0F);

    InferenceParams params;
    params.target_resolution = kRes;

    std::vector<StageTiming> timings;
    for (int iteration = 0; iteration < 4; ++iteration) {
        auto result = engine.value()->process_frame(
            rgb.view(), hint.view(), params,
            [&](const StageTiming& timing) { timings.push_back(timing); });
        REQUIRE(result.has_value());
    }

    const bool replayed = has_stage(timings, "torchtrt_cuda_graph_replay");
    const bool reported_fallback = has_stage_prefix(timings, "torchtrt_cuda_graph_fallback_");
    CHECK((replayed || reported_fallback));
    if (replayed) {
        CHECK(has_stage(timings, "torchtrt_input_current_stream_event"));
        CHECK_FALSE(has_stage(timings, "torchtrt_input_wait_event_enqueue"));
        CHECK(has_stage(timings, "torchtrt_input_ready_wait"));
        CHECK(has_stage(timings, "gpu_prepare_device"));
        CHECK(has_stage(timings, "gpu_prepare_wait_over_device"));
        CHECK(has_stage(timings, "torchtrt_cuda_graph_input_copy"));
        CHECK(has_stage(timings, "torchtrt_cuda_graph_input_copy_gpu"));
        CHECK(has_stage(timings, "torchtrt_cuda_graph_input_copy_queue_wait"));
        CHECK(has_stage(timings, "torchtrt_cuda_graph_capture_stream_wait"));
        CHECK(has_stage(timings, "torchtrt_cuda_graph_replay_gpu"));
        CHECK(has_stage(timings, "torchtrt_cuda_graph_replay_queue_wait"));
        CHECK(has_stage(timings, "torchtrt_cuda_graph_current_stream_wait"));
    }
    if (reported_fallback) {
        CHECK(has_stage(timings, "torchtrt_forward_direct"));
    }
#endif
}

TEST_CASE("TorchTRT keeps GPU source and despill when despeckle uses CPU",
          "[integration][torchtrt][dynamic][regression]") {
#if !defined(_WIN32)
    SUCCEED("TorchTRT in-process backend is Windows-only in Sprint 1.");
#else
    const auto model_path = dynamic_torchtrt_external_pos_artifact();
    if (auto reason = corridorkey::tests::unusable_model_artifact_reason(
            model_path, "dynamic TorchTRT external-pos artifact");
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
    params.target_resolution = kRes;
    params.upscale_method = UpscaleMethod::Bilinear;
    params.output_auxiliary_images = false;
    params.source_passthrough = true;
    params.auto_despeckle = true;
    params.despeckle_size = 1;

    std::vector<StageTiming> timings;
    auto result = engine.value()->process_frame(
        rgb.view(), hint.view(), params,
        [&](const StageTiming& timing) { timings.push_back(timing); });
    REQUIRE(result.has_value());
    if (!has_stage(timings, "post_gpu_prepare")) {
        SKIP("TorchTRT CUDA post-process path unavailable.");
    }

    CHECK(has_stage(timings, "post_source_passthrough_gpu"));
    CHECK(has_stage(timings, "post_source_passthrough_gpu_copy_device_to_device"));
    CHECK(has_stage(timings, "post_despill_gpu"));
    CHECK(has_stage(timings, "post_despeckle"));
    CHECK_FALSE(has_stage(timings, "post_source_passthrough"));
    CHECK_FALSE(has_stage(timings, "post_despill"));
#endif
}

// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,readability-identifier-length,bugprone-easily-swappable-parameters,readability-function-cognitive-complexity,readability-function-size,cppcoreguidelines-avoid-magic-numbers,modernize-use-designated-initializers,readability-uppercase-literal-suffix,readability-math-missing-parentheses,modernize-use-ranges,modernize-use-starts-ends-with,modernize-use-emplace,modernize-use-auto,modernize-loop-convert,modernize-avoid-c-style-cast,modernize-return-braced-init-list,readability-implicit-bool-conversion,readability-container-contains,readability-redundant-member-init,readability-redundant-string-init,bugprone-narrowing-conversions,cppcoreguidelines-narrowing-conversions,readability-avoid-nested-conditional-operator,modernize-use-nodiscard,readability-make-member-function-const,cppcoreguidelines-pro-type-reinterpret-cast,bugprone-implicit-widening-of-multiplication-result,readability-redundant-inline-specifier,cppcoreguidelines-prefer-member-initializer,performance-unnecessary-value-param,readability-use-concise-preprocessor-directives,readability-else-after-return,readability-string-compare,bugprone-exception-escape,cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays,bugprone-branch-clone,cert-err33-c,readability-redundant-declaration,readability-qualified-auto,modernize-use-scoped-lock,modernize-use-bool-literals,cppcoreguidelines-init-variables,cppcoreguidelines-special-member-functions,cppcoreguidelines-owning-memory,cppcoreguidelines-no-malloc,performance-enum-size,performance-avoid-endl,bugprone-unchecked-optional-access,bugprone-unchecked-string-to-number-conversion,cppcoreguidelines-pro-type-cstyle-cast,modernize-use-using,modernize-use-integer-sign-comparison,cert-dcl50-cpp,cppcoreguidelines-pro-type-const-cast,readability-identifier-naming,modernize-raw-string-literal,readability-container-size-empty,bugprone-command-processor,readability-use-std-min-max,cppcoreguidelines-avoid-non-const-global-variables,bugprone-misplaced-widening-cast,readability-misleading-indentation,cert-env33-c,performance-unnecessary-copy-initialization,readability-named-parameter,readability-isolate-declaration,cert-err34-c,modernize-avoid-variadic-functions,cppcoreguidelines-pro-bounds-constant-array-index)
