#include <catch2/catch_all.hpp>

#include "app/job_orchestrator.hpp"

using namespace corridorkey;
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

TEST_CASE("doctor summary honors packaged bundle inventory for Windows RTX",
          "[unit][doctor][regression]") {
    nlohmann::json report;
    report["system"]["capabilities"]["platform"] = "windows";
    report["bundle"]["healthy"] = true;
    report["bundle"]["packaged_layout_detected"] = true;
    report["bundle"]["packaged_models"] =
        nlohmann::json::array({{{"filename", "corridorkey_fp16_512.onnx"}, {"found", true}},
                               {{"filename", "corridorkey_fp16_1024.onnx"}, {"found", true}},
                               {{"filename", "corridorkey_fp16_1536.onnx"}, {"found", true}},
                               {{"filename", "corridorkey_fp16_2048.onnx"}, {"found", true}}});
    report["video"]["healthy"] = true;
    report["cache"]["healthy"] = true;
    report["coreml"]["applicable"] = false;
    report["mlx"]["applicable"] = false;
    report["mlx"]["probe_available"] = false;
    report["mlx"]["primary_pack_ready"] = false;
    report["mlx"]["bridge_ready"] = false;
    report["mlx"]["backend_integrated"] = false;
    report["mlx"]["healthy"] = false;
    report["windows_universal"]["applicable"] = true;
    report["windows_universal"]["provider_available"] = true;
    report["windows_universal"]["backend_integrated"] = true;
    report["windows_universal"]["healthy"] = true;
    report["windows_universal"]["recommended_backend"] = "tensorrt";
    report["windows_universal"]["recommended_model"] = "corridorkey_fp16_512.onnx";
    report["bundle"]["model_inventory_contract_complete"] = true;
    report["bundle"]["model_profile"] = "windows-rtx";
    report["optimization_profile"]["id"] = "windows-rtx";
    report["models"] = nlohmann::json::array({{{"filename", "corridorkey_fp16_512.onnx"},
                                               {"packaged_for_windows", true},
                                               {"found", true},
                                               {"validated_platforms", nlohmann::json::array()},
                                               {"packaged_for_macos", false}},
                                              {{"filename", "corridorkey_fp16_1024.onnx"},
                                               {"packaged_for_windows", true},
                                               {"found", true},
                                               {"validated_platforms", nlohmann::json::array()},
                                               {"packaged_for_macos", false}},
                                              {{"filename", "corridorkey_fp16_1536.onnx"},
                                               {"packaged_for_windows", true},
                                               {"found", false},
                                               {"validated_platforms", nlohmann::json::array()},
                                               {"packaged_for_macos", false}},
                                              {{"filename", "corridorkey_fp16_2048.onnx"},
                                               {"packaged_for_windows", true},
                                               {"found", false},
                                               {"validated_platforms", nlohmann::json::array()},
                                               {"packaged_for_macos", false}}});

    auto summary = summarize_doctor_report(report);

    REQUIRE(summary["validated_models_present"].get<bool>());
    REQUIRE(summary["windows_universal_packaged_models_present"].get<bool>());
    REQUIRE(summary["bundle_inventory_contract_healthy"].get<bool>());
    REQUIRE(summary["packaged_profile_matches_active_profile"].get<bool>());
    REQUIRE(summary["healthy"].get<bool>());
}

TEST_CASE("doctor summary fails when an expected packaged bundle model is missing",
          "[unit][doctor][regression]") {
    nlohmann::json report;
    report["system"]["capabilities"]["platform"] = "windows";
    report["bundle"]["healthy"] = true;
    report["bundle"]["packaged_layout_detected"] = true;
    report["bundle"]["packaged_models"] =
        nlohmann::json::array({{{"filename", "corridorkey_fp16_512.onnx"}, {"found", true}},
                               {{"filename", "corridorkey_fp16_1024.onnx"}, {"found", false}}});
    report["video"]["healthy"] = true;
    report["cache"]["healthy"] = true;
    report["coreml"]["applicable"] = false;
    report["mlx"]["applicable"] = false;
    report["windows_universal"]["applicable"] = true;
    report["windows_universal"]["provider_available"] = true;
    report["windows_universal"]["backend_integrated"] = true;
    report["windows_universal"]["healthy"] = true;
    report["windows_universal"]["recommended_backend"] = "tensorrt";
    report["windows_universal"]["recommended_model"] = "corridorkey_fp16_512.onnx";
    report["bundle"]["model_inventory_contract_complete"] = true;
    report["bundle"]["model_profile"] = "windows-rtx";
    report["optimization_profile"]["id"] = "windows-rtx";
    report["models"] = nlohmann::json::array();

    auto summary = summarize_doctor_report(report);

    REQUIRE_FALSE(summary["validated_models_present"].get<bool>());
    REQUIRE_FALSE(summary["windows_universal_packaged_models_present"].get<bool>());
    REQUIRE_FALSE(summary["healthy"].get<bool>());
}

TEST_CASE("doctor summary reports recommended and certified artifact state",
          "[unit][doctor][regression]") {
    nlohmann::json report;
    report["system"]["capabilities"]["platform"] = "windows";
    report["bundle"]["healthy"] = true;
    report["bundle"]["model_inventory_contract_complete"] = true;
    report["bundle"]["model_profile"] = "windows-rtx";
    report["video"]["healthy"] = true;
    report["cache"]["healthy"] = true;
    report["coreml"]["applicable"] = false;
    report["mlx"]["applicable"] = false;
    report["windows_universal"]["applicable"] = true;
    report["windows_universal"]["healthy"] = true;
    report["optimization_profile"]["id"] = "windows-rtx";
    report["models"] = nlohmann::json::array(
        {{{"filename", "corridorkey_fp16_1024.onnx"},
          {"found", true},
          {"artifact_state",
           {{"certified_for_active_device", true}, {"recommended_for_active_device", true}}}},
         {{"filename", "corridorkey_fp16_1536.onnx"},
          {"found", true},
          {"artifact_state",
           {{"certified_for_active_device", true}, {"recommended_for_active_device", false}}}}});

    auto summary = summarize_doctor_report(report);

    REQUIRE(summary["certified_model_count"].get<std::size_t>() == 2);
    REQUIRE(summary["recommended_model_present"].get<bool>());
    REQUIRE(summary["bundle_inventory_contract_healthy"].get<bool>());
    REQUIRE(summary["packaged_profile_matches_active_profile"].get<bool>());
}

TEST_CASE("doctor summary does not count placeholder artifacts as recommended present",
          "[unit][doctor][regression]") {
    nlohmann::json report;
    report["system"]["capabilities"]["platform"] = "macos";
    report["bundle"]["healthy"] = false;
    report["bundle"]["model_inventory_contract_complete"] = true;
    report["video"]["healthy"] = true;
    report["cache"]["healthy"] = true;
    report["coreml"]["applicable"] = true;
    report["coreml"]["healthy"] = false;
    report["mlx"]["applicable"] = true;
    report["mlx"]["probe_available"] = true;
    report["mlx"]["primary_pack_ready"] = false;
    report["mlx"]["bridge_ready"] = false;
    report["mlx"]["backend_integrated"] = false;
    report["mlx"]["healthy"] = false;
    report["windows_universal"]["applicable"] = false;
    report["optimization_profile"]["id"] = "apple-silicon-mlx";
    report["models"] = nlohmann::json::array(
        {{{"filename", "corridorkey_mlx.safetensors"},
          {"packaged_for_macos", true},
          {"found", true},
          {"usable", false},
          {"validated_platforms", nlohmann::json::array({"macos_apple_silicon"})},
          {"artifact_state", {{"present", false}, {"recommended_for_active_device", false}}}}});

    auto summary = summarize_doctor_report(report);

    REQUIRE_FALSE(summary["validated_models_present"].get<bool>());
    REQUIRE_FALSE(summary["recommended_model_present"].get<bool>());
    REQUIRE_FALSE(summary["healthy"].get<bool>());
}

TEST_CASE("doctor summary fails when packaged RTX profile and active profile diverge",
          "[unit][doctor][regression]") {
    nlohmann::json report;
    report["system"]["capabilities"]["platform"] = "windows";
    report["bundle"]["healthy"] = true;
    report["bundle"]["model_inventory_contract_complete"] = true;
    report["bundle"]["model_profile"] = "windows-rtx";
    report["video"]["healthy"] = true;
    report["cache"]["healthy"] = true;
    report["coreml"]["applicable"] = false;
    report["mlx"]["applicable"] = false;
    report["windows_universal"]["applicable"] = true;
    report["windows_universal"]["healthy"] = true;
    report["optimization_profile"]["id"] = "windows-directml";
    report["models"] = nlohmann::json::array();

    auto summary = summarize_doctor_report(report);

    REQUIRE(summary["bundle_inventory_contract_healthy"].get<bool>());
    REQUIRE_FALSE(summary["packaged_profile_matches_active_profile"].get<bool>());
    REQUIRE_FALSE(summary["healthy"].get<bool>());
}

TEST_CASE("doctor summary reports healthy macOS MLX bundle", "[unit][doctor][regression]") {
    nlohmann::json report;
    report["system"]["capabilities"]["platform"] = "macos";
    report["bundle"]["healthy"] = true;
    report["bundle"]["packaged_layout_detected"] = true;
    report["bundle"]["model_inventory_contract_complete"] = true;
    report["bundle"]["model_profile"] = "apple-silicon-mlx";
    report["bundle"]["packaged_models"] = nlohmann::json::array(
        {{{"filename", "corridorkey_mlx.safetensors"}, {"found", true}},
         {{"filename", "corridorkey_mlx_bridge_512.mlxfn"}, {"found", true}}});
    report["video"]["healthy"] = true;
    report["cache"]["healthy"] = true;
    report["coreml"]["applicable"] = true;
    report["coreml"]["healthy"] = true;
    report["mlx"]["applicable"] = true;
    report["mlx"]["probe_available"] = true;
    report["mlx"]["primary_pack_ready"] = true;
    report["mlx"]["bridge_ready"] = true;
    report["mlx"]["backend_integrated"] = true;
    report["mlx"]["healthy"] = true;
    report["windows_universal"]["applicable"] = false;
    report["optimization_profile"]["id"] = "apple-silicon-mlx";
    report["models"] = nlohmann::json::array(
        {{{"filename", "corridorkey_mlx.safetensors"},
          {"packaged_for_macos", true},
          {"found", true},
          {"validated_platforms", nlohmann::json::array({"macos_apple_silicon"})}},
         {{"filename", "corridorkey_mlx_bridge_512.mlxfn"},
          {"packaged_for_macos", true},
          {"found", true},
          {"validated_platforms", nlohmann::json::array({"macos_apple_silicon"})}}});

    auto summary = summarize_doctor_report(report);

    REQUIRE(summary["validated_models_present"].get<bool>());
    REQUIRE(summary["apple_acceleration_healthy"].get<bool>());
    REQUIRE(summary["bundle_inventory_contract_healthy"].get<bool>());
    REQUIRE(summary["healthy"].get<bool>());
}

TEST_CASE("doctor summary fails when macOS MLX primary pack is missing",
          "[unit][doctor][regression]") {
    nlohmann::json report;
    report["system"]["capabilities"]["platform"] = "macos";
    report["bundle"]["healthy"] = true;
    report["bundle"]["model_inventory_contract_complete"] = true;
    report["bundle"]["model_profile"] = "apple-silicon-mlx";
    report["video"]["healthy"] = true;
    report["cache"]["healthy"] = true;
    report["coreml"]["applicable"] = true;
    report["coreml"]["healthy"] = true;
    report["mlx"]["applicable"] = true;
    report["mlx"]["probe_available"] = true;
    report["mlx"]["primary_pack_ready"] = false;
    report["mlx"]["bridge_ready"] = true;
    report["mlx"]["backend_integrated"] = true;
    report["mlx"]["healthy"] = false;
    report["windows_universal"]["applicable"] = false;
    report["optimization_profile"]["id"] = "apple-silicon-mlx";
    report["models"] = nlohmann::json::array(
        {{{"filename", "corridorkey_mlx.safetensors"},
          {"packaged_for_macos", true},
          {"found", false},
          {"validated_platforms", nlohmann::json::array({"macos_apple_silicon"})}}});

    auto summary = summarize_doctor_report(report);

    REQUIRE_FALSE(summary["validated_models_present"].get<bool>());
    REQUIRE_FALSE(summary["apple_acceleration_healthy"].get<bool>());
    REQUIRE_FALSE(summary["healthy"].get<bool>());
}

TEST_CASE("benchmark phase timings aggregate raw stage timings", "[unit][runtime][regression]") {
    const std::vector<StageTiming> timings = {
        StageTiming{"engine_create", 10.0, 1, 0},
        StageTiming{"benchmark_warmup_frame", 40.0, 2, 2},
        StageTiming{"sequence_infer_batch", 90.0, 3, 3},
        StageTiming{"sequence_write_output", 25.0, 1, 1},
        StageTiming{"job_total", 165.0, 1, 1},
    };

    auto phase_timings = summarize_stage_groups(timings);
    REQUIRE(phase_timings.is_array());
    REQUIRE(phase_timings.size() == 5);
    REQUIRE(phase_timings[0]["name"] == "prepare");
    REQUIRE(phase_timings[0]["total_ms"] == Catch::Approx(10.0));
    REQUIRE(phase_timings[1]["name"] == "warmup_compile");
    REQUIRE(phase_timings[1]["total_ms"] == Catch::Approx(40.0));
    REQUIRE(phase_timings[2]["name"] == "execute");
    REQUIRE(phase_timings[2]["total_ms"] == Catch::Approx(90.0));
    REQUIRE(phase_timings[3]["name"] == "write_output");
    REQUIRE(phase_timings[3]["total_ms"] == Catch::Approx(25.0));
    REQUIRE(phase_timings[4]["name"] == "total");
    REQUIRE(phase_timings[4]["total_ms"] == Catch::Approx(165.0));
}

// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,readability-identifier-length,bugprone-easily-swappable-parameters,readability-function-cognitive-complexity,readability-function-size,cppcoreguidelines-avoid-magic-numbers,modernize-use-designated-initializers,readability-uppercase-literal-suffix,readability-math-missing-parentheses,modernize-use-ranges,modernize-use-starts-ends-with,modernize-use-emplace,modernize-use-auto,modernize-loop-convert,modernize-avoid-c-style-cast,modernize-return-braced-init-list,readability-implicit-bool-conversion,readability-container-contains,readability-redundant-member-init,readability-redundant-string-init,bugprone-narrowing-conversions,cppcoreguidelines-narrowing-conversions,readability-avoid-nested-conditional-operator,modernize-use-nodiscard,readability-make-member-function-const,cppcoreguidelines-pro-type-reinterpret-cast,bugprone-implicit-widening-of-multiplication-result,readability-redundant-inline-specifier,cppcoreguidelines-prefer-member-initializer,performance-unnecessary-value-param,readability-use-concise-preprocessor-directives,readability-else-after-return,readability-string-compare,bugprone-exception-escape,cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays,bugprone-branch-clone,cert-err33-c,readability-redundant-declaration,readability-qualified-auto,modernize-use-scoped-lock,modernize-use-bool-literals,cppcoreguidelines-init-variables,cppcoreguidelines-special-member-functions,cppcoreguidelines-owning-memory,cppcoreguidelines-no-malloc,performance-enum-size,performance-avoid-endl,bugprone-unchecked-optional-access,bugprone-unchecked-string-to-number-conversion,cppcoreguidelines-pro-type-cstyle-cast,modernize-use-using,modernize-use-integer-sign-comparison,cert-dcl50-cpp,cppcoreguidelines-pro-type-const-cast,readability-identifier-naming,modernize-raw-string-literal,readability-container-size-empty,bugprone-command-processor,readability-use-std-min-max,cppcoreguidelines-avoid-non-const-global-variables,bugprone-misplaced-widening-cast,readability-misleading-indentation,cert-env33-c,performance-unnecessary-copy-initialization,readability-named-parameter,readability-isolate-declaration,cert-err34-c,modernize-avoid-variadic-functions,cppcoreguidelines-pro-bounds-constant-array-index)
