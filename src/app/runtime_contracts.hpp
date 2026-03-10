#pragma once

#include <corridorkey/engine.hpp>
#include <nlohmann/json.hpp>
#include <string>

namespace corridorkey::app {

std::string backend_to_string(Backend backend);
std::string job_event_type_to_string(JobEventType type);

nlohmann::json to_json(const Error& error);
nlohmann::json to_json(const BackendFallbackInfo& fallback);
nlohmann::json to_json(const RuntimeCapabilities& capabilities);
nlohmann::json to_json(const StageTiming& timing);
nlohmann::json to_json(const JobEvent& event);
nlohmann::json to_json(const ModelCatalogEntry& model);
nlohmann::json to_json(const PresetDefinition& preset);

}  // namespace corridorkey::app
