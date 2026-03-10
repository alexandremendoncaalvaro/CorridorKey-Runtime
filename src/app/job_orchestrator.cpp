#include "job_orchestrator.hpp"
#include "hardware_profile.hpp"
#include <corridorkey/version.hpp>
#include <algorithm>
#include <iostream>
#include <chrono>

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

nlohmann::json JobOrchestrator::get_system_info() {
    nlohmann::json info;
    info["version"] = CORRIDORKEY_VERSION_STRING;
    
    auto devices = list_devices();
    nlohmann::json devices_json = nlohmann::json::array();
    
    for (const auto& d : devices) {
        nlohmann::json dj;
        dj["name"] = d.name;
        dj["memory_mb"] = d.available_memory_mb;
        
        std::string backend_name;
        switch (d.backend) {
            case Backend::CPU: backend_name = "cpu"; break;
            case Backend::CoreML: backend_name = "coreml"; break;
            case Backend::CUDA: backend_name = "cuda"; break;
            case Backend::TensorRT: backend_name = "tensorrt"; break;
            case Backend::DirectML: backend_name = "dml"; break;
            default: backend_name = "unknown"; break;
        }
        dj["backend"] = backend_name;
        devices_json.push_back(dj);
    }
    
    info["devices"] = devices_json;
    
    // Recommendation based on primary device
    auto strategy = HardwareProfile::get_best_strategy(devices[0]);
    info["recommendation"]["resolution"] = strategy.target_resolution;
    info["recommendation"]["variant"] = strategy.recommended_variant;
    
    return info;
}

nlohmann::json JobOrchestrator::run_doctor(const std::filesystem::path& models_dir) {
    nlohmann::json report;
    report["timestamp"] = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    
    // Check Hardware
    report["system"] = get_system_info();
    
    // Check Models
    nlohmann::json models = nlohmann::json::array();
    std::vector<std::string> variants = {"int8", "fp16", "fp32"};
    std::vector<int> resolutions = {512, 768, 1024};
    
    for (const auto& v : variants) {
        for (int r : resolutions) {
            std::string filename = "corridorkey_" + v + "_" + std::to_string(r) + ".onnx";
            std::filesystem::path p = models_dir / filename;
            nlohmann::json mj;
            mj["variant"] = v;
            mj["resolution"] = r;
            mj["path"] = p.string();
            mj["found"] = std::filesystem::exists(p);
            if (mj["found"]) {
                mj["size_bytes"] = std::filesystem::file_size(p);
            }
            models.push_back(mj);
        }
    }
    report["models"] = models;
    
    return report;
}

nlohmann::json JobOrchestrator::run_benchmark(const std::filesystem::path& model_path, const DeviceInfo& device) {
    nlohmann::json results;
    
    auto engine_res = Engine::create(model_path, device);
    if (!engine_res) {
        results["error"] = engine_res.error().message;
        return results;
    }
    auto engine = std::move(*engine_res);
    
    int res = engine->recommended_resolution();
    ImageBuffer rgb_buf(res, res, 3);
    ImageBuffer hint_buf(res, res, 1);
    
    const int warmup_runs = 2;
    const int benchmark_runs = 5;
    
    std::cout << "[Benchmark] Warming up (" << warmup_runs << " runs)..." << std::endl;
    for (int i = 0; i < warmup_runs; ++i) {
        engine->process_frame(rgb_buf.view(), hint_buf.view());
    }
    
    std::cout << "[Benchmark] Running benchmark (" << benchmark_runs << " runs)..." << std::endl;
    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < benchmark_runs; ++i) {
        engine->process_frame(rgb_buf.view(), hint_buf.view());
    }
    auto end = std::chrono::high_resolution_clock::now();
    
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    float avg_ms = static_cast<float>(duration) / benchmark_runs;
    
    results["model"] = model_path.filename().string();
    results["resolution"] = res;
    results["device"] = device.name;
    results["avg_latency_ms"] = avg_ms;
    results["fps"] = 1000.0f / avg_ms;
    
    return results;
}

} // namespace corridorkey::app
