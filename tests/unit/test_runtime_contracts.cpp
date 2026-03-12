#include <catch2/catch_all.hpp>
#include <corridorkey/engine.hpp>

#include "app/runtime_contracts.hpp"
#include "app/runtime_diagnostics.hpp"

using namespace corridorkey;
using namespace corridorkey::app;

TEST_CASE("runtime capabilities expose stable diagnostics", "[unit][runtime]") {
    auto capabilities = runtime_capabilities();
    auto devices = list_devices();

    REQUIRE_FALSE(capabilities.platform.empty());
    REQUIRE(capabilities.cpu_fallback_available);
    REQUIRE(capabilities.supported_backends.size() == devices.size());
    REQUIRE_FALSE(capabilities.default_video_encoder.empty());
    auto json = to_json(capabilities);
    REQUIRE(json.contains("mlx_probe_available"));
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
    REQUIRE(int8_512->intended_use == "portable_preview");
    REQUIRE(int8_512->artifact_family == "onnx");
    REQUIRE(int8_512->recommended_backend == "cpu");
    std::vector<std::string> validated_platforms = {"macos_apple_silicon"};
    REQUIRE(int8_512->validated_platforms == validated_platforms);

    auto int8_768 = find_model("corridorkey_int8_768.onnx");
    REQUIRE(int8_768 != models.end());
    REQUIRE(int8_768->validated_for_macos);
    REQUIRE_FALSE(int8_768->packaged_for_macos);
    REQUIRE(int8_768->validated_hardware_tiers == std::vector<std::string>{"apple_silicon_16gb"});

    auto int8_1024 = find_model("corridorkey_int8_1024.onnx");
    REQUIRE(int8_1024 != models.end());
    REQUIRE_FALSE(int8_1024->validated_for_macos);

    auto mlx_pack = find_model("corridorkey_mlx.safetensors");
    REQUIRE(mlx_pack != models.end());
    REQUIRE(mlx_pack->validated_for_macos);
    REQUIRE(mlx_pack->packaged_for_macos);
    REQUIRE(mlx_pack->artifact_family == "safetensors");
    REQUIRE(mlx_pack->recommended_backend == "mlx");
    REQUIRE(mlx_pack->intended_use == "apple_acceleration_primary");
    REQUIRE(mlx_pack->intended_platforms == std::vector<std::string>{"macos_apple_silicon"});
}

TEST_CASE("job events serialize to stable NDJSON payloads", "[unit][runtime]") {
    JobEvent event;
    event.type = JobEventType::BackendSelected;
    event.phase = "prepare";
    event.progress = 0.0F;
    event.backend = Backend::CPU;
    event.message = "Generic CPU";
    event.fallback = BackendFallbackInfo{Backend::CoreML, Backend::CPU, "CoreML session failed"};
    event.timings.push_back(StageTiming{"ort_run", 12.5, 1, 3});

    auto json = to_json(event);

    REQUIRE(json["type"] == "backend_selected");
    REQUIRE(json["phase"] == "prepare");
    REQUIRE(json["backend"] == "cpu");
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
