#pragma once

#include <corridorkey/engine.hpp>
#include <cstdint>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

namespace corridorkey::app {

struct ArtifactSelection {
    std::filesystem::path executable_model_path = {};
    int requested_resolution = 0;
    int effective_resolution = 0;
    bool used_fallback = false;
    bool coarse_to_fine = false;
};

struct RuntimeOptimizationProfile {
    std::string id = "";
    std::string label = "";
    std::string intended_track = "";
    std::string backend_intent = "";
    std::string fallback_policy = "";
    std::string warmup_policy = "";
    std::string certification_tier = "";
    bool unrestricted_quality_attempt = false;
};

struct ArtifactRuntimeState {
    bool packaged_for_active_track = false;
    bool present = false;
    bool certified_for_active_track = false;
    bool certified_for_active_device = false;
    bool recommended_for_active_device = false;
    std::string state = "";
};

std::string backend_to_string(Backend backend);
std::string job_event_type_to_string(JobEventType type);

CORRIDORKEY_API std::optional<std::string> active_packaged_model_profile();
CORRIDORKEY_API std::optional<DeviceInfo> preferred_runtime_device(
    const RuntimeCapabilities& capabilities, const std::vector<DeviceInfo>& devices);
CORRIDORKEY_API RuntimeOptimizationProfile runtime_optimization_profile_for_device(
    const RuntimeCapabilities& capabilities, const DeviceInfo& device);
CORRIDORKEY_API ArtifactRuntimeState artifact_runtime_state_for_device(
    const ModelCatalogEntry& model, const RuntimeCapabilities& capabilities,
    const DeviceInfo& device, bool usable);
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
CORRIDORKEY_API std::optional<int> minimum_supported_memory_mb_for_resolution(Backend backend,
                                                                              int resolution);
CORRIDORKEY_API bool should_use_coarse_to_fine_for_request(
    const DeviceInfo& requested_device, int requested_resolution, QualityFallbackMode fallback_mode,
    int coarse_resolution_override = 0, bool allow_unrestricted_quality_attempt = false);
CORRIDORKEY_API std::optional<int> coarse_artifact_resolution_for_request(
    const DeviceInfo& requested_device, int requested_resolution,
    int coarse_resolution_override = 0);
CORRIDORKEY_API std::optional<int> packaged_model_resolution(
    const std::filesystem::path& model_path);
CORRIDORKEY_API bool is_packaged_corridorkey_model(const std::filesystem::path& model_path);
CORRIDORKEY_API std::filesystem::path sibling_model_path_for_resolution(
    const std::filesystem::path& model_path, int resolution);
CORRIDORKEY_API Result<void> validate_refinement_mode_for_artifact(
    const std::filesystem::path& model_path, RefinementMode refinement_mode);
CORRIDORKEY_API Result<std::vector<std::filesystem::path>> expected_artifact_paths_for_request(
    const std::filesystem::path& models_root, const DeviceInfo& requested_device,
    int requested_resolution, bool allow_lower_resolution_fallback = false,
    QualityFallbackMode fallback_mode = QualityFallbackMode::Auto,
    int coarse_resolution_override = 0, bool allow_unrestricted_quality_attempt = false);
CORRIDORKEY_API Result<std::vector<ArtifactSelection>> quality_artifact_candidates_for_request(
    const std::filesystem::path& models_root, const DeviceInfo& requested_device,
    int requested_resolution, bool allow_lower_resolution_fallback = false,
    QualityFallbackMode fallback_mode = QualityFallbackMode::Auto,
    int coarse_resolution_override = 0, bool allow_unrestricted_quality_attempt = false);
CORRIDORKEY_API Result<std::filesystem::path> resolve_model_artifact_for_request(
    const std::filesystem::path& model_path, const InferenceParams& params,
    const DeviceInfo& requested_device);

nlohmann::json to_json(const Error& error);
nlohmann::json to_json(const BackendFallbackInfo& fallback);
nlohmann::json to_json(const RuntimeCapabilities& capabilities);
nlohmann::json to_json(const StageTiming& timing);
nlohmann::json to_json(const JobEvent& event);
nlohmann::json to_json(const ModelCatalogEntry& model);
nlohmann::json to_json(const PresetDefinition& preset);
nlohmann::json to_json(const RuntimeOptimizationProfile& profile);
nlohmann::json to_json(const ArtifactRuntimeState& state);

}  // namespace corridorkey::app
