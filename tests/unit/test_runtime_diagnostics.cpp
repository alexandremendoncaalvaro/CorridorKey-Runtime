#include <catch2/catch_all.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <vector>

#include "app/runtime_diagnostics.hpp"

using namespace corridorkey;
using namespace corridorkey::app;

namespace {

void touch_file(const std::filesystem::path& path) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    REQUIRE(stream.is_open());
    stream << "ok";
}

}  // namespace

TEST_CASE("windows TensorRT probes respect supported VRAM tiers",
          "[unit][doctor][regression]") {
    SECTION("10 GB probes include 1536 and below") {
        DeviceInfo device{"RTX 3080", 10240, Backend::TensorRT, 0};
        auto models = windows_probe_models_for_backend(Backend::TensorRT, device);
        const std::vector<std::string> expected_models{"corridorkey_fp16_1536.onnx",
                                                       "corridorkey_fp16_1024.onnx",
                                                       "corridorkey_fp16_768.onnx",
                                                       "corridorkey_fp16_512.onnx"};
        REQUIRE(models == expected_models);
    }

    SECTION("16 GB caps probes at 1536 and below") {
        DeviceInfo device{"RTX 4080", 16384, Backend::TensorRT, 0};
        auto models = windows_probe_models_for_backend(Backend::TensorRT, device);
        const std::vector<std::string> expected_models{"corridorkey_fp16_1536.onnx",
                                                       "corridorkey_fp16_1024.onnx",
                                                       "corridorkey_fp16_768.onnx",
                                                       "corridorkey_fp16_512.onnx"};
        REQUIRE(models == expected_models);
    }

    SECTION("24 GB keeps the full packaged FP16 probe ladder") {
        DeviceInfo device{"RTX 4090", 24576, Backend::TensorRT, 0};
        auto models = windows_probe_models_for_backend(Backend::TensorRT, device);
        const std::vector<std::string> expected_models{"corridorkey_fp16_2048.onnx",
                                                       "corridorkey_fp16_1536.onnx",
                                                       "corridorkey_fp16_1024.onnx",
                                                       "corridorkey_fp16_768.onnx",
                                                       "corridorkey_fp16_512.onnx"};
        REQUIRE(models == expected_models);
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

    const auto report = inspect_bundle_for_diagnostics(models_dir, win64_dir / "corridorkey.exe");

    REQUIRE(report["layout_kind"] == "windows_ofx");
    REQUIRE(report["packaged_layout_detected"].get<bool>());
    REQUIRE(report["models_dir_matches_bundle"].get<bool>());
    REQUIRE(report["bundle_track"] == "directml");
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
          "corridorkey_fp16_512.onnx", "corridorkey_fp16_768.onnx", "corridorkey_fp16_1024.onnx"}) {
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
        {"expected_models",
         {"corridorkey_fp16_512.onnx", "corridorkey_fp16_768.onnx",
          "corridorkey_fp16_1024.onnx", "corridorkey_int8_512.onnx",
          "corridorkey_int8_768.onnx", "corridorkey_int8_1024.onnx"}},
        {"present_models",
         {"corridorkey_fp16_512.onnx", "corridorkey_fp16_768.onnx",
          "corridorkey_fp16_1024.onnx", "corridorkey_int8_512.onnx",
          "corridorkey_int8_768.onnx", "corridorkey_int8_1024.onnx"}},
        {"missing_models", nlohmann::json::array()}};
    std::filesystem::create_directories(bundle_dir);
    std::ofstream(bundle_dir / "model_inventory.json") << inventory.dump(2);

    const auto report = inspect_bundle_for_diagnostics(models_dir, win64_dir / "corridorkey.exe");

    REQUIRE(report["bundle_track"] == "rtx");
    REQUIRE(report["model_profile"] == "rtx-lite");
    REQUIRE(report["healthy"].get<bool>());

    const auto packaged_models = report["packaged_models"];
    REQUIRE(packaged_models.is_array());
    REQUIRE(packaged_models.size() == 6);
    for (const auto& entry : packaged_models) {
        REQUIRE(entry["found"].get<bool>());
    }

    std::filesystem::remove_all(temp_dir);
}
