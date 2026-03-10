#include "job_orchestrator.hpp"

#include <algorithm>
#include <chrono>
#include <corridorkey/version.hpp>

#include "hardware_profile.hpp"
#include "runtime_contracts.hpp"

namespace corridorkey::app {

namespace {

Result<void> emit_event(const JobEventCallback& callback, const JobEvent& event) {
    if (callback && !callback(event)) {
        return Unexpected(Error{ErrorCode::Cancelled, "Processing cancelled by user"});
    }
    return {};
}

}  // namespace

Result<void> JobOrchestrator::run(const JobRequest& request, ProgressCallback on_progress,
                                  JobEventCallback on_event) {
    JobRequest req = request;
    bool emitted_fallback = false;

    auto started_event =
        JobEvent{JobEventType::JobStarted, "prepare", 0.0F, Backend::Auto, "Job accepted"};
    auto started_res = emit_event(on_event, started_event);
    if (!started_res) return Unexpected(started_res.error());

    // 1. Resolve Hardware Strategy if resolution is Auto (0)
    if (req.params.target_resolution <= 0) {
        auto strategy = HardwareProfile::get_best_strategy(req.device);
        req.params.target_resolution = strategy.target_resolution;
    }

    // 2. Create Engine
    auto engine_res = Engine::create(req.model_path, req.device);
    if (!engine_res) {
        auto failed_event = JobEvent{
            JobEventType::Failed,
            "prepare",
            0.0F,
            Backend::Auto,
            "Engine initialization failed",
            "",
            engine_res.error(),
        };
        auto emit_res = emit_event(on_event, failed_event);
        if (!emit_res) return Unexpected(emit_res.error());
        return Unexpected(engine_res.error());
    }
    auto engine = std::move(*engine_res);

    auto backend_event = JobEvent{
        JobEventType::BackendSelected, "prepare", 0.0F, engine->current_device().backend,
        engine->current_device().name,
    };
    if (engine->backend_fallback().has_value()) {
        backend_event.fallback = engine->backend_fallback();
        emitted_fallback = true;
    }
    auto emit_backend_res = emit_event(on_event, backend_event);
    if (!emit_backend_res) return Unexpected(emit_backend_res.error());

    if (engine->backend_fallback().has_value()) {
        auto warning_event = JobEvent{
            JobEventType::Warning,
            "prepare",
            0.0F,
            engine->current_device().backend,
            "Fell back to CPU for compatibility.",
            "",
            std::nullopt,
            engine->backend_fallback(),
        };
        auto emit_warning_res = emit_event(on_event, warning_event);
        if (!emit_warning_res) return Unexpected(emit_warning_res.error());
    }

    if (req.hint_path.empty()) {
        auto warning_event = JobEvent{
            JobEventType::Warning,
            "prepare",
            0.0F,
            engine->current_device().backend,
            "No alpha hint provided. Rough matte generation will be used.",
        };
        auto emit_warning_res = emit_event(on_event, warning_event);
        if (!emit_warning_res) return Unexpected(emit_warning_res.error());
    }

    // 3. Prepare Output Directory
    if (std::filesystem::is_directory(req.output_path) || req.output_path.extension().empty()) {
        std::filesystem::create_directories(req.output_path);
    } else {
        std::filesystem::create_directories(req.output_path.parent_path());
    }

    auto report_progress = [&](float progress, const std::string& status) -> bool {
        if (!emitted_fallback && engine->backend_fallback().has_value()) {
            emitted_fallback = true;
            JobEvent warning_event{
                JobEventType::Warning,
                "inference",
                progress,
                engine->current_device().backend,
                "Fell back to CPU during execution.",
                "",
                std::nullopt,
                engine->backend_fallback(),
            };
            auto warning_res = emit_event(on_event, warning_event);
            if (!warning_res) return false;

            JobEvent backend_update{
                JobEventType::BackendSelected,
                "inference",
                progress,
                engine->current_device().backend,
                engine->current_device().name,
                "",
                std::nullopt,
                engine->backend_fallback(),
            };
            auto backend_res = emit_event(on_event, backend_update);
            if (!backend_res) return false;
        }

        if (on_progress && !on_progress(progress, status)) {
            return false;
        }

        JobEvent progress_event{
            JobEventType::Progress, "inference", progress, engine->current_device().backend, status,
        };
        auto progress_res = emit_event(on_event, progress_event);
        return progress_res.has_value();
    };

    auto finalize_failure = [&](const Error& error) -> Result<void> {
        JobEvent failed_event;
        failed_event.type =
            error.code == ErrorCode::Cancelled ? JobEventType::Cancelled : JobEventType::Failed;
        failed_event.phase = "complete";
        failed_event.progress = 1.0F;
        failed_event.backend = engine->current_device().backend;
        failed_event.message = error.message;
        failed_event.error = error;
        if (engine->backend_fallback().has_value()) {
            failed_event.fallback = engine->backend_fallback();
        }
        auto emit_res = emit_event(on_event, failed_event);
        if (!emit_res) return Unexpected(emit_res.error());
        return Unexpected(error);
    };

    // 4. Dispatch to Video or Sequence
    if (std::filesystem::is_regular_file(req.input_path) && is_video_file(req.input_path)) {
        auto result = engine->process_video(req.input_path, req.hint_path, req.output_path,
                                            req.params, report_progress);
        if (!result) {
            return finalize_failure(result.error());
        }
    } else {
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
            Error error{ErrorCode::InvalidParameters, "No valid input files found."};
            return finalize_failure(error);
        }

        auto result =
            engine->process_sequence(inputs, hints, req.output_path, req.params, report_progress);
        if (!result) {
            return finalize_failure(result.error());
        }
    }

    JobEvent artifact_event{
        JobEventType::ArtifactWritten,
        "complete",
        1.0F,
        engine->current_device().backend,
        "Primary output written",
        req.output_path.string(),
    };
    if (engine->backend_fallback().has_value()) {
        artifact_event.fallback = engine->backend_fallback();
    }
    auto emit_artifact_res = emit_event(on_event, artifact_event);
    if (!emit_artifact_res) return Unexpected(emit_artifact_res.error());

    JobEvent completed_event{
        JobEventType::Completed,
        "complete",
        1.0F,
        engine->current_device().backend,
        "Processing finished successfully",
    };
    if (engine->backend_fallback().has_value()) {
        completed_event.fallback = engine->backend_fallback();
    }
    auto emit_completed_res = emit_event(on_event, completed_event);
    if (!emit_completed_res) return Unexpected(emit_completed_res.error());

    return {};
}

bool JobOrchestrator::is_video_file(const std::filesystem::path& p) {
    std::string ext = p.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    return ext == ".mp4" || ext == ".mov" || ext == ".avi" || ext == ".mkv";
}

nlohmann::json JobOrchestrator::get_system_info() {
    nlohmann::json info;
    info["version"] = CORRIDORKEY_VERSION_STRING;
    info["capabilities"] = to_json(runtime_capabilities());

    auto devices = list_devices();
    nlohmann::json devices_json = nlohmann::json::array();

    for (const auto& d : devices) {
        nlohmann::json dj;
        dj["name"] = d.name;
        dj["memory_mb"] = d.available_memory_mb;

        std::string backend_name;
        switch (d.backend) {
            case Backend::CPU:
                backend_name = "cpu";
                break;
            case Backend::CoreML:
                backend_name = "coreml";
                break;
            case Backend::CUDA:
                backend_name = "cuda";
                break;
            case Backend::TensorRT:
                backend_name = "tensorrt";
                break;
            case Backend::DirectML:
                backend_name = "dml";
                break;
            default:
                backend_name = "unknown";
                break;
        }
        dj["backend"] = backend_name;
        devices_json.push_back(dj);
    }

    info["devices"] = devices_json;

    // Recommendation based on primary device
    auto strategy = HardwareProfile::get_best_strategy(devices[0]);
    info["recommendation"]["resolution"] = strategy.target_resolution;
    info["recommendation"]["variant"] = strategy.recommended_variant;
    info["commands"] = {"info", "doctor", "benchmark", "download", "models", "presets", "process"};

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
    report["presets"] = list_presets();

    return report;
}

nlohmann::json JobOrchestrator::run_benchmark(const std::filesystem::path& model_path,
                                              const DeviceInfo& device) {
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

    for (int i = 0; i < warmup_runs; ++i) {
        engine->process_frame(rgb_buf.view(), hint_buf.view());
    }

    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < benchmark_runs; ++i) {
        engine->process_frame(rgb_buf.view(), hint_buf.view());
    }
    auto end = std::chrono::high_resolution_clock::now();

    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    float avg_ms = static_cast<float>(duration) / benchmark_runs;

    results["model"] = model_path.filename().string();
    results["resolution"] = res;
    results["requested_device"] = device.name;
    results["device"] = engine->current_device().name;
    results["backend"] = backend_to_string(engine->current_device().backend);
    results["avg_latency_ms"] = avg_ms;
    results["fps"] = 1000.0f / avg_ms;
    if (engine->backend_fallback().has_value()) {
        results["fallback"] = to_json(*engine->backend_fallback());
    }

    return results;
}

nlohmann::json JobOrchestrator::list_models() {
    nlohmann::json models = nlohmann::json::array();
    for (const auto& model : model_catalog()) {
        models.push_back(to_json(model));
    }
    return models;
}

nlohmann::json JobOrchestrator::list_presets() {
    nlohmann::json presets = nlohmann::json::array();
    for (const auto& preset : preset_catalog()) {
        presets.push_back(to_json(preset));
    }
    return presets;
}

}  // namespace corridorkey::app
