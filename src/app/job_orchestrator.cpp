#include "job_orchestrator.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <corridorkey/version.hpp>

#include "common/stage_profiler.hpp"
#include "hardware_profile.hpp"
#include "runtime_contracts.hpp"
#include "runtime_diagnostics.hpp"

namespace corridorkey::app {

namespace {

Result<void> emit_event(const JobEventCallback& callback, const JobEvent& event) {
    if (callback && !callback(event)) {
        return Unexpected(Error{ErrorCode::Cancelled, "Processing cancelled by user"});
    }
    return {};
}

std::vector<StageTiming> finalize_timings(common::StageProfiler& profiler,
                                          const std::chrono::steady_clock::time_point& start,
                                          bool& total_recorded) {
    if (!total_recorded) {
        auto end = std::chrono::steady_clock::now();
        profiler.record("job_total", std::chrono::duration<double, std::milli>(end - start).count(),
                        1);
        total_recorded = true;
    }

    return profiler.snapshot();
}

std::filesystem::path make_benchmark_output_path(const JobRequest& request, bool video_input) {
    auto temp_root = std::filesystem::temp_directory_path() / "corridorkey-benchmark";
    std::filesystem::create_directories(temp_root);

    auto token = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    auto stem = request.input_path.stem().string();
    if (stem.empty()) {
        stem = "synthetic";
    }

    if (video_input) {
        return temp_root / (stem + "_" + token + ".mp4");
    }

    return temp_root / (stem + "_" + token);
}

std::uint64_t processed_units_from_timings(const std::vector<StageTiming>& timings) {
    const std::array<std::string_view, 4> preferred_stages = {
        "video_encode_frame",
        "sequence_write_output",
        "video_infer_batch",
        "sequence_infer_batch",
    };

    for (std::string_view stage_name : preferred_stages) {
        auto it = std::find_if(timings.begin(), timings.end(), [&](const StageTiming& timing) {
            return timing.name == stage_name && timing.work_units > 0;
        });
        if (it != timings.end()) {
            return it->work_units;
        }
    }

    return 0;
}

void cleanup_benchmark_output(const std::filesystem::path& output_path) {
    std::error_code error;
    if (output_path.empty()) return;

    if (std::filesystem::is_directory(output_path, error)) {
        std::filesystem::remove_all(output_path, error);
        return;
    }

    std::filesystem::remove(output_path, error);
}

}  // namespace

Result<void> JobOrchestrator::run(const JobRequest& request, ProgressCallback on_progress,
                                  JobEventCallback on_event) {
    JobRequest req = request;
    bool emitted_fallback = false;
    common::StageProfiler profiler;
    auto stage_callback = [&](const StageTiming& timing) { profiler.record(timing); };
    auto job_start = std::chrono::steady_clock::now();
    bool total_recorded = false;

    auto started_event =
        JobEvent{JobEventType::JobStarted, "prepare", 0.0F, Backend::Auto, "Job accepted"};
    auto started_res = emit_event(on_event, started_event);
    if (!started_res) return Unexpected(started_res.error());

    // 1. Create Engine
    auto engine_res = profiler.measure("engine_create", [&]() {
        return Engine::create(req.model_path, req.device, stage_callback);
    });
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
        failed_event.timings = finalize_timings(profiler, job_start, total_recorded);
        auto emit_res = emit_event(on_event, failed_event);
        if (!emit_res) return Unexpected(emit_res.error());
        return Unexpected(engine_res.error());
    }
    auto engine = std::move(*engine_res);

    if (req.params.target_resolution <= 0) {
        profiler.measure("hardware_strategy", [&]() {
            if (engine->current_device().backend == Backend::MLX) {
                req.params.target_resolution = engine->recommended_resolution();
                return;
            }

            auto strategy = HardwareProfile::get_best_strategy(engine->current_device());
            req.params.target_resolution = strategy.target_resolution;
        });
    }

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

    // 2. Prepare Output Directory
    profiler.measure("output_prepare", [&]() {
        if (std::filesystem::is_directory(req.output_path) || req.output_path.extension().empty()) {
            std::filesystem::create_directories(req.output_path);
        } else {
            std::filesystem::create_directories(req.output_path.parent_path());
        }
    });

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
        failed_event.timings = finalize_timings(profiler, job_start, total_recorded);
        auto emit_res = emit_event(on_event, failed_event);
        if (!emit_res) return Unexpected(emit_res.error());
        return Unexpected(error);
    };

    // 3. Dispatch to Video or Sequence
    if (std::filesystem::is_regular_file(req.input_path) && is_video_file(req.input_path)) {
        auto result = profiler.measure("video_pipeline", [&]() {
            return engine->process_video(req.input_path, req.hint_path, req.output_path, req.params,
                                         report_progress, stage_callback);
        });
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

        profiler.measure("sequence_collect_inputs", [&]() {
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
        });

        if (inputs.empty()) {
            Error error{ErrorCode::InvalidParameters, "No valid input files found."};
            return finalize_failure(error);
        }

        auto result = profiler.measure(
            "sequence_pipeline",
            [&]() {
                return engine->process_sequence(inputs, hints, req.output_path, req.params,
                                                report_progress, stage_callback);
            },
            inputs.size());
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
    completed_event.timings = finalize_timings(profiler, job_start, total_recorded);
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
            case Backend::MLX:
                backend_name = "mlx";
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

    report["system"] = get_system_info();
    auto health = inspect_operational_health(models_dir);
    report["executable"] = health["executable"];
    report["bundle"] = health["bundle"];
    report["video"] = health["video"];
    report["cache"] = health["cache"];
    report["coreml"] = health["coreml"];
    report["mlx"] = health["mlx"];

    nlohmann::json models = nlohmann::json::array();
    bool validated_models_present = true;
    for (const auto& model : model_catalog()) {
        std::filesystem::path path = models_dir / model.filename;
        nlohmann::json entry = to_json(model);
        entry["path"] = path.string();
        entry["found"] = std::filesystem::exists(path);
        if (entry["found"]) {
            entry["size_bytes"] = std::filesystem::file_size(path);
        }
        if (!model.validated_platforms.empty() && model.packaged_for_macos) {
            validated_models_present = validated_models_present && entry["found"].get<bool>();
        }
        models.push_back(entry);
    }
    report["models"] = models;
    report["presets"] = list_presets();
    report["summary"]["bundle_healthy"] = report["bundle"]["healthy"];
    report["summary"]["video_healthy"] = report["video"]["healthy"];
    report["summary"]["cache_healthy"] = report["cache"]["healthy"];
    report["summary"]["coreml_healthy"] =
        !report["coreml"]["applicable"].get<bool>() || report["coreml"]["healthy"].get<bool>();
    report["summary"]["apple_acceleration_probe_ready"] =
        !report["mlx"]["applicable"].get<bool>() ||
        (report["mlx"]["probe_available"].get<bool>() &&
         report["mlx"]["primary_pack_ready"].get<bool>());
    report["summary"]["apple_acceleration_bridge_ready"] =
        !report["mlx"]["applicable"].get<bool>() || report["mlx"]["bridge_ready"].get<bool>();
    report["summary"]["apple_acceleration_backend_integrated"] =
        !report["mlx"]["applicable"].get<bool>() || report["mlx"]["backend_integrated"].get<bool>();
    report["summary"]["apple_acceleration_healthy"] =
        !report["mlx"]["applicable"].get<bool>() || report["mlx"]["healthy"].get<bool>();
    report["summary"]["validated_models_present"] = validated_models_present;
    report["summary"]["healthy"] = report["summary"]["bundle_healthy"].get<bool>() &&
                                   report["summary"]["video_healthy"].get<bool>() &&
                                   report["summary"]["cache_healthy"].get<bool>() &&
                                   report["summary"]["apple_acceleration_healthy"].get<bool>() &&
                                   validated_models_present;

    return report;
}

nlohmann::json JobOrchestrator::run_benchmark(const JobRequest& request) {
    nlohmann::json results;
    if (request.input_path.empty()) {
        common::StageProfiler profiler;
        auto stage_callback = [&](const StageTiming& timing) { profiler.record(timing); };

        auto engine_res = profiler.measure("engine_create", [&]() {
            return Engine::create(request.model_path, request.device, stage_callback);
        });
        if (!engine_res) {
            results["error"] = engine_res.error().message;
            return results;
        }
        auto engine = std::move(*engine_res);

        InferenceParams params = request.params;
        int res = params.target_resolution > 0 ? params.target_resolution
                                               : engine->recommended_resolution();
        ImageBuffer rgb_buf(res, res, 3);
        ImageBuffer hint_buf(res, res, 1);
        std::fill(rgb_buf.view().data.begin(), rgb_buf.view().data.end(), 0.0f);
        std::fill(hint_buf.view().data.begin(), hint_buf.view().data.end(), 0.0f);

        double cold_latency_ms = 0.0;
        const int warmup_runs = 2;
        const int benchmark_runs = 5;
        std::vector<double> warmup_latencies;
        std::vector<double> benchmark_latencies;

        {
            auto cold_start = std::chrono::steady_clock::now();
            auto cold_res = profiler.measure(
                "benchmark_cold_frame",
                [&]() {
                    return engine->process_frame(rgb_buf.view(), hint_buf.view(), params,
                                                 stage_callback);
                },
                1);
            if (!cold_res) {
                results["error"] = cold_res.error().message;
                return results;
            }
            auto cold_end = std::chrono::steady_clock::now();
            cold_latency_ms =
                std::chrono::duration<double, std::milli>(cold_end - cold_start).count();
        }

        for (int i = 0; i < warmup_runs; ++i) {
            auto warmup_start = std::chrono::steady_clock::now();
            auto warmup_res = profiler.measure(
                "benchmark_warmup_frame",
                [&]() {
                    return engine->process_frame(rgb_buf.view(), hint_buf.view(), params,
                                                 stage_callback);
                },
                1);
            if (!warmup_res) {
                results["error"] = warmup_res.error().message;
                return results;
            }
            auto warmup_end = std::chrono::steady_clock::now();
            warmup_latencies.push_back(
                std::chrono::duration<double, std::milli>(warmup_end - warmup_start).count());
        }

        auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < benchmark_runs; ++i) {
            auto frame_start = std::chrono::steady_clock::now();
            auto frame_res = profiler.measure(
                "benchmark_frame_total",
                [&]() {
                    return engine->process_frame(rgb_buf.view(), hint_buf.view(), params,
                                                 stage_callback);
                },
                1);
            if (!frame_res) {
                results["error"] = frame_res.error().message;
                return results;
            }
            auto frame_end = std::chrono::steady_clock::now();
            benchmark_latencies.push_back(
                std::chrono::duration<double, std::milli>(frame_end - frame_start).count());
        }
        auto end = std::chrono::steady_clock::now();

        double total_ms = std::chrono::duration<double, std::milli>(end - start).count();
        profiler.record("benchmark_total", total_ms, benchmark_runs);

        results["mode"] = "synthetic";
        results["model"] = request.model_path.filename().string();
        results["resolution"] = res;
        results["requested_device"] = request.device.name;
        results["device"] = engine->current_device().name;
        results["backend"] = backend_to_string(engine->current_device().backend);
        results["warmup_runs"] = warmup_runs;
        results["benchmark_runs"] = benchmark_runs;
        results["cold_latency_ms"] = cold_latency_ms;
        results["avg_latency_ms"] = total_ms / benchmark_runs;
        results["fps"] = total_ms > 0.0 ? (1000.0 * benchmark_runs) / total_ms : 0.0;
        results["latency_ms"]["warmup"] = summarize_latency_samples(warmup_latencies);
        results["latency_ms"]["steady_state"] = summarize_latency_samples(benchmark_latencies);
        results["stage_timings"] = nlohmann::json::array();
        for (const auto& timing : profiler.snapshot()) {
            results["stage_timings"].push_back(to_json(timing));
        }
        if (engine->backend_fallback().has_value()) {
            results["fallback"] = to_json(*engine->backend_fallback());
        }
        return results;
    }

    JobRequest benchmark_request = request;
    bool video_input = std::filesystem::is_regular_file(benchmark_request.input_path) &&
                       is_video_file(benchmark_request.input_path);
    bool cleanup_output = benchmark_request.output_path.empty();
    if (cleanup_output) {
        benchmark_request.output_path = make_benchmark_output_path(benchmark_request, video_input);
    }

    std::vector<StageTiming> timings;
    Backend selected_backend = Backend::Auto;
    std::string selected_device_name = benchmark_request.device.name;
    std::optional<BackendFallbackInfo> fallback;

    auto start = std::chrono::steady_clock::now();
    auto run_res = run(benchmark_request, nullptr, [&](const JobEvent& event) {
        if (event.backend != Backend::Auto) {
            selected_backend = event.backend;
            if (!event.message.empty()) {
                selected_device_name = event.message;
            }
        }
        if (event.fallback.has_value()) {
            fallback = event.fallback;
        }
        if (event.type == JobEventType::Completed || event.type == JobEventType::Failed ||
            event.type == JobEventType::Cancelled) {
            timings = event.timings;
        }
        return true;
    });
    auto end = std::chrono::steady_clock::now();

    double total_ms = std::chrono::duration<double, std::milli>(end - start).count();
    std::uint64_t units = processed_units_from_timings(timings);

    results["mode"] = "workload";
    results["model"] = benchmark_request.model_path.filename().string();
    results["input"] = benchmark_request.input_path.string();
    results["requested_device"] = benchmark_request.device.name;
    results["device"] = selected_device_name;
    results["backend"] = backend_to_string(selected_backend);
    results["total_duration_ms"] = total_ms;
    if (units > 0) {
        results["processed_units"] = units;
        results["throughput_units_per_second"] = total_ms > 0.0 ? (1000.0 * units) / total_ms : 0.0;
        results["avg_unit_latency_ms"] = total_ms / static_cast<double>(units);
    }
    results["stage_timings"] = nlohmann::json::array();
    for (const auto& timing : timings) {
        results["stage_timings"].push_back(to_json(timing));
    }
    if (fallback.has_value()) {
        results["fallback"] = to_json(*fallback);
    }
    if (!run_res) {
        results["error"] = run_res.error().message;
    }
    if (!cleanup_output) {
        results["output_path"] = benchmark_request.output_path.string();
    }

    if (cleanup_output) {
        cleanup_benchmark_output(benchmark_request.output_path);
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
