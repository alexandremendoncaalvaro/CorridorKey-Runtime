#include "job_orchestrator.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <corridorkey/version.hpp>
#include <optional>

#include "../frame_io/video_io.hpp"
#include "common/hardware_telemetry.hpp"
#include "common/stage_profiler.hpp"
#include "hardware_profile.hpp"
#include "output_path_utils.hpp"
#include "runtime_contracts.hpp"
#include "runtime_diagnostics.hpp"

namespace corridorkey::app {

namespace {

nlohmann::json metrics_to_json(const common::SystemMetrics& m) {
    nlohmann::json j;
    j["ram_usage_mb"] = m.ram_usage_mb;
    j["cpu_usage_percent"] = m.cpu_usage_percent;
    return j;
}

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

std::string stage_group_name(std::string_view stage_name) {
    const auto contains = [&](std::string_view token) {
        return stage_name.find(token) != std::string_view::npos;
    };

    if (stage_name == "job_total" || stage_name == "benchmark_total") {
        return "total";
    }
    if (contains("compile") || contains("warmup")) {
        return "warmup_compile";
    }
    if (contains("write") || contains("encode") || contains("artifact")) {
        return "write_output";
    }
    if (contains("prepare") || contains("engine_create") || contains("hardware_strategy") ||
        contains("collect_inputs") || contains("session_create")) {
        return "prepare";
    }
    if (contains("render") || contains("infer") || contains("_run") || contains("pipeline") ||
        contains("frame_total")) {
        return "execute";
    }
    return "other";
}

Result<std::filesystem::path> make_benchmark_output_path(const JobRequest& request,
                                                         bool video_input) {
    auto temp_root = std::filesystem::temp_directory_path() / "corridorkey-benchmark";
    std::filesystem::create_directories(temp_root);

    auto token = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    auto stem = request.input_path.stem().string();
    if (stem.empty()) {
        stem = "synthetic";
    }

    if (video_input) {
        VideoFrameFormat input_format;
        return resolve_video_output_path(temp_root / (stem + "_" + token), request.video_output,
                                         input_format);
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

Result<void> prepare_output_path(const std::filesystem::path& output_path) {
    std::error_code error;

    if (output_path.empty()) {
        return Unexpected(Error{ErrorCode::InvalidParameters, "Output path must not be empty."});
    }

    if (std::filesystem::is_directory(output_path, error) || output_path.extension().empty()) {
        if (!std::filesystem::create_directories(output_path, error) && error) {
            return Unexpected(Error{ErrorCode::IoError,
                                    "Could not create output directory: " + output_path.string() +
                                        " (" + error.message() + ")"});
        }
        return {};
    }

    auto parent = output_path.parent_path();
    if (parent.empty()) {
        return {};
    }

    if (!std::filesystem::create_directories(parent, error) && error) {
        return Unexpected(
            Error{ErrorCode::IoError, "Could not create output directory: " + parent.string() +
                                          " (" + error.message() + ")"});
    }

    return {};
}

bool report_flag(const nlohmann::json& report, const char* section, const char* field,
                 bool default_value = false) {
    if (!report.contains(section) || !report[section].is_object()) {
        return default_value;
    }
    return report[section].value(field, default_value);
}

std::string report_string(const nlohmann::json& report, const char* section, const char* field) {
    if (!report.contains(section) || !report[section].is_object()) {
        return "";
    }
    return report[section].value(field, "");
}

bool packaged_profile_matches_active_profile(const nlohmann::json& report) {
    if (!report.contains("optimization_profile") || !report["optimization_profile"].is_object()) {
        return true;
    }

    const auto bundle_profile = report_string(report, "bundle", "model_profile");
    const auto active_profile = report["optimization_profile"].value("id", "");
    if (bundle_profile.empty() || active_profile.empty()) {
        return true;
    }

    if (bundle_profile == "rtx-lite" || bundle_profile == "rtx-stable") {
        return active_profile == "windows-rtx-lite";
    }
    if (bundle_profile == "rtx-full") {
        return active_profile == "windows-rtx-full";
    }

    return true;
}

std::size_t count_models_with_artifact_state(const nlohmann::json& models, const char* field) {
    if (!models.is_array()) {
        return 0;
    }

    return static_cast<std::size_t>(std::count_if(models.begin(), models.end(), [&](const auto& entry) {
        return entry.contains("artifact_state") && entry["artifact_state"].is_object() &&
               entry["artifact_state"].value(field, false);
    }));
}

std::pair<bool, bool> windows_packaged_model_presence(const nlohmann::json& report,
                                                      const nlohmann::json& models) {
    const auto bundle = report.contains("bundle") ? report["bundle"] : nlohmann::json::object();
    if (bundle.value("packaged_layout_detected", false) && bundle.contains("packaged_models") &&
        bundle["packaged_models"].is_array()) {
        const auto& packaged_models = bundle["packaged_models"];
        const bool any_models = !packaged_models.empty();
        const bool all_found =
            std::all_of(packaged_models.begin(), packaged_models.end(), [](const auto& entry) {
                return entry.value("found", false);
            });
        return {any_models, !any_models || all_found};
    }

    bool packaged_windows_models_present = true;
    bool any_packaged_windows_model = false;
    for (const auto& entry : models) {
        if (!entry.value("packaged_for_windows", false)) {
            continue;
        }
        any_packaged_windows_model = true;
        packaged_windows_models_present =
            packaged_windows_models_present && entry.value("found", false);
    }

    return {any_packaged_windows_model,
            !any_packaged_windows_model || packaged_windows_models_present};
}

struct ResolvedVideoOutput {
    std::filesystem::path output_path;
    std::optional<std::string> warning;
};

Result<ResolvedVideoOutput> resolve_video_output_for_request(const JobRequest& request) {
    if (request.output_path.empty()) {
        return Unexpected(
            Error{ErrorCode::InvalidParameters, "Output path must not be empty for video jobs."});
    }

    std::filesystem::path output_path = request.output_path;
    std::error_code error;
    bool output_is_directory = std::filesystem::exists(output_path, error) &&
                               std::filesystem::is_directory(output_path, error);

    if (output_is_directory) {
        auto stem = request.input_path.stem().string();
        if (stem.empty()) {
            stem = "output";
        }
        output_path = output_path / (stem + "_out");
    }

    bool has_extension = !output_path.extension().empty();
    if (has_extension && request.video_output.mode != VideoOutputMode::Lossless) {
        return ResolvedVideoOutput{output_path, std::nullopt};
    }
    if (has_extension && request.video_output.allow_lossy_fallback) {
        return ResolvedVideoOutput{output_path, std::nullopt};
    }

    auto reader_res = VideoReader::open(request.input_path);
    if (!reader_res) {
        return Unexpected(reader_res.error());
    }
    VideoFrameFormat input_format = (*reader_res)->format();

    if (has_extension) {
        auto plan = resolve_video_output_plan(output_path, request.video_output, input_format);
        if (plan) {
            return ResolvedVideoOutput{output_path, std::nullopt};
        }

        std::filesystem::path base_path = output_path;
        base_path.replace_extension();
        auto fallback_path =
            resolve_video_output_path(base_path, request.video_output, input_format);
        if (fallback_path) {
            std::string extension = output_path.extension().string();
            std::transform(extension.begin(), extension.end(), extension.begin(),
                           [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
            std::string warning = "Lossless output is not available for " + extension +
                                  ". Writing lossless output to " + fallback_path->string() +
                                  " instead. Use --video-encode balanced to keep " + extension +
                                  " with compression.";
            return ResolvedVideoOutput{*fallback_path, warning};
        }

        return Unexpected(plan.error());
    }

    auto resolved_path = resolve_video_output_path(output_path, request.video_output, input_format);
    if (!resolved_path) {
        return Unexpected(resolved_path.error());
    }
    return ResolvedVideoOutput{*resolved_path, std::nullopt};
}

}  // namespace

Result<void> JobOrchestrator::run(const JobRequest& request, ProgressCallback on_progress,
                                  JobEventCallback on_event) {
    JobRequest req = request;
    req.output_path = normalize_runtime_output_path(req.output_path);
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
    auto output_prepare_res =
        profiler.measure("output_prepare", [&]() { return prepare_output_path(req.output_path); });
    if (!output_prepare_res) {
        auto failed_event = JobEvent{
            JobEventType::Failed,
            "prepare",
            0.0F,
            engine->current_device().backend,
            "Output path preparation failed",
            "",
            output_prepare_res.error(),
        };
        failed_event.timings = finalize_timings(profiler, job_start, total_recorded);
        auto emit_res = emit_event(on_event, failed_event);
        if (!emit_res) return Unexpected(emit_res.error());
        return Unexpected(output_prepare_res.error());
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

        // Capture system metrics without performance cost
        auto metrics = common::get_current_metrics();
        progress_event.metrics = metrics_to_json(metrics);

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

    bool input_is_video =
        std::filesystem::is_regular_file(req.input_path) && is_video_file(req.input_path);
    if (input_is_video) {
        auto resolved_output = resolve_video_output_for_request(req);
        if (!resolved_output) {
            return finalize_failure(resolved_output.error());
        }
        req.output_path = resolved_output->output_path;
        if (resolved_output->warning.has_value()) {
            auto warning_event = JobEvent{
                JobEventType::Warning,     "prepare", 0.0F, engine->current_device().backend,
                *resolved_output->warning,
            };
            auto emit_warning_res = emit_event(on_event, warning_event);
            if (!emit_warning_res) return Unexpected(emit_warning_res.error());
        }
    }

    // 3. Dispatch to Video or Sequence
    if (input_is_video) {
        auto result = profiler.measure("video_pipeline", [&]() {
            return engine->process_video(req.input_path, req.hint_path, req.output_path, req.params,
                                         req.video_output, report_progress, stage_callback);
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
    auto capabilities = runtime_capabilities();
    info["capabilities"] = to_json(capabilities);

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

    auto active_device = preferred_runtime_device(capabilities, devices);
    if (active_device.has_value()) {
        nlohmann::json active_device_json;
        active_device_json["name"] = active_device->name;
        active_device_json["memory_mb"] = active_device->available_memory_mb;
        active_device_json["backend"] = backend_to_string(active_device->backend);
        info["active_device"] = active_device_json;

        auto strategy = HardwareProfile::get_best_strategy(*active_device);
        info["recommendation"]["resolution"] = strategy.target_resolution;
        info["recommendation"]["variant"] = strategy.recommended_variant;
        info["recommendation"]["optimization_profile"] =
            to_json(runtime_optimization_profile_for_device(capabilities, *active_device));
    }
    info["commands"] = {"info",   "doctor",  "benchmark", "download",
                        "models", "presets", "process",   "compile-context"};

    return info;
}

nlohmann::json summarize_stage_groups(const std::vector<StageTiming>& timings) {
    std::vector<StageTiming> grouped;

    for (const auto& timing : timings) {
        const auto group_name = stage_group_name(timing.name);
        auto existing =
            std::find_if(grouped.begin(), grouped.end(), [&](const StageTiming& grouped_timing) {
                return grouped_timing.name == group_name;
            });
        if (existing == grouped.end()) {
            grouped.push_back(StageTiming{group_name, timing.total_ms, timing.sample_count,
                                          timing.work_units});
            continue;
        }

        existing->total_ms += timing.total_ms;
        existing->sample_count += timing.sample_count;
        existing->work_units += timing.work_units;
    }

    static const std::array<std::string_view, 6> order = {"prepare",        "warmup_compile",
                                                          "execute",        "write_output",
                                                          "total",          "other"};
    std::sort(grouped.begin(), grouped.end(), [&](const StageTiming& left, const StageTiming& right) {
        const auto rank = [&](std::string_view name) {
            auto it = std::find(order.begin(), order.end(), name);
            if (it == order.end()) {
                return static_cast<int>(order.size());
            }
            return static_cast<int>(std::distance(order.begin(), it));
        };
        return rank(left.name) < rank(right.name);
    });

    nlohmann::json json = nlohmann::json::array();
    for (const auto& timing : grouped) {
        json.push_back(to_json(timing));
    }
    return json;
}

nlohmann::json summarize_doctor_report(const nlohmann::json& report) {
    nlohmann::json summary;
    const auto platform = report["system"]["capabilities"].value("platform", "");
    const bool bundle_healthy = report_flag(report, "bundle", "healthy", false);
    const bool video_healthy = report_flag(report, "video", "healthy", false);
    const bool cache_healthy = report_flag(report, "cache", "healthy", false);
    const bool coreml_healthy =
        !report_flag(report, "coreml", "applicable", false) ||
        report_flag(report, "coreml", "healthy", false);
    const bool apple_probe_ready =
        !report_flag(report, "mlx", "applicable", false) ||
        (report_flag(report, "mlx", "probe_available", false) &&
         report_flag(report, "mlx", "primary_pack_ready", false));
    const bool apple_bridge_ready =
        !report_flag(report, "mlx", "applicable", false) ||
        report_flag(report, "mlx", "bridge_ready", false);
    const bool apple_backend_integrated =
        !report_flag(report, "mlx", "applicable", false) ||
        report_flag(report, "mlx", "backend_integrated", false);
    const bool apple_healthy =
        !report_flag(report, "mlx", "applicable", false) ||
        report_flag(report, "mlx", "healthy", false);
    const bool windows_provider_ready =
        !report_flag(report, "windows_universal", "applicable", false) ||
        report_flag(report, "windows_universal", "provider_available", false);
    const bool windows_execution_ready =
        !report_flag(report, "windows_universal", "applicable", false) ||
        report_flag(report, "windows_universal", "backend_integrated", false);

    const auto [any_packaged_windows_model, packaged_windows_models_present] =
        windows_packaged_model_presence(report, report["models"]);
    bool validated_models_present = true;
    if (platform == "windows") {
        validated_models_present =
            !any_packaged_windows_model || packaged_windows_models_present;
    } else {
        for (const auto& entry : report["models"]) {
            if (!entry["validated_platforms"].empty() && entry.value("packaged_for_macos", false)) {
                validated_models_present =
                    validated_models_present && entry.value("found", false);
            }
        }
    }

    summary["bundle_healthy"] = bundle_healthy;
    summary["video_healthy"] = video_healthy;
    summary["cache_healthy"] = cache_healthy;
    summary["coreml_healthy"] = coreml_healthy;
    summary["apple_acceleration_probe_ready"] = apple_probe_ready;
    summary["apple_acceleration_bridge_ready"] = apple_bridge_ready;
    summary["apple_acceleration_backend_integrated"] = apple_backend_integrated;
    summary["apple_acceleration_healthy"] = apple_healthy;
    summary["validated_models_present"] = validated_models_present;
    summary["windows_universal_provider_ready"] = windows_provider_ready;
    summary["windows_universal_execution_ready"] = windows_execution_ready;
    summary["windows_universal_packaged_models_present"] =
        !report_flag(report, "windows_universal", "applicable", false) ||
        !any_packaged_windows_model || packaged_windows_models_present;
    summary["windows_universal_preferred_backend"] =
        report["windows_universal"].value("recommended_backend", "");
    summary["windows_universal_preferred_model"] =
        report["windows_universal"].value("recommended_model", "");
    summary["bundle_inventory_contract_healthy"] =
        report_flag(report, "bundle", "model_inventory_contract_complete", true);
    summary["packaged_profile_matches_active_profile"] =
        packaged_profile_matches_active_profile(report);
    summary["certified_model_count"] = count_models_with_artifact_state(report["models"],
                                                                        "certified_for_active_device");
    summary["recommended_model_present"] =
        count_models_with_artifact_state(report["models"], "recommended_for_active_device") > 0;
    summary["windows_universal_healthy"] =
        !report_flag(report, "windows_universal", "applicable", false) ||
        report_flag(report, "windows_universal", "healthy", false);
    summary["healthy"] = bundle_healthy && video_healthy && cache_healthy && apple_healthy &&
                         summary["windows_universal_healthy"].get<bool>() &&
                         validated_models_present &&
                         summary["bundle_inventory_contract_healthy"].get<bool>() &&
                         summary["packaged_profile_matches_active_profile"].get<bool>();
    return summary;
}

nlohmann::json JobOrchestrator::run_doctor(const std::filesystem::path& models_dir) {
    nlohmann::json report;
    report["timestamp"] = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());

    auto capabilities = runtime_capabilities();
    auto devices = list_devices();
    auto active_device = preferred_runtime_device(capabilities, devices);

    report["system"] = get_system_info();
    auto health = inspect_operational_health(models_dir);
    report["executable"] = health["executable"];
    report["bundle"] = health["bundle"];
    report["video"] = health["video"];
    report["cache"] = health["cache"];
    report["coreml"] = health["coreml"];
    report["mlx"] = health["mlx"];
    report["windows_universal"] = health["windows_universal"];

    nlohmann::json models = nlohmann::json::array();
    for (const auto& model : model_catalog()) {
        std::filesystem::path path = models_dir / model.filename;
        nlohmann::json entry = to_json(model);
        entry["path"] = path.string();
        entry["found"] = std::filesystem::exists(path);
        if (entry["found"]) {
            entry["size_bytes"] = std::filesystem::file_size(path);
        }
        if (active_device.has_value()) {
            entry["artifact_state"] =
                to_json(artifact_runtime_state_for_device(model, capabilities, *active_device,
                                                          entry["found"].get<bool>()));
        }
        models.push_back(entry);
    }
    report["models"] = models;
    if (active_device.has_value()) {
        report["active_device"]["name"] = active_device->name;
        report["active_device"]["memory_mb"] = active_device->available_memory_mb;
        report["active_device"]["backend"] = backend_to_string(active_device->backend);
        report["optimization_profile"] =
            to_json(runtime_optimization_profile_for_device(capabilities, *active_device));
    }
    report["presets"] = list_presets();
    report["summary"] = summarize_doctor_report(report);

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
        results["execution_profile"] = to_json(
            runtime_optimization_profile_for_device(runtime_capabilities(),
                                                    engine->current_device()));
        results["warmup_runs"] = warmup_runs;
        results["benchmark_runs"] = benchmark_runs;
        results["cold_latency_ms"] = cold_latency_ms;
        results["avg_latency_ms"] = total_ms / benchmark_runs;
        results["fps"] = total_ms > 0.0 ? (1000.0 * benchmark_runs) / total_ms : 0.0;
        results["latency_ms"]["warmup"] = summarize_latency_samples(warmup_latencies);
        results["latency_ms"]["steady_state"] = summarize_latency_samples(benchmark_latencies);
        results["stage_timings"] = nlohmann::json::array();
        const auto stage_timings = profiler.snapshot();
        for (const auto& timing : stage_timings) {
            results["stage_timings"].push_back(to_json(timing));
        }
        results["phase_timings"] = summarize_stage_groups(stage_timings);
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
        auto output_res = make_benchmark_output_path(benchmark_request, video_input);
        if (!output_res) {
            results["error"] = output_res.error().message;
            return results;
        }
        benchmark_request.output_path = *output_res;
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
    DeviceInfo execution_device = benchmark_request.device;
    execution_device.name = selected_device_name.empty() ? execution_device.name : selected_device_name;
    execution_device.backend = selected_backend == Backend::Auto ? execution_device.backend : selected_backend;
    results["execution_profile"] =
        to_json(runtime_optimization_profile_for_device(runtime_capabilities(), execution_device));
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
    results["phase_timings"] = summarize_stage_groups(timings);
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
