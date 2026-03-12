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
    const RuntimeCapabilities& capabilities, Backend requested_backend,
    const std::optional<PresetDefinition>& preset);

nlohmann::json to_json(const Error& error);
nlohmann::json to_json(const BackendFallbackInfo& fallback);
nlohmann::json to_json(const RuntimeCapabilities& capabilities);
nlohmann::json to_json(const StageTiming& timing);
nlohmann::json to_json(const JobEvent& event);
nlohmann::json to_json(const ModelCatalogEntry& model);
nlohmann::json to_json(const PresetDefinition& preset);

}  // namespace corridorkey::app
