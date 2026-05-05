#include <catch2/catch_all.hpp>
#include <filesystem>
#include <fstream>

#include "app/job_orchestrator.hpp"

using namespace corridorkey::app;

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

TEST_CASE("doctor report exposes operational health sections", "[integration][doctor]") {
    auto models_dir = std::filesystem::path(PROJECT_ROOT) / "models";
    auto report = JobOrchestrator::run_doctor(models_dir);

    REQUIRE(report.contains("executable"));
    REQUIRE(report.contains("bundle"));
    REQUIRE(report.contains("video"));
    REQUIRE(report.contains("cache"));
    REQUIRE(report.contains("coreml"));
    REQUIRE(report.contains("mlx"));
    REQUIRE(report.contains("windows_universal"));
    REQUIRE(report.contains("summary"));
    REQUIRE(report["models"].is_array());
    REQUIRE(report["presets"].is_array());

    REQUIRE(report["bundle"].contains("healthy"));
    REQUIRE(report["bundle"].contains("signature"));
    REQUIRE(report["bundle"].contains("core_library_found"));
    REQUIRE(report["bundle"].contains("core_library_referenced"));
    REQUIRE(report["bundle"].contains("mlx_library_found"));
    REQUIRE(report["bundle"].contains("mlx_library_referenced"));
    REQUIRE(report["bundle"].contains("mlx_metallib_found"));
    REQUIRE(report["bundle"].contains("mlx_bridge_present"));
    REQUIRE(report["bundle"]["mlx_bridge_artifacts"].is_array());
    REQUIRE(report["video"].contains("default_mode"));
    REQUIRE(report["video"].contains("default_container"));
    REQUIRE(report["video"].contains("default_encoder"));
    REQUIRE(report["video"].contains("lossless_available"));
    REQUIRE(report["video"].contains("lossless_unavailable_reason"));
    REQUIRE(report["video"]["supported_encoders"].is_array());
    REQUIRE(report["video"].contains("portable_h264_available"));
    REQUIRE(report["cache"].contains("writable"));
    REQUIRE(report["cache"].contains("configured_path"));
    REQUIRE(report["cache"].contains("selected_path"));
    REQUIRE(report["cache"].contains("fallback_in_use"));
    REQUIRE(report["cache"]["candidates"].is_array());
    REQUIRE(report["cache"].contains("optimized_models_dir"));
    REQUIRE(report["cache"].contains("optimized_model_count"));
    REQUIRE(report["cache"]["optimized_models"].is_array());
    REQUIRE(report["cache"].contains("coreml_ep_cache_dir"));
    REQUIRE(report["cache"].contains("tensorrt_rtx_cache_dir"));
    REQUIRE(report["coreml"].contains("applicable"));
    REQUIRE(report["coreml"].contains("available"));
    REQUIRE(report["coreml"].contains("probe_policy"));
    REQUIRE(report["coreml"].contains("models"));
    REQUIRE(report["coreml"]["models"].is_array());
    if (!report["coreml"]["models"].empty()) {
        auto entry = report["coreml"]["models"].front();
        REQUIRE(entry.contains("filename"));
        REQUIRE(entry.contains("found"));
        REQUIRE(entry.contains("usable"));
        REQUIRE(entry.contains("artifact_status"));
        REQUIRE(entry.contains("full_graph_supported"));
        REQUIRE(entry.contains("error"));
    }
    REQUIRE(report["mlx"].contains("applicable"));
    REQUIRE(report["mlx"].contains("probe_available"));
    REQUIRE(report["mlx"].contains("primary_pack_ready"));
    REQUIRE(report["mlx"].contains("bridge_ready"));
    REQUIRE(report["mlx"].contains("integration_mode"));
    REQUIRE(report["mlx"].contains("backend_integrated"));
    REQUIRE(report["mlx"].contains("models"));
    REQUIRE(report["mlx"].contains("primary_artifacts"));
    REQUIRE(report["mlx"].contains("bridge_artifacts"));
    REQUIRE(report["mlx"]["integration_mode"] == "mlx_pack_with_bridge_exports");
    REQUIRE(report["mlx"]["models"].is_array());
    if (!report["mlx"]["primary_artifacts"].empty()) {
        auto entry = report["mlx"]["primary_artifacts"].front();
        REQUIRE(entry.contains("filename"));
        REQUIRE(entry.contains("found"));
        REQUIRE(entry.contains("usable"));
        REQUIRE(entry.contains("artifact_family"));
        REQUIRE(entry.contains("artifact_status"));
        REQUIRE(entry.contains("recommended_backend"));
        REQUIRE(entry.contains("probe_ready"));
        REQUIRE(entry.contains("error"));
    }
    if (report["mlx"]["bridge_ready"].get<bool>()) {
        REQUIRE(report["mlx"]["backend_integrated"].get<bool>());
    }
    REQUIRE(report["windows_universal"].contains("applicable"));
    REQUIRE(report["windows_universal"].contains("gpu_detected"));
    REQUIRE(report["windows_universal"].contains("provider_available"));
    REQUIRE(report["windows_universal"].contains("runtime_cache_ready"));
    REQUIRE(report["windows_universal"].contains("packaged_models"));
    REQUIRE(report["windows_universal"].contains("compiled_context_models"));
    REQUIRE(report["windows_universal"].contains("execution_probe_policy"));
    REQUIRE(report["windows_universal"].contains("execution_probes"));
    REQUIRE(report["windows_universal"].contains("recommended_backend"));
    REQUIRE(report["windows_universal"].contains("recommended_model"));
    REQUIRE(report["windows_universal"].contains("recommended_backend_reason"));
    REQUIRE(report["windows_universal"]["execution_probes"].is_array());
    if (!report["windows_universal"]["execution_probes"].empty()) {
        auto entry = report["windows_universal"]["execution_probes"].front();
        REQUIRE(entry.contains("backend"));
        REQUIRE(entry.contains("model"));
        REQUIRE(entry.contains("requested_resolution"));
        REQUIRE(entry.contains("session_create_ok"));
        REQUIRE(entry.contains("frame_execute_ok"));
        REQUIRE(entry.contains("fallback_used"));
    }
    REQUIRE(report["summary"].contains("coreml_healthy"));
    REQUIRE(report["summary"].contains("apple_acceleration_probe_ready"));
    REQUIRE(report["summary"].contains("apple_acceleration_bridge_ready"));
    REQUIRE(report["summary"].contains("apple_acceleration_backend_integrated"));
    REQUIRE(report["summary"].contains("apple_acceleration_healthy"));
    REQUIRE(report["summary"].contains("windows_universal_provider_ready"));
    REQUIRE(report["summary"].contains("windows_universal_execution_ready"));
    REQUIRE(report["summary"].contains("windows_universal_packaged_models_present"));
    REQUIRE(report["summary"].contains("windows_universal_preferred_backend"));
    REQUIRE(report["summary"].contains("windows_universal_preferred_model"));
    REQUIRE(report["summary"].contains("windows_universal_healthy"));
    REQUIRE(report["summary"].contains("validated_models_present"));
}

TEST_CASE("doctor report ignores macOS metadata sidecars in packaged models",
          "[integration][doctor]") {
    auto source_models_dir = std::filesystem::path(PROJECT_ROOT) / "models";
    auto temp_dir = std::filesystem::temp_directory_path() / "corridorkey-doctor-sidecars";
    std::filesystem::remove_all(temp_dir);
    std::filesystem::create_directories(temp_dir);

    for (const auto& filename :
         {"corridorkey_fp16_512.onnx", "corridorkey_mlx.safetensors",
          "corridorkey_mlx_bridge_512.mlxfn", "corridorkey_mlx_bridge_1024.mlxfn"}) {
        std::filesystem::create_symlink(source_models_dir / filename, temp_dir / filename);
    }

    {
        std::ofstream sidecar(temp_dir / "._corridorkey_mlx_bridge_512.mlxfn",
                              std::ios::binary | std::ios::trunc);
        REQUIRE(sidecar.is_open());
        sidecar << "metadata";
    }

    auto report = JobOrchestrator::run_doctor(temp_dir);

    for (const auto& entry : report["mlx"]["bridge_artifacts"]) {
        REQUIRE(entry["filename"].get<std::string>().rfind("._", 0) != 0);
    }
    for (const auto& entry : report["mlx"]["models"]) {
        REQUIRE(entry["filename"].get<std::string>().rfind("._", 0) != 0);
    }
    for (const auto& entry : report["coreml"]["models"]) {
        REQUIRE(std::filesystem::path(entry["filename"].get<std::string>()).extension() == ".onnx");
    }

    std::filesystem::remove_all(temp_dir);
}

// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,readability-identifier-length,bugprone-easily-swappable-parameters,readability-function-cognitive-complexity,readability-function-size,cppcoreguidelines-avoid-magic-numbers,modernize-use-designated-initializers,readability-uppercase-literal-suffix,readability-math-missing-parentheses,modernize-use-ranges,modernize-use-starts-ends-with,modernize-use-emplace,modernize-use-auto,modernize-loop-convert,modernize-avoid-c-style-cast,modernize-return-braced-init-list,readability-implicit-bool-conversion,readability-container-contains,readability-redundant-member-init,readability-redundant-string-init,bugprone-narrowing-conversions,cppcoreguidelines-narrowing-conversions,readability-avoid-nested-conditional-operator,modernize-use-nodiscard,readability-make-member-function-const,cppcoreguidelines-pro-type-reinterpret-cast,bugprone-implicit-widening-of-multiplication-result,readability-redundant-inline-specifier,cppcoreguidelines-prefer-member-initializer,performance-unnecessary-value-param,readability-use-concise-preprocessor-directives,readability-else-after-return,readability-string-compare,bugprone-exception-escape,cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays,bugprone-branch-clone,cert-err33-c,readability-redundant-declaration,readability-qualified-auto,modernize-use-scoped-lock,modernize-use-bool-literals,cppcoreguidelines-init-variables,cppcoreguidelines-special-member-functions,cppcoreguidelines-owning-memory,cppcoreguidelines-no-malloc,performance-enum-size,performance-avoid-endl,bugprone-unchecked-optional-access,bugprone-unchecked-string-to-number-conversion,cppcoreguidelines-pro-type-cstyle-cast,modernize-use-using,modernize-use-integer-sign-comparison,cert-dcl50-cpp,cppcoreguidelines-pro-type-const-cast,readability-identifier-naming,modernize-raw-string-literal,readability-container-size-empty,bugprone-command-processor,readability-use-std-min-max,cppcoreguidelines-avoid-non-const-global-variables,bugprone-misplaced-widening-cast,readability-misleading-indentation,cert-env33-c,performance-unnecessary-copy-initialization,readability-named-parameter,readability-isolate-declaration,cert-err34-c,modernize-avoid-variadic-functions,cppcoreguidelines-pro-bounds-constant-array-index)
