#include "job_orchestrator.hpp"
#include "hardware_profile.hpp"
#include <algorithm>
#include <iostream>

namespace corridorkey::app {

Result<void> JobOrchestrator::run(const JobRequest& request, ProgressCallback on_progress) {
    JobRequest req = request;

    // 1. Resolve Hardware Strategy if resolution is Auto (0)
    if (req.params.target_resolution <= 0) {
        auto strategy = HardwareProfile::get_best_strategy(req.device);
        req.params.target_resolution = strategy.target_resolution;
        std::cout << "[App] Auto-resolution strategy: " << req.params.target_resolution << "px\n";
    }

    // 2. Create Engine
    auto engine_res = Engine::create(req.model_path, req.device);
    if (!engine_res) {
        return unexpected(engine_res.error());
    }
    auto engine = std::move(*engine_res);

    // 3. Prepare Output Directory
    if (std::filesystem::is_directory(req.output_path) || req.output_path.extension().empty()) {
        std::filesystem::create_directories(req.output_path);
    } else {
        std::filesystem::create_directories(req.output_path.parent_path());
    }

    // 4. Dispatch to Video or Sequence
    if (std::filesystem::is_regular_file(req.input_path) && is_video_file(req.input_path)) {
        return engine->process_video(
            req.input_path, 
            req.hint_path, 
            req.output_path, 
            req.params, 
            on_progress
        );
    } 
    
    // Process as Image Sequence (Folder or single image)
    std::vector<std::filesystem::path> inputs;
    std::vector<std::filesystem::path> hints;

    auto is_image_file = [](const std::filesystem::path& p) {
        std::string ext = p.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        return ext == ".png" || ext == ".exr" || ext == ".jpg" || ext == ".jpeg";
    };

    if (std::filesystem::is_directory(req.input_path)) {
        for (const auto& entry : std::filesystem::directory_iterator(req.input_path)) {
            if (entry.is_regular_file() && is_image_file(entry.path())) {
                inputs.push_back(entry.path());
                if (!req.hint_path.empty()) {
                    hints.push_back(req.hint_path / entry.path().filename());
                }
            }
        }
        std::sort(inputs.begin(), inputs.end());
        if (!hints.empty()) std::sort(hints.begin(), hints.end());
    } else {
        inputs.push_back(req.input_path);
        if (!req.hint_path.empty()) {
            hints.push_back(req.hint_path);
        }
    }

    if (inputs.empty()) {
        return unexpected(Error{ ErrorCode::InvalidParameters, "No valid input files found." });
    }

    return engine->process_sequence(inputs, hints, req.output_path, req.params, on_progress);
}

bool JobOrchestrator::is_video_file(const std::filesystem::path& p) {
    std::string ext = p.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    return ext == ".mp4" || ext == ".mov" || ext == ".avi" || ext == ".mkv";
}

} // namespace corridorkey::app
