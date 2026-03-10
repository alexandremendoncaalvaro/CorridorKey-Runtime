#include <catch2/catch_all.hpp>
#include <corridorkey/engine.hpp>

#include "app/runtime_contracts.hpp"

using namespace corridorkey;
using namespace corridorkey::app;

TEST_CASE("runtime capabilities expose stable diagnostics", "[unit][runtime]") {
    auto capabilities = runtime_capabilities();
    auto devices = list_devices();

    REQUIRE_FALSE(capabilities.platform.empty());
    REQUIRE(capabilities.cpu_fallback_available);
    REQUIRE(capabilities.supported_backends.size() == devices.size());
    REQUIRE_FALSE(capabilities.default_video_encoder.empty());
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

    auto int8_768 = find_model("corridorkey_int8_768.onnx");
    REQUIRE(int8_768 != models.end());
    REQUIRE(int8_768->validated_for_macos);
    REQUIRE(int8_768->packaged_for_macos);

    auto int8_1024 = find_model("corridorkey_int8_1024.onnx");
    REQUIRE(int8_1024 != models.end());
    REQUIRE_FALSE(int8_1024->validated_for_macos);
}

TEST_CASE("job events serialize to stable NDJSON payloads", "[unit][runtime]") {
    JobEvent event;
    event.type = JobEventType::BackendSelected;
    event.phase = "prepare";
    event.progress = 0.0F;
    event.backend = Backend::CPU;
    event.message = "Generic CPU";
    event.fallback = BackendFallbackInfo{Backend::CoreML, Backend::CPU, "CoreML session failed"};

    auto json = to_json(event);

    REQUIRE(json["type"] == "backend_selected");
    REQUIRE(json["phase"] == "prepare");
    REQUIRE(json["backend"] == "cpu");
    REQUIRE(json["fallback"]["requested_backend"] == "coreml");
    REQUIRE(json["fallback"]["selected_backend"] == "cpu");
}

TEST_CASE("preset catalog exposes a default macOS profile", "[unit][runtime]") {
    auto presets = preset_catalog();

    auto default_it =
        std::find_if(presets.begin(), presets.end(),
                     [](const PresetDefinition& preset) { return preset.default_for_macos; });

    REQUIRE(default_it != presets.end());
    REQUIRE(default_it->params.enable_tiling);
    REQUIRE(default_it->recommended_model == "corridorkey_int8_768.onnx");
}
