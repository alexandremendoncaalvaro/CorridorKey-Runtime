#include <catch2/catch_all.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <vector>

#include "app/runtime_diagnostics.hpp"

using namespace corridorkey;
using namespace corridorkey::app;

namespace {

void touch_file(const std::filesystem::path& path, const std::string& contents = "ok") {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    REQUIRE(stream.is_open());
    stream << contents;
}

}  // namespace

TEST_CASE("windows TensorRT probes respect supported VRAM tiers",
          "[unit][doctor][regression]") {
    SECTION("10 GB probes include 1536 and below") {
        DeviceInfo device{"RTX 3080", 10240, Backend::TensorRT, 0};
        auto models = windows_probe_models_for_backend(Backend::TensorRT, device);
        const std::vector<std::string> expected_models{"corridorkey_fp16_1536.onnx",
                                                       "corridorkey_fp16_1024.onnx",
                                                       "corridorkey_fp16_512.onnx"};
        REQUIRE(models == expected_models);
    }

    SECTION("16 GB caps probes at 1536 and below") {
        DeviceInfo device{"RTX 4080", 16384, Backend::TensorRT, 0};
        auto models = windows_probe_models_for_backend(Backend::TensorRT, device);
        const std::vector<std::string> expected_models{"corridorkey_fp16_1536.onnx",
                                                       "corridorkey_fp16_1024.onnx",
                                                       "corridorkey_fp16_512.onnx"};
        REQUIRE(models == expected_models);
    }

    SECTION("24 GB keeps the full packaged FP16 probe ladder") {
        DeviceInfo device{"RTX 4090", 24576, Backend::TensorRT, 0};
        auto models = windows_probe_models_for_backend(Backend::TensorRT, device);
        const std::vector<std::string> expected_models{"corridorkey_fp16_2048.onnx",
                                                       "corridorkey_fp16_1536.onnx",
                                                       "corridorkey_fp16_1024.onnx",
                                                       "corridorkey_fp16_512.onnx"};
        REQUIRE(models == expected_models);
    }

    SECTION("windows universal keeps the public ladder on 512 and 1024") {
        DeviceInfo smaller_device{"AMD Radeon", 8192, Backend::DirectML, 0};
        auto smaller_models = windows_probe_models_for_backend(Backend::DirectML, smaller_device);
        REQUIRE(smaller_models ==
                std::vector<std::string>{"corridorkey_fp16_512.onnx",
                                         "corridorkey_int8_512.onnx"});

        DeviceInfo larger_device{"AMD Radeon", 16384, Backend::DirectML, 0};
        auto larger_models = windows_probe_models_for_backend(Backend::DirectML, larger_device);
        REQUIRE(larger_models ==
                std::vector<std::string>{"corridorkey_fp16_1024.onnx",
                                         "corridorkey_fp16_512.onnx",
                                         "corridorkey_int8_512.onnx"});
    }
}

TEST_CASE("preferred Windows probe prioritizes strict TensorRT success",
          "[unit][doctor][regression]") {
    nlohmann::json probes = nlohmann::json::array(
        {{{"backend", "winml"},
          {"model", "corridorkey_fp16_1024.onnx"},
          {"requested_resolution", 1024},
          {"session_create_ok", true},
          {"frame_execute_ok", true},
          {"fallback_used", false}},
         {{"backend", "tensorrt"},
          {"model", "corridorkey_fp16_1536.onnx"},
          {"requested_resolution", 1536},
          {"session_create_ok", true},
          {"frame_execute_ok", true},
          {"fallback_used", false}}});

    auto preferred = preferred_windows_probe(probes);

    REQUIRE(preferred.has_value());
    REQUIRE(preferred->at("backend") == "tensorrt");
    REQUIRE(preferred->at("model") == "corridorkey_fp16_1536.onnx");
}

TEST_CASE("preferred Windows probe ignores probes that used fallback",
          "[unit][doctor][regression]") {
    nlohmann::json probes = nlohmann::json::array(
        {{{"backend", "tensorrt"},
          {"model", "corridorkey_fp16_2048.onnx"},
          {"requested_resolution", 2048},
          {"session_create_ok", true},
          {"frame_execute_ok", true},
          {"fallback_used", true}},
         {{"backend", "winml"},
          {"model", "corridorkey_fp16_1024.onnx"},
          {"requested_resolution", 1024},
          {"session_create_ok", true},
          {"frame_execute_ok", true},
          {"fallback_used", false}}});

    auto preferred = preferred_windows_probe(probes);

    REQUIRE(preferred.has_value());
    REQUIRE(preferred->at("backend") == "winml");
    REQUIRE(is_successful_windows_probe(*preferred));
}

TEST_CASE("doctor bundle inspection recognizes Windows OFX DirectML layout",
          "[unit][doctor][regression]") {
    auto temp_dir = std::filesystem::temp_directory_path() / "corridorkey-doctor-ofx-layout";
    std::filesystem::remove_all(temp_dir);

    const auto win64_dir = temp_dir / "Contents" / "Win64";
    const auto models_dir = temp_dir / "Contents" / "Resources" / "models";

    for (const auto& filename :
         {"corridorkey_int8_512.onnx", "corridorkey_int8_768.onnx", "corridorkey_int8_1024.onnx",
          "corridorkey_fp16_512.onnx", "corridorkey_fp16_768.onnx", "corridorkey_fp16_1024.onnx",
          "corridorkey_fp16_1536.onnx", "corridorkey_fp16_2048.onnx"}) {
        touch_file(models_dir / filename);
    }

    for (const auto& filename :
         {"corridorkey.exe", "CorridorKey.ofx", "onnxruntime.dll",
          "onnxruntime_providers_shared.dll", "DirectML.dll"}) {
        touch_file(win64_dir / filename);
    }

    const nlohmann::json inventory = {
        {"package_type", "ofx_bundle"},
        {"model_profile", "windows-universal"},
        {"bundle_track", "dml"},
        {"release_label", "Windows DirectML"},
        {"optimization_profile_id", "windows-directml"},
        {"optimization_profile_label", "Windows DirectML"},
        {"backend_intent", "dml"},
        {"fallback_policy", "experimental_gpu_then_cpu_tolerant_workflows"},
        {"warmup_policy", "provider_specific_session_warmup"},
        {"certification_tier", "experimental"},
        {"unrestricted_quality_attempt", false},
        {"expected_models",
         {"corridorkey_int8_512.onnx", "corridorkey_int8_768.onnx", "corridorkey_int8_1024.onnx",
          "corridorkey_fp16_512.onnx", "corridorkey_fp16_768.onnx", "corridorkey_fp16_1024.onnx",
          "corridorkey_fp16_1536.onnx", "corridorkey_fp16_2048.onnx"}},
        {"present_models",
         {"corridorkey_int8_512.onnx", "corridorkey_int8_768.onnx", "corridorkey_int8_1024.onnx",
          "corridorkey_fp16_512.onnx", "corridorkey_fp16_768.onnx", "corridorkey_fp16_1024.onnx",
          "corridorkey_fp16_1536.onnx", "corridorkey_fp16_2048.onnx"}},
        {"missing_models", nlohmann::json::array()},
        {"compiled_context_models", nlohmann::json::array()},
        {"expected_compiled_context_models", nlohmann::json::array()},
        {"missing_compiled_context_models", nlohmann::json::array()},
        {"compiled_context_complete", true},
    };
    std::ofstream(temp_dir / "model_inventory.json") << inventory.dump(2);

    const auto report = inspect_bundle_for_diagnostics(models_dir, win64_dir / "corridorkey.exe");

    REQUIRE(report["layout_kind"] == "windows_ofx");
    REQUIRE(report["packaged_layout_detected"].get<bool>());
    REQUIRE(report["models_dir_matches_bundle"].get<bool>());
    REQUIRE(report["bundle_track"] == "dml");
    REQUIRE(report["directml_runtime_found"].get<bool>());
    REQUIRE(report["runtime_backend_bundle_ready"].get<bool>());
    REQUIRE(report["healthy"].get<bool>());

    std::filesystem::remove_all(temp_dir);
}

TEST_CASE("doctor bundle inspection reports packaged TensorRT context models",
          "[unit][doctor][regression]") {
    auto temp_dir = std::filesystem::temp_directory_path() / "corridorkey-doctor-rtx-context";
    std::filesystem::remove_all(temp_dir);

    const auto win64_dir = temp_dir / "Contents" / "Win64";
    const auto models_dir = temp_dir / "Contents" / "Resources" / "models";

    for (const auto& filename :
         {"corridorkey_int8_512.onnx", "corridorkey_int8_768.onnx", "corridorkey_int8_1024.onnx",
          "corridorkey_fp16_512.onnx", "corridorkey_fp16_512_ctx.onnx",
          "corridorkey_fp16_768.onnx", "corridorkey_fp16_1024.onnx",
          "corridorkey_fp16_1024_ctx.onnx", "corridorkey_fp16_1536.onnx",
          "corridorkey_fp16_2048.onnx"}) {
        touch_file(models_dir / filename);
    }

    for (const auto& filename :
         {"corridorkey.exe", "CorridorKey.ofx", "onnxruntime.dll",
          "onnxruntime_providers_shared.dll", "onnxruntime_providers_nv_tensorrt_rtx.dll"}) {
        touch_file(win64_dir / filename);
    }

    const auto report = inspect_bundle_for_diagnostics(models_dir, win64_dir / "corridorkey.exe");
    const auto compiled_context_models =
        report["compiled_context_models"].get<std::vector<std::string>>();

    REQUIRE(report["bundle_track"] == "rtx");
    REQUIRE(report["compiled_context_models"].is_array());
    REQUIRE(compiled_context_models.size() == 2);
    REQUIRE(std::find(compiled_context_models.begin(), compiled_context_models.end(),
                      "corridorkey_fp16_512_ctx.onnx") != compiled_context_models.end());
    REQUIRE(std::find(compiled_context_models.begin(), compiled_context_models.end(),
                      "corridorkey_fp16_1024_ctx.onnx") != compiled_context_models.end());

    std::filesystem::remove_all(temp_dir);
}

TEST_CASE("doctor bundle inspection honors packaged model inventory for RTX lite bundles",
          "[unit][doctor][regression]") {
    auto temp_dir = std::filesystem::temp_directory_path() / "corridorkey-doctor-rtx-lite";
    std::filesystem::remove_all(temp_dir);

    const auto bundle_dir = temp_dir / "CorridorKey.ofx.bundle";
    const auto win64_dir = bundle_dir / "Contents" / "Win64";
    const auto models_dir = bundle_dir / "Contents" / "Resources" / "models";

    for (const auto& filename :
         {"corridorkey_int8_512.onnx", "corridorkey_int8_768.onnx", "corridorkey_int8_1024.onnx",
          "corridorkey_fp16_512.onnx", "corridorkey_fp16_512_ctx.onnx",
          "corridorkey_fp16_1024.onnx", "corridorkey_fp16_1024_ctx.onnx"}) {
        touch_file(models_dir / filename);
    }

    for (const auto& filename :
         {"corridorkey.exe", "CorridorKey.ofx", "onnxruntime.dll",
          "onnxruntime_providers_shared.dll", "onnxruntime_providers_nv_tensorrt_rtx.dll",
          "cudart64_12.dll", "tensorrt_rtx_1_2.dll", "tensorrt_onnxparser_rtx_1_2.dll"}) {
        touch_file(win64_dir / filename);
    }

    const nlohmann::json inventory = {
        {"package_type", "ofx_bundle"},
        {"model_profile", "rtx-lite"},
        {"bundle_track", "rtx"},
        {"release_label", "Windows RTX Lite"},
        {"optimization_profile_id", "windows-rtx-lite"},
        {"optimization_profile_label", "Windows RTX Lite"},
        {"backend_intent", "tensorrt"},
        {"fallback_policy", "conservative_safe_quality_ceiling"},
        {"warmup_policy", "precompiled_context_or_first_run_compile"},
        {"certification_tier", "validated_ladder_through_1024"},
        {"unrestricted_quality_attempt", false},
        {"expected_models",
         {"corridorkey_fp16_512.onnx", "corridorkey_fp16_1024.onnx", "corridorkey_int8_512.onnx",
          "corridorkey_int8_768.onnx", "corridorkey_int8_1024.onnx"}},
        {"present_models",
         {"corridorkey_fp16_512.onnx", "corridorkey_fp16_1024.onnx", "corridorkey_int8_512.onnx",
          "corridorkey_int8_768.onnx", "corridorkey_int8_1024.onnx"}},
        {"missing_models", nlohmann::json::array()},
        {"compiled_context_models",
         nlohmann::json::array({"corridorkey_fp16_512_ctx.onnx",
                                "corridorkey_fp16_1024_ctx.onnx"})},
        {"expected_compiled_context_models",
         nlohmann::json::array({"corridorkey_fp16_512_ctx.onnx",
                                "corridorkey_fp16_1024_ctx.onnx"})},
        {"missing_compiled_context_models", nlohmann::json::array()},
        {"compiled_context_complete", true},
    };
    std::filesystem::create_directories(bundle_dir);
    std::ofstream(bundle_dir / "model_inventory.json") << inventory.dump(2);

    const auto report = inspect_bundle_for_diagnostics(models_dir, win64_dir / "corridorkey.exe");

    REQUIRE(report["bundle_track"] == "rtx");
    REQUIRE(report["model_profile"] == "rtx-lite");
    REQUIRE(report["model_inventory_contract_complete"].get<bool>());
    REQUIRE(report["optimization_profile_id"] == "windows-rtx-lite");
    REQUIRE(report["healthy"].get<bool>());

    const auto packaged_models = report["packaged_models"];
    REQUIRE(packaged_models.is_array());
    REQUIRE(packaged_models.size() == 5);
    for (const auto& entry : packaged_models) {
        REQUIRE(entry["found"].get<bool>());
    }

    std::filesystem::remove_all(temp_dir);
}

TEST_CASE("doctor bundle inspection marks RTX bundles unhealthy when compiled contexts are missing",
          "[unit][doctor][regression]") {
    auto temp_dir = std::filesystem::temp_directory_path() / "corridorkey-doctor-rtx-missing-ctx";
    std::filesystem::remove_all(temp_dir);

    const auto bundle_dir = temp_dir / "CorridorKey.ofx.bundle";
    const auto win64_dir = bundle_dir / "Contents" / "Win64";
    const auto models_dir = bundle_dir / "Contents" / "Resources" / "models";

    for (const auto& filename :
         {"corridorkey_fp16_512.onnx", "corridorkey_fp16_1024.onnx", "corridorkey_int8_512.onnx",
          "corridorkey_int8_768.onnx", "corridorkey_int8_1024.onnx"}) {
        touch_file(models_dir / filename);
    }

    for (const auto& filename :
         {"corridorkey.exe", "CorridorKey.ofx", "onnxruntime.dll",
          "onnxruntime_providers_shared.dll", "onnxruntime_providers_nv_tensorrt_rtx.dll",
          "cudart64_12.dll", "tensorrt_rtx_1_2.dll", "tensorrt_onnxparser_rtx_1_2.dll"}) {
        touch_file(win64_dir / filename);
    }

    const nlohmann::json inventory = {
        {"package_type", "ofx_bundle"},
        {"model_profile", "rtx-lite"},
        {"bundle_track", "rtx"},
        {"release_label", "Windows RTX Lite"},
        {"optimization_profile_id", "windows-rtx-lite"},
        {"optimization_profile_label", "Windows RTX Lite"},
        {"backend_intent", "tensorrt"},
        {"fallback_policy", "conservative_safe_quality_ceiling"},
        {"warmup_policy", "precompiled_context_or_first_run_compile"},
        {"certification_tier", "validated_ladder_through_1024"},
        {"unrestricted_quality_attempt", false},
        {"expected_models",
         {"corridorkey_fp16_512.onnx", "corridorkey_fp16_1024.onnx", "corridorkey_int8_512.onnx",
          "corridorkey_int8_768.onnx", "corridorkey_int8_1024.onnx"}},
        {"present_models",
         {"corridorkey_fp16_512.onnx", "corridorkey_fp16_1024.onnx", "corridorkey_int8_512.onnx",
          "corridorkey_int8_768.onnx", "corridorkey_int8_1024.onnx"}},
        {"missing_models", nlohmann::json::array()},
        {"compiled_context_models", nlohmann::json::array()},
        {"expected_compiled_context_models",
         nlohmann::json::array({"corridorkey_fp16_512_ctx.onnx",
                                "corridorkey_fp16_1024_ctx.onnx"})},
        {"missing_compiled_context_models",
         nlohmann::json::array({"corridorkey_fp16_512_ctx.onnx",
                                "corridorkey_fp16_1024_ctx.onnx"})},
        {"compiled_context_complete", false},
    };
    std::filesystem::create_directories(bundle_dir);
    std::ofstream(bundle_dir / "model_inventory.json") << inventory.dump(2);

    const auto report = inspect_bundle_for_diagnostics(models_dir, win64_dir / "corridorkey.exe");

    REQUIRE(report["bundle_track"] == "rtx");
    REQUIRE(report["compiled_context_complete"] == false);
    REQUIRE(report["healthy"] == false);

    std::filesystem::remove_all(temp_dir);
}

TEST_CASE("bundle diagnostics expose RTX inventory contract metadata",
          "[unit][doctor][regression]") {
#if !defined(_WIN32)
    SUCCEED("Windows RTX bundle diagnostics are only applicable on Windows.");
#else
    const auto temp_root =
        std::filesystem::temp_directory_path() / "corridorkey-runtime-diagnostics-fixture";
    std::filesystem::remove_all(temp_root);

    const auto bundle_root = temp_root / "CorridorKey.ofx.bundle";
    const auto win64_dir = bundle_root / "Contents" / "Win64";
    const auto models_dir = bundle_root / "Contents" / "Resources" / "models";

    touch_file(win64_dir / "corridorkey.exe");
    touch_file(win64_dir / "CorridorKey.ofx");
    touch_file(win64_dir / "onnxruntime.dll");
    touch_file(win64_dir / "onnxruntime_providers_shared.dll");
    touch_file(win64_dir / "onnxruntime_providers_nv_tensorrt_rtx.dll");
    touch_file(win64_dir / "tensorrt_rtx_1.dll");
    touch_file(win64_dir / "tensorrt_onnxparser_rtx_1.dll");
    touch_file(win64_dir / "cudart64_12.dll");

    touch_file(models_dir / "corridorkey_fp16_512.onnx");
    touch_file(models_dir / "corridorkey_fp16_512_ctx.onnx");

    const nlohmann::json inventory = {
        {"package_type", "ofx_bundle"},
        {"model_profile", "rtx-full"},
        {"bundle_track", "rtx"},
        {"release_label", "Windows RTX Full"},
        {"optimization_profile_id", "windows-rtx-full"},
        {"optimization_profile_label", "Windows RTX Full"},
        {"backend_intent", "tensorrt"},
        {"fallback_policy", "attempt_packaged_quality_then_runtime_failure"},
        {"warmup_policy", "precompiled_context_or_first_run_compile"},
        {"certification_tier", "packaged_fp16_ladder_through_2048"},
        {"unrestricted_quality_attempt", true},
        {"expected_models", nlohmann::json::array({"corridorkey_fp16_512.onnx"})},
        {"present_models", nlohmann::json::array({"corridorkey_fp16_512.onnx"})},
        {"missing_models", nlohmann::json::array()},
        {"compiled_context_models", nlohmann::json::array({"corridorkey_fp16_512_ctx.onnx"})},
        {"expected_compiled_context_models",
         nlohmann::json::array({"corridorkey_fp16_512_ctx.onnx"})},
        {"missing_compiled_context_models", nlohmann::json::array()},
        {"compiled_context_complete", true},
    };
    touch_file(bundle_root / "model_inventory.json", inventory.dump(2));

    const auto diagnostics = inspect_bundle_for_diagnostics(models_dir, win64_dir / "corridorkey.exe");

    REQUIRE(diagnostics["packaged_layout_detected"].get<bool>());
    REQUIRE(diagnostics["healthy"].get<bool>());
    REQUIRE(diagnostics["model_profile"] == "rtx-full");
    REQUIRE(diagnostics["bundle_track"] == "rtx");
    REQUIRE(diagnostics["optimization_profile_id"] == "windows-rtx-full");
    REQUIRE(diagnostics["certification_tier"] == "packaged_fp16_ladder_through_2048");
    REQUIRE(diagnostics["unrestricted_quality_attempt"].get<bool>());
    REQUIRE(diagnostics["compiled_context_complete"].get<bool>());
    REQUIRE(diagnostics["model_inventory_contract_complete"].get<bool>());
    REQUIRE(diagnostics["model_inventory"]["contract_complete"].get<bool>());
    REQUIRE(diagnostics["model_inventory"]["expected_compiled_context_models"].size() == 1);

    std::filesystem::remove_all(temp_root);
#endif
}
