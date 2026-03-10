#pragma once

#include <filesystem>
#include <nlohmann/json.hpp>
#include <vector>

namespace corridorkey::app {

nlohmann::json inspect_operational_health(const std::filesystem::path& models_dir);
nlohmann::json summarize_latency_samples(const std::vector<double>& samples);

}  // namespace corridorkey::app
