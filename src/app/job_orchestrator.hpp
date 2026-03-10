#pragma once

#include <corridorkey/engine.hpp>
#include <filesystem>
#include <string>

namespace corridorkey::app {

/**
 * @brief Definition of a processing job.
 * Decouples CLI arguments from the actual execution logic.
 */
struct JobRequest {
    std::filesystem::path input_path;
    std::filesystem::path hint_path;   // Can be empty for auto-hinting
    std::filesystem::path output_path;
    std::filesystem::path model_path;
    
    InferenceParams params;
    DeviceInfo device;
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
    static Result<void> run(const JobRequest& request, ProgressCallback on_progress = nullptr);

private:
    static bool is_video_file(const std::filesystem::path& p);
};

} // namespace corridorkey::app
