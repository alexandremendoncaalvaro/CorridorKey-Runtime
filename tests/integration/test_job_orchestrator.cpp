#include <algorithm>
#include <catch2/catch_all.hpp>
#include <corridorkey/frame_io.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>

#include "../test_model_artifact_utils.hpp"
#include "app/job_orchestrator.hpp"

using namespace corridorkey;
using namespace corridorkey::app;

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

std::filesystem::path create_dummy_frame(const std::filesystem::path& dir, int index, int width,
                                         int height) {
    char filename[64];
    std::snprintf(filename, sizeof(filename), "dummy_frame_%04d.png", index);
    auto path = dir / filename;

    ImageBuffer original(width, height, 3);
    Image view = original.view();
    std::fill(view.data.begin(), view.data.end(), 0.5f);

    auto res = frame_io::write_frame(path, view);
    REQUIRE(res.has_value());
    return path;
}

bool has_stage(const nlohmann::json& timings, const std::string& name) {
    return timings.is_array() &&
           std::any_of(timings.begin(), timings.end(), [&](const auto& timing) {
               return timing.is_object() && timing.value("name", std::string()) == name;
           });
}
}  // namespace

TEST_CASE("JobOrchestrator runs full sequence and respects cancellation", "[integration][app]") {
    auto model_path = std::filesystem::path(PROJECT_ROOT) / "models" / "corridorkey_int8_512.onnx";
    if (auto reason = corridorkey::tests::unusable_model_artifact_reason(model_path);
        reason.has_value()) {
        SKIP(*reason);
    }

    auto tmp_dir = std::filesystem::temp_directory_path() / "corridorkey_test_orchest";
    std::filesystem::create_directories(tmp_dir);

    // Create 3 dummy frames
    for (int i = 1; i <= 3; ++i) {
        create_dummy_frame(tmp_dir, i, 64, 64);
    }

    JobRequest request;
    request.input_path = tmp_dir;
    request.input_path = tmp_dir;
    request.output_path = tmp_dir / "out_seq";
    request.model_path = model_path;
    request.device = DeviceInfo{"Generic CPU", 0, Backend::CPU};
    request.params.target_resolution = 512;
    request.params.enable_tiling = false;

    SECTION("Happy path completion") {
        int frames_processed = 0;
        auto on_progress = [&](float /*progress*/, const std::string& status) -> bool {
            if (status.find("Processing frame") != std::string::npos) {
                frames_processed++;
            }
            return true;  // Continue
        };

        auto run_res = JobOrchestrator::run(request, on_progress, nullptr);
        if (!run_res.has_value()) {
            FAIL("Orchestrator returned error: " << run_res.error().message);
        }

        // Ensure all frames were rendered
        REQUIRE(std::filesystem::exists(tmp_dir / "out_seq" / "Comp" / "dummy_frame_0001.png"));
        REQUIRE(std::filesystem::exists(tmp_dir / "out_seq" / "Comp" / "dummy_frame_0002.png"));
        REQUIRE(std::filesystem::exists(tmp_dir / "out_seq" / "Comp" / "dummy_frame_0003.png"));
    }

    SECTION("Immediate cancellation") {
        int frames_processed = 0;
        auto on_progress = [&](float /*progress*/, const std::string& status) -> bool {
            if (status.find("Processing frame") != std::string::npos) {
                frames_processed++;
            }
            // Cancel immediately on first progress report
            return false;
        };

        auto run_res = JobOrchestrator::run(request, on_progress, nullptr);

        // The orchestrator returns an error `Cancellation requested`
        REQUIRE(!run_res.has_value());
        REQUIRE(std::string(run_res.error().message).find("cancel") != std::string::npos);

        // It should have halted before finishing the entire backlog
        REQUIRE(frames_processed < 3);
    }

    // Cleanup
    std::filesystem::remove_all(tmp_dir);
}

TEST_CASE("JobOrchestrator benchmark reports metadata and stage coverage",
          "[integration][app][runtime][regression]") {
    auto model_path = std::filesystem::path(PROJECT_ROOT) / "models" / "corridorkey_int8_512.onnx";
    if (auto reason = corridorkey::tests::unusable_model_artifact_reason(model_path);
        reason.has_value()) {
        SKIP(*reason);
    }

    SECTION("synthetic benchmark surfaces session and steady-state timings") {
        JobRequest request;
        request.model_path = model_path;
        request.device = DeviceInfo{"Generic CPU", 0, Backend::CPU};
        request.params.target_resolution = 512;
        request.params.batch_size = 1;
        request.params.enable_tiling = false;

        auto report = JobOrchestrator::run_benchmark(request);
        REQUIRE_FALSE(report.contains("error"));
        REQUIRE(report["mode"] == "synthetic");
        REQUIRE(report["batch_size"] == 1);
        REQUIRE(report["tiling_enabled"] == false);
        REQUIRE(report["io_binding"]["requested_mode"] == "auto");
        REQUIRE(report["io_binding"]["eligible"] == false);
        REQUIRE(report["io_binding"]["active"] == false);
        REQUIRE(report["io_binding"]["observed"] == false);
        REQUIRE(report["warmup_runs"] == 2);
        REQUIRE(report["steady_state_runs"] == 5);
        REQUIRE(has_stage(report["stage_timings"], "ort_env_acquire"));
        REQUIRE(has_stage(report["stage_timings"], "ort_session_options"));
        REQUIRE(has_stage(report["stage_timings"], "ort_session_create"));
        REQUIRE(has_stage(report["stage_timings"], "ort_metadata_extract"));
        REQUIRE(has_stage(report["stage_timings"], "frame_prepare_inputs"));
        REQUIRE(has_stage(report["stage_timings"], "ort_run"));
        REQUIRE(has_stage(report["stage_timings"], "frame_extract_outputs"));
        REQUIRE(has_stage(report["stage_timings"], "frame_extract_outputs_tensor_materialize"));
        REQUIRE(has_stage(report["stage_timings"], "frame_extract_outputs_resize"));
        REQUIRE(has_stage(report["stage_timings"], "frame_extract_outputs_finalize"));
    }

    SECTION("workload benchmark reports sequence stages and additive metadata") {
        auto tmp_dir =
            std::filesystem::temp_directory_path() / "corridorkey_benchmark_orchestrator_test";
        std::filesystem::remove_all(tmp_dir);
        std::filesystem::create_directories(tmp_dir);
        create_dummy_frame(tmp_dir, 1, 64, 64);
        create_dummy_frame(tmp_dir, 2, 64, 64);

        JobRequest request;
        request.input_path = tmp_dir;
        request.model_path = model_path;
        request.device = DeviceInfo{"Generic CPU", 0, Backend::CPU};
        request.params.target_resolution = 512;
        request.params.batch_size = 2;
        request.params.enable_tiling = false;

        auto report = JobOrchestrator::run_benchmark(request);
        std::filesystem::remove_all(tmp_dir);

        REQUIRE_FALSE(report.contains("error"));
        REQUIRE(report["mode"] == "workload");
        REQUIRE(report["batch_size"] == 2);
        REQUIRE(report["tiling_enabled"] == false);
        REQUIRE(report["io_binding"]["requested_mode"] == "auto");
        REQUIRE(report["io_binding"]["eligible"] == false);
        REQUIRE(report["io_binding"]["active"] == false);
        REQUIRE(report["io_binding"]["observed"] == false);
        REQUIRE(report["effective_resolution"] == 512);
        REQUIRE(has_stage(report["stage_timings"], "sequence_read_input"));
        REQUIRE(has_stage(report["stage_timings"], "sequence_infer_batch"));
        REQUIRE(has_stage(report["stage_timings"], "sequence_write_output"));
        REQUIRE(has_stage(report["stage_timings"], "batch_extract_outputs"));
        REQUIRE(has_stage(report["stage_timings"], "batch_extract_outputs_tensor_materialize"));
        REQUIRE(has_stage(report["stage_timings"], "batch_extract_outputs_resize"));
        REQUIRE(has_stage(report["stage_timings"], "batch_extract_outputs_finalize"));
    }
}

// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,readability-identifier-length,bugprone-easily-swappable-parameters,readability-function-cognitive-complexity,readability-function-size,cppcoreguidelines-avoid-magic-numbers,modernize-use-designated-initializers,readability-uppercase-literal-suffix,readability-math-missing-parentheses,modernize-use-ranges,modernize-use-starts-ends-with,modernize-use-emplace,modernize-use-auto,modernize-loop-convert,modernize-avoid-c-style-cast,modernize-return-braced-init-list,readability-implicit-bool-conversion,readability-container-contains,readability-redundant-member-init,readability-redundant-string-init,bugprone-narrowing-conversions,cppcoreguidelines-narrowing-conversions,readability-avoid-nested-conditional-operator,modernize-use-nodiscard,readability-make-member-function-const,cppcoreguidelines-pro-type-reinterpret-cast,bugprone-implicit-widening-of-multiplication-result,readability-redundant-inline-specifier,cppcoreguidelines-prefer-member-initializer,performance-unnecessary-value-param,readability-use-concise-preprocessor-directives,readability-else-after-return,readability-string-compare,bugprone-exception-escape,cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays,bugprone-branch-clone,cert-err33-c,readability-redundant-declaration,readability-qualified-auto,modernize-use-scoped-lock,modernize-use-bool-literals,cppcoreguidelines-init-variables,cppcoreguidelines-special-member-functions,cppcoreguidelines-owning-memory,cppcoreguidelines-no-malloc,performance-enum-size,performance-avoid-endl,bugprone-unchecked-optional-access,bugprone-unchecked-string-to-number-conversion,cppcoreguidelines-pro-type-cstyle-cast,modernize-use-using,modernize-use-integer-sign-comparison,cert-dcl50-cpp,cppcoreguidelines-pro-type-const-cast,readability-identifier-naming,modernize-raw-string-literal,readability-container-size-empty,bugprone-command-processor,readability-use-std-min-max,cppcoreguidelines-avoid-non-const-global-variables,bugprone-misplaced-widening-cast,readability-misleading-indentation,cert-env33-c,performance-unnecessary-copy-initialization,readability-named-parameter,readability-isolate-declaration,cert-err34-c,modernize-avoid-variadic-functions,cppcoreguidelines-pro-bounds-constant-array-index)
