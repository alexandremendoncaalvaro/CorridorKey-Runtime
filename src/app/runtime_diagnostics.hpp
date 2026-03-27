#pragma once

#include <filesystem>
#include <corridorkey/engine.hpp>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

namespace corridorkey::app {

std::string diagnostic_backend_label(Backend backend);
std::vector<std::string> windows_probe_models_for_backend(Backend backend,
                                                          const DeviceInfo& device);
int windows_backend_probe_priority(Backend backend);
bool is_successful_windows_probe(const nlohmann::json& probe);
std::optional<nlohmann::json> preferred_windows_probe(const nlohmann::json& probes);

nlohmann::json inspect_bundle_for_diagnostics(const std::filesystem::path& models_dir,
                                              const std::filesystem::path& executable_path);
nlohmann::json inspect_operational_health(const std::filesystem::path& models_dir);
nlohmann::json summarize_latency_samples(const std::vector<double>& samples);

}  // namespace corridorkey::app
