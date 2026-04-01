#include <catch2/catch_all.hpp>
#include <corridorkey/engine.hpp>
#include <cstdlib>
#include <filesystem>
#include <fstream>

#include "app/hardware_profile.hpp"
#include "app/runtime_contracts.hpp"
#include "app/runtime_diagnostics.hpp"

using namespace corridorkey;
using namespace corridorkey::app;

namespace {

struct ScopedModelsDirOverride {
    std::optional<std::string> previous = std::nullopt;

    explicit ScopedModelsDirOverride(const std::filesystem::path& models_dir) {
#if defined(_WIN32)
        char* raw = nullptr;
        size_t length = 0;
        if (_dupenv_s(&raw, &length, "CORRIDORKEY_MODELS_DIR") == 0 && raw != nullptr) {
            previous = std::string(raw, length > 0 ? length - 1 : 0);
            std::free(raw);
        }
        _putenv_s("CORRIDORKEY_MODELS_DIR", models_dir.string().c_str());
#else
        if (const char* value = std::getenv("CORRIDORKEY_MODELS_DIR"); value != nullptr) {
            previous = std::string(value);
        }
        setenv("CORRIDORKEY_MODELS_DIR", models_dir.string().c_str(), 1);
#endif
    }

    ~ScopedModelsDirOverride() {
#if defined(_WIN32)
        if (previous.has_value()) {
            _putenv_s("CORRIDORKEY_MODELS_DIR", previous->c_str());
        } else {
            _putenv_s("CORRIDORKEY_MODELS_DIR", "");
        }
#else
        if (previous.has_value()) {
            setenv("CORRIDORKEY_MODELS_DIR", previous->c_str(), 1);
        } else {
            unsetenv("CORRIDORKEY_MODELS_DIR");
        }
#endif
    }
};

std::filesystem::path write_models_inventory_fixture(const std::string& model_profile) {
    const auto temp_dir =
        std::filesystem::temp_directory_path() / ("corridorkey-model-profile-" + model_profile);
    std::filesystem::remove_all(temp_dir);
    std::filesystem::create_directories(temp_dir / "models");
    std::ofstream(temp_dir / "model_inventory.json")
        << nlohmann::json{{"model_profile", model_profile}}.dump(2);
    return temp_dir / "models";
}

}  // namespace

TEST_CASE("runtime capabilities expose stable diagnostics", "[unit][runtime]") {
    auto capabilities = runtime_capabilities();
    auto devices = list_devices();

    REQUIRE_FALSE(capabilities.platform.empty());
    REQUIRE(capabilities.cpu_fallback_available);
    REQUIRE(capabilities.supported_backends.size() == devices.size());
    if (capabilities.lossless_video_available) {
        REQUIRE(capabilities.default_video_mode == VideoOutputMode::Lossless);
    }
    if (capabilities.lossless_video_available) {
        REQUIRE_FALSE(capabilities.default_video_container.empty());
        REQUIRE_FALSE(capabilities.default_video_encoder.empty());
    }
    auto json = to_json(capabilities);
    REQUIRE(json.contains("mlx_probe_available"));
    REQUIRE(json.contains("default_video_mode"));
    REQUIRE(json.contains("lossless_video_available"));
    REQUIRE(json.contains("supported_execution_engines"));
    REQUIRE(json["supported_execution_engines"].is_array());
    REQUIRE_FALSE(json["supported_execution_engines"].empty());
}

TEST_CASE("preferred runtime device and optimization profile stay product-aligned",
          "[unit][runtime][regression]") {
    RuntimeCapabilities windows_capabilities;
    windows_capabilities.platform = "windows";
    windows_capabilities.supported_backends = {Backend::CPU, Backend::TensorRT};

    std::vector<DeviceInfo> windows_devices = {
        DeviceInfo{"Generic CPU", 0, Backend::CPU},
        DeviceInfo{"RTX 3080", 10240, Backend::TensorRT},
    };

    auto preferred_windows = preferred_runtime_device(windows_capabilities, windows_devices);
    REQUIRE(preferred_windows.has_value());
    REQUIRE(preferred_windows->backend == Backend::TensorRT);

    const auto windows_rtx_models_dir = write_models_inventory_fixture("windows-rtx");
    {
        ScopedModelsDirOverride windows_rtx_override(windows_rtx_models_dir);
        auto profile =
            runtime_optimization_profile_for_device(windows_capabilities, *preferred_windows);
        REQUIRE(profile.id == "windows-rtx");
        REQUIRE(profile.certification_tier == "packaged_fp16_ladder_through_2048");
        REQUIRE(profile.unrestricted_quality_attempt);
    }
    std::filesystem::remove_all(windows_rtx_models_dir.parent_path());

    RuntimeCapabilities mac_capabilities;
    mac_capabilities.platform = "macos";
    mac_capabilities.apple_silicon = true;
    mac_capabilities.supported_backends = {Backend::CPU, Backend::MLX};

    std::vector<DeviceInfo> mac_devices = {
        DeviceInfo{"Generic CPU", 0, Backend::CPU},
        DeviceInfo{"Apple Silicon MLX", 16384, Backend::MLX},
    };

    auto preferred_mac = preferred_runtime_device(mac_capabilities, mac_devices);
    REQUIRE(preferred_mac.has_value());
    REQUIRE(preferred_mac->backend == Backend::MLX);

    auto apple_profile = runtime_optimization_profile_for_device(mac_capabilities, *preferred_mac);
    REQUIRE(apple_profile.id == "apple-silicon-mlx");
    REQUIRE(apple_profile.backend_intent == "mlx");
}

TEST_CASE("model catalog marks validated macOS entries", "[unit][runtime]") {
    auto models = model_catalog();

    auto find_model = [&](const std::string& filename) {
        return std::find_if(models.begin(), models.end(), [&](const ModelCatalogEntry& entry) {
            return entry.filename == filename;
        });
    };

    auto int8_512 = find_model("corridorkey_int8_512.onnx");
    REQUIRE(int8_512 != models.end());
    REQUIRE(int8_512->validated_for_macos);
    REQUIRE(int8_512->packaged_for_macos);
    REQUIRE(int8_512->packaged_for_windows);
    REQUIRE(int8_512->intended_use == "portable_preview");
    REQUIRE(int8_512->artifact_family == "onnx");
    REQUIRE(int8_512->recommended_backend == "cpu");
    std::vector<std::string> validated_platforms = {"macos_apple_silicon"};
    REQUIRE(int8_512->validated_platforms == validated_platforms);

    auto int8_768 = find_model("corridorkey_int8_768.onnx");
    REQUIRE(int8_768 != models.end());
    REQUIRE(int8_768->validated_for_macos);
    REQUIRE_FALSE(int8_768->packaged_for_macos);
    REQUIRE(int8_768->packaged_for_windows);
    REQUIRE(int8_768->validated_hardware_tiers == std::vector<std::string>{"apple_silicon_16gb"});

    auto int8_1024 = find_model("corridorkey_int8_1024.onnx");
    REQUIRE(int8_1024 != models.end());
    REQUIRE_FALSE(int8_1024->validated_for_macos);
    REQUIRE(int8_1024->packaged_for_windows);

    auto mlx_pack = find_model("corridorkey_mlx.safetensors");
    REQUIRE(mlx_pack != models.end());
    REQUIRE(mlx_pack->validated_for_macos);
    REQUIRE(mlx_pack->packaged_for_macos);
    REQUIRE(mlx_pack->artifact_family == "safetensors");
    REQUIRE(mlx_pack->recommended_backend == "mlx");
    REQUIRE(mlx_pack->intended_use == "apple_acceleration_primary");
    REQUIRE(mlx_pack->intended_platforms == std::vector<std::string>{"macos_apple_silicon"});

    auto fp16_1024 = find_model("corridorkey_fp16_1024.onnx");
    REQUIRE(fp16_1024 != models.end());
    REQUIRE(fp16_1024->packaged_for_windows);
    REQUIRE(fp16_1024->recommended_backend == "tensorrt");
    REQUIRE(fp16_1024->intended_use == "windows_rtx_primary");

    auto fp16_512 = find_model("corridorkey_fp16_512.onnx");
    REQUIRE(fp16_512 != models.end());
    REQUIRE(fp16_512->packaged_for_windows);

    auto fp16_768 = find_model("corridorkey_fp16_768.onnx");
    REQUIRE(fp16_768 != models.end());
    REQUIRE_FALSE(fp16_768->packaged_for_windows);
    REQUIRE(fp16_768->intended_use == "reference_validation");
}

TEST_CASE("job events serialize to stable NDJSON payloads", "[unit][runtime]") {
    JobEvent event;
    event.type = JobEventType::BackendSelected;
    event.phase = "prepare";
    event.progress = 0.0F;
    event.backend = Backend::CPU;
    event.engine = ExecutionEngine::Official;
    event.message = "Generic CPU";
    event.fallback = BackendFallbackInfo{Backend::CoreML, Backend::CPU, "CoreML session failed"};
    event.timings.push_back(StageTiming{"ort_run", 12.5, 1, 3});

    auto json = to_json(event);

    REQUIRE(json["type"] == "backend_selected");
    REQUIRE(json["phase"] == "prepare");
    REQUIRE(json["backend"] == "cpu");
    REQUIRE(json["engine"] == "official");
    REQUIRE(json["fallback"]["requested_backend"] == "coreml");
    REQUIRE(json["fallback"]["selected_backend"] == "cpu");
    REQUIRE(json["timings"][0]["name"] == "ort_run");
    REQUIRE(json["timings"][0]["total_ms"] == Catch::Approx(12.5));
    REQUIRE(json["timings"][0]["avg_ms"] == Catch::Approx(12.5));
    REQUIRE(json["timings"][0]["ms_per_unit"] == Catch::Approx(12.5 / 3.0));
}

TEST_CASE("preset catalog exposes a default macOS profile", "[unit][runtime]") {
    auto presets = preset_catalog();

    auto default_it =
        std::find_if(presets.begin(), presets.end(),
                     [](const PresetDefinition& preset) { return preset.default_for_macos; });

    REQUIRE(default_it != presets.end());
    REQUIRE(default_it->params.enable_tiling);
    REQUIRE_FALSE(default_it->params.auto_despeckle);
    REQUIRE(default_it->recommended_model == "corridorkey_mlx.safetensors");
    REQUIRE(default_it->intended_use == "apple_acceleration_primary");
    std::vector<std::string> preset_platforms = {"macos_apple_silicon"};
    REQUIRE(default_it->validated_platforms == preset_platforms);

    auto max_quality_it =
        std::find_if(presets.begin(), presets.end(),
                     [](const PresetDefinition& preset) { return preset.id == "mac-max-quality"; });
    REQUIRE(max_quality_it != presets.end());
    REQUIRE(max_quality_it->params.auto_despeckle);

    auto windows_default_it =
        std::find_if(presets.begin(), presets.end(),
                     [](const PresetDefinition& preset) { return preset.default_for_windows; });
    REQUIRE(windows_default_it != presets.end());
    REQUIRE(windows_default_it->id == "win-rtx-balanced");
}

TEST_CASE("preset lookup accepts product-facing aliases", "[unit][runtime]") {
    auto capabilities = runtime_capabilities();
    bool windows_rtx_defaults =
        capabilities.platform == "windows" &&
        std::find(capabilities.supported_backends.begin(), capabilities.supported_backends.end(),
                  Backend::TensorRT) != capabilities.supported_backends.end();

    auto preview = find_preset_by_selector("preview");
    REQUIRE(preview.has_value());
    if (windows_rtx_defaults) {
        REQUIRE(preview->id == "win-cpu-safe");
    } else {
        REQUIRE(preview->id == "mac-preview");
    }

    auto balanced = find_preset_by_selector("balanced");
    REQUIRE(balanced.has_value());
    if (windows_rtx_defaults) {
        REQUIRE(balanced->id == "win-rtx-balanced");
    } else {
        REQUIRE(balanced->id == "mac-balanced");
    }

    auto max_quality = find_preset_by_selector("max");
    REQUIRE(max_quality.has_value());
    if (windows_rtx_defaults) {
        REQUIRE(max_quality->id == "win-rtx-max-quality");
    } else {
        REQUIRE(max_quality->id == "mac-max-quality");
    }

    REQUIRE_FALSE(find_preset_by_selector("unknown").has_value());
}

TEST_CASE("default model selection stays aligned with device intent", "[unit][runtime]") {
    RuntimeCapabilities mac_capabilities;
    mac_capabilities.platform = "macos";
    mac_capabilities.apple_silicon = true;
    mac_capabilities.supported_backends = {Backend::MLX, Backend::CPU};

    auto default_preset = default_preset_for_capabilities(mac_capabilities);
    REQUIRE(default_preset.has_value());
    REQUIRE(default_preset->id == "mac-balanced");

    auto mlx_model = default_model_for_request(
        mac_capabilities, DeviceInfo{"Apple Silicon MLX", 16000, Backend::Auto}, default_preset);
    REQUIRE(mlx_model.has_value());
    REQUIRE(mlx_model->filename == "corridorkey_mlx.safetensors");

    auto cpu_model = default_model_for_request(
        mac_capabilities, DeviceInfo{"Generic CPU", 0, Backend::CPU}, default_preset);
    REQUIRE(cpu_model.has_value());
    REQUIRE(cpu_model->filename == "corridorkey_int8_512.onnx");

    RuntimeCapabilities windows_capabilities;
    windows_capabilities.platform = "windows";
    windows_capabilities.supported_backends = {Backend::TensorRT, Backend::CPU};

    auto windows_default = default_preset_for_capabilities(windows_capabilities);
    REQUIRE(windows_default.has_value());
    REQUIRE(windows_default->id == "win-rtx-balanced");

    auto windows_rtx_model = default_model_for_request(
        windows_capabilities, DeviceInfo{"NVIDIA GeForce RTX 3080", 10240, Backend::TensorRT},
        windows_default);
    REQUIRE(windows_rtx_model.has_value());
    REQUIRE(windows_rtx_model->filename == "corridorkey_fp16_1024.onnx");

    auto windows_rtx_fp16_model = default_model_for_request(
        windows_capabilities, DeviceInfo{"NVIDIA GeForce RTX 3080", 10240, Backend::TensorRT},
        windows_default, ArtifactVariantPreference::FP16);
    REQUIRE(windows_rtx_fp16_model.has_value());
    REQUIRE(windows_rtx_fp16_model->filename == "corridorkey_fp16_1024.onnx");

    auto windows_cpu_model = default_model_for_request(
        windows_capabilities, DeviceInfo{"Generic CPU", 0, Backend::CPU}, windows_default);
    REQUIRE(windows_cpu_model.has_value());
    REQUIRE(windows_cpu_model->filename == "corridorkey_int8_512.onnx");

    auto windows_cpu_fp16_model =
        default_model_for_request(windows_capabilities, DeviceInfo{"Generic CPU", 0, Backend::CPU},
                                  windows_default, ArtifactVariantPreference::FP16);
    REQUIRE(windows_cpu_fp16_model.has_value());
    REQUIRE(windows_cpu_fp16_model->filename == "corridorkey_fp16_512.onnx");

    RuntimeCapabilities windows_universal_capabilities;
    windows_universal_capabilities.platform = "windows";
    windows_universal_capabilities.supported_backends = {Backend::DirectML, Backend::CPU};

    auto windows_universal_model =
        default_model_for_request(windows_universal_capabilities,
                                  DeviceInfo{"AMD Radeon", 16384, Backend::DirectML}, std::nullopt);
    REQUIRE(windows_universal_model.has_value());
    REQUIRE(windows_universal_model->filename == "corridorkey_int8_1024.onnx");
}

TEST_CASE("windows GPU resolution ceilings stay aligned with VRAM tiers", "[unit][runtime]") {
    REQUIRE(max_supported_resolution_for_device(DeviceInfo{"RTX 3070", 8192, Backend::TensorRT}) ==
            512);
    REQUIRE(max_supported_resolution_for_device(DeviceInfo{"RTX 3080", 10240, Backend::TensorRT}) ==
            1024);
    REQUIRE(max_supported_resolution_for_device(DeviceInfo{"RTX 4080", 16384, Backend::TensorRT}) ==
            1536);
    REQUIRE(max_supported_resolution_for_device(DeviceInfo{"RTX 4090", 24576, Backend::TensorRT}) ==
            2048);
    REQUIRE(max_supported_resolution_for_device(
                DeviceInfo{"AMD Radeon", 8192, Backend::DirectML}) == 512);
    REQUIRE(max_supported_resolution_for_device(
                DeviceInfo{"AMD Radeon", 16384, Backend::DirectML}) == 1024);
    REQUIRE(minimum_supported_memory_mb_for_resolution(Backend::TensorRT, 1536) == 16000);
    REQUIRE(minimum_supported_memory_mb_for_resolution(Backend::TensorRT, 2048) == 24000);
    REQUIRE_FALSE(minimum_supported_memory_mb_for_resolution(Backend::TensorRT, 768).has_value());
    REQUIRE_FALSE(minimum_supported_memory_mb_for_resolution(Backend::DirectML, 768).has_value());
}

TEST_CASE("hardware profile delegates Windows safe quality ceilings to runtime contracts",
          "[unit][runtime][regression]") {
    const auto rtx_strategy =
        HardwareProfile::get_best_strategy(DeviceInfo{"RTX 3080", 10240, Backend::TensorRT});
    CHECK(rtx_strategy.target_resolution == 1024);
    CHECK(rtx_strategy.recommended_variant == "fp16");

    const auto directml_strategy =
        HardwareProfile::get_best_strategy(DeviceInfo{"AMD Radeon", 16384, Backend::DirectML});
    CHECK(directml_strategy.target_resolution == 1024);
    CHECK(directml_strategy.recommended_variant == "int8");

    const auto cpu_strategy =
        HardwareProfile::get_best_strategy(DeviceInfo{"Generic CPU", 0, Backend::CPU});
    CHECK(cpu_strategy.target_resolution == 512);
    CHECK(cpu_strategy.recommended_variant == "int8");
}

TEST_CASE("runtime coarse-to-fine policy prefers safer coarse artifacts", "[unit][runtime]") {
    const auto legacy_models_dir = write_models_inventory_fixture("legacy");
    std::filesystem::remove(legacy_models_dir.parent_path() / "model_inventory.json");
    ScopedModelsDirOverride legacy_override(legacy_models_dir);

    const DeviceInfo rtx_3080{"RTX 3080", 10240, Backend::TensorRT};
    REQUIRE(should_use_coarse_to_fine_for_request(rtx_3080, 1536, QualityFallbackMode::Auto));
    REQUIRE(coarse_artifact_resolution_for_request(rtx_3080, 1536) == 1024);
    REQUIRE(should_use_coarse_to_fine_for_request(rtx_3080, 2048, QualityFallbackMode::Auto));
    REQUIRE(coarse_artifact_resolution_for_request(rtx_3080, 2048) == 1024);
    REQUIRE_FALSE(
        should_use_coarse_to_fine_for_request(rtx_3080, 1536, QualityFallbackMode::Direct));
    REQUIRE_FALSE(
        should_use_coarse_to_fine_for_request(rtx_3080, 1536, QualityFallbackMode::Auto, 0, true));
    REQUIRE(
        should_use_coarse_to_fine_for_request(rtx_3080, 1536, QualityFallbackMode::CoarseToFine));
    REQUIRE(coarse_artifact_resolution_for_request(rtx_3080, 1536, 768) == 768);
    REQUIRE_FALSE(coarse_artifact_resolution_for_request(rtx_3080, 1536, 1536).has_value());

    std::filesystem::remove_all(legacy_models_dir.parent_path());
}

TEST_CASE("runtime refinement override validation is explicit for current artifacts",
          "[unit][runtime][regression]") {
    auto auto_mode =
        validate_refinement_mode_for_artifact("corridorkey_fp16_1024.onnx", RefinementMode::Auto);
    REQUIRE(auto_mode.has_value());

    auto tiled_mode =
        validate_refinement_mode_for_artifact("corridorkey_fp16_1024.onnx", RefinementMode::Tiled);
    REQUIRE_FALSE(tiled_mode.has_value());
    REQUIRE(tiled_mode.error().code == ErrorCode::InvalidParameters);
    REQUIRE(tiled_mode.error().message.find("refinement strategy override") != std::string::npos);
}

TEST_CASE("runtime artifact selection prefers lower packaged candidates automatically",
          "[unit][runtime][regression]") {
    auto temp_dir = std::filesystem::temp_directory_path() / "corridorkey-runtime-artifact-select";
    std::filesystem::remove_all(temp_dir);
    std::filesystem::create_directories(temp_dir);
    std::ofstream(temp_dir / "corridorkey_fp16_768.onnx") << "stub";
    std::ofstream(temp_dir / "corridorkey_fp16_512.onnx") << "stub";

    auto selections = quality_artifact_candidates_for_request(
        temp_dir, DeviceInfo{"RTX 3080", 10240, Backend::TensorRT}, 2048,
        ArtifactVariantPreference::FP16, false, QualityFallbackMode::Auto);
    REQUIRE(selections.has_value());
    REQUIRE_FALSE(selections->empty());
    CHECK(selections->front().effective_resolution == 512);
    CHECK(selections->front().coarse_to_fine);

    auto expected = expected_artifact_paths_for_request(
        temp_dir, DeviceInfo{"RTX 3080", 10240, Backend::TensorRT}, 2048,
        ArtifactVariantPreference::FP16, false, QualityFallbackMode::Auto);
    REQUIRE(expected.has_value());
    REQUIRE(expected->size() == 2);
    CHECK(expected->front().filename() == "corridorkey_fp16_1024.onnx");
    CHECK((*expected)[1].filename() == "corridorkey_fp16_512.onnx");

    std::filesystem::remove_all(temp_dir);
}

TEST_CASE("packaged model resolution uses catalog entries for non-onnx artifacts",
          "[unit][runtime][regression]") {
    REQUIRE(packaged_model_resolution("corridorkey_mlx.safetensors") == 2048);
    REQUIRE(is_packaged_corridorkey_model("corridorkey_mlx.safetensors"));
}

TEST_CASE("artifact runtime state separates packaged, certified, and recommended",
          "[unit][runtime][regression]") {
    RuntimeCapabilities windows_capabilities;
    windows_capabilities.platform = "windows";
    windows_capabilities.supported_backends = {Backend::TensorRT, Backend::CPU};
    DeviceInfo rtx_3080{"RTX 3080", 10240, Backend::TensorRT};

    const auto windows_rtx_models_dir = write_models_inventory_fixture("windows-rtx");
    ScopedModelsDirOverride windows_rtx_override(windows_rtx_models_dir);

    auto fp16_1024 = find_model_by_filename("corridorkey_fp16_1024.onnx");
    REQUIRE(fp16_1024.has_value());
    auto recommended_state =
        artifact_runtime_state_for_device(*fp16_1024, windows_capabilities, rtx_3080, true);
    REQUIRE(recommended_state.packaged_for_active_track);
    REQUIRE(recommended_state.present);
    REQUIRE(recommended_state.certified_for_active_track);
    REQUIRE(recommended_state.certified_for_active_device);
    REQUIRE(recommended_state.recommended_for_active_device);
    REQUIRE(recommended_state.state == "recommended");

    auto fp16_1536 = find_model_by_filename("corridorkey_fp16_1536.onnx");
    REQUIRE(fp16_1536.has_value());
    auto certified_state =
        artifact_runtime_state_for_device(*fp16_1536, windows_capabilities, rtx_3080, true);
    REQUIRE(certified_state.packaged_for_active_track);
    REQUIRE(certified_state.present);
    REQUIRE(certified_state.certified_for_active_track);
    REQUIRE_FALSE(certified_state.certified_for_active_device);
    REQUIRE_FALSE(certified_state.recommended_for_active_device);
    REQUIRE(certified_state.state == "packaged");

    auto int8_1024 = find_model_by_filename("corridorkey_int8_1024.onnx");
    REQUIRE(int8_1024.has_value());
    auto packaged_only_state =
        artifact_runtime_state_for_device(*int8_1024, windows_capabilities, rtx_3080, true);
    REQUIRE(packaged_only_state.packaged_for_active_track);
    REQUIRE(packaged_only_state.present);
    REQUIRE_FALSE(packaged_only_state.certified_for_active_track);
    REQUIRE_FALSE(packaged_only_state.recommended_for_active_device);
    REQUIRE(packaged_only_state.state == "packaged");

    auto fp16_768 = find_model_by_filename("corridorkey_fp16_768.onnx");
    REQUIRE(fp16_768.has_value());
    auto reference_only_state =
        artifact_runtime_state_for_device(*fp16_768, windows_capabilities, rtx_3080, true);
    REQUIRE_FALSE(reference_only_state.packaged_for_active_track);
    REQUIRE(reference_only_state.present);
    REQUIRE(reference_only_state.state == "reference_only");

    auto missing_state =
        artifact_runtime_state_for_device(*fp16_1536, windows_capabilities, rtx_3080, false);
    REQUIRE(missing_state.packaged_for_active_track);
    REQUIRE_FALSE(missing_state.present);
    REQUIRE(missing_state.state == "missing");

    std::filesystem::remove_all(windows_rtx_models_dir.parent_path());
}

TEST_CASE("latency summaries stay stable for benchmark payloads", "[unit][runtime]") {
    auto json = summarize_latency_samples({4.0, 6.0, 8.0, 10.0});

    REQUIRE(json["count"] == 4);
    REQUIRE(json["min_ms"] == Catch::Approx(4.0));
    REQUIRE(json["max_ms"] == Catch::Approx(10.0));
    REQUIRE(json["avg_ms"] == Catch::Approx(7.0));
    REQUIRE(json["p50_ms"] == Catch::Approx(6.0));
    REQUIRE(json["p95_ms"] == Catch::Approx(8.0));
}
