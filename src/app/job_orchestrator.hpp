#pragma once

#include <corridorkey/engine.hpp>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <string>

namespace corridorkey::app {

CORRIDORKEY_API nlohmann::json summarize_doctor_report(const nlohmann::json& report);
CORRIDORKEY_API nlohmann::json summarize_stage_groups(const std::vector<StageTiming>& timings);

/**
 * @brief Structured result of a job.
 */
struct JobResult {
    bool success;
    std::string message;
    nlohmann::json metadata;
};

/**
 * @brief Definition of a processing job.
 * Decouples CLI arguments from the actual execution logic.
 */
struct JobRequest {
    std::filesystem::path input_path;
    std::filesystem::path hint_path;  // Can be empty for auto-hinting
    std::filesystem::path output_path;
    std::filesystem::path model_path;

    InferenceParams params;
    VideoOutputOptions video_output;
    DeviceInfo device;
    EngineCreateOptions engine_options = {};
};

/**
 * @brief High-level orchestrator that manages the lifecycle of a processing job.
 * This is the primary entry point for the CLI and future TUIs.
 */
class CORRIDORKEY_API JobOrchestrator {
   public:
    /**
     * @brief Execute a job request.
     * Handles file detection (video vs sequence) and engine initialization.
     */
    static Result<void> run(const JobRequest& request, ProgressCallback on_progress = nullptr,
                            JobEventCallback on_event = nullptr);

    /**
     * @brief Get comprehensive hardware and system information as JSON.
     * Used by CLI --json and future GUIs.
     */
    static nlohmann::json get_system_info();

    /**
     * @brief Run a diagnostic check on the system and models.
     */
    static nlohmann::json run_doctor(const std::filesystem::path& models_dir);

    /**
     * @brief Run a performance benchmark.
     */
    static nlohmann::json run_benchmark(const JobRequest& request);

    /**
     * @brief Get the built-in model catalog as stable JSON.
     */
    static nlohmann::json list_models();

    /**
     * @brief Get the built-in preset catalog as stable JSON.
     */
    static nlohmann::json list_presets();

   private:
    static bool is_video_file(const std::filesystem::path& p);
};

}  // namespace corridorkey::app
