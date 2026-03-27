#pragma once

#include <corridorkey/engine.hpp>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>

namespace corridorkey::app {

std::string backend_to_string(Backend backend);
std::string job_event_type_to_string(JobEventType type);

CORRIDORKEY_API std::optional<ModelCatalogEntry> find_model_by_filename(
    const std::string& filename);
CORRIDORKEY_API std::optional<PresetDefinition> find_preset_by_selector(
    const std::string& selector);
CORRIDORKEY_API std::optional<PresetDefinition> default_preset_for_capabilities(
    const RuntimeCapabilities& capabilities);
CORRIDORKEY_API std::optional<ModelCatalogEntry> default_model_for_request(
    const RuntimeCapabilities& capabilities, const DeviceInfo& requested_device,
    const std::optional<PresetDefinition>& preset);
CORRIDORKEY_API std::optional<int> max_supported_resolution_for_device(
    const DeviceInfo& requested_device);
CORRIDORKEY_API std::optional<int> minimum_supported_memory_mb_for_resolution(
    Backend backend, int resolution);
CORRIDORKEY_API bool should_use_coarse_to_fine_for_request(
    const DeviceInfo& requested_device, int requested_resolution,
    QualityFallbackMode fallback_mode, int coarse_resolution_override = 0);
CORRIDORKEY_API std::optional<int> coarse_artifact_resolution_for_request(
    const DeviceInfo& requested_device, int requested_resolution,
    int coarse_resolution_override = 0);

nlohmann::json to_json(const Error& error);
nlohmann::json to_json(const BackendFallbackInfo& fallback);
nlohmann::json to_json(const RuntimeCapabilities& capabilities);
nlohmann::json to_json(const StageTiming& timing);
nlohmann::json to_json(const JobEvent& event);
nlohmann::json to_json(const ModelCatalogEntry& model);
nlohmann::json to_json(const PresetDefinition& preset);

}  // namespace corridorkey::app
