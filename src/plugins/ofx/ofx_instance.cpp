#include <algorithm>
#include <chrono>
#include <corridorkey/engine.hpp>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <new>
#include <sstream>
#include <string>
#include <vector>

#include "app/runtime_contracts.hpp"
#include "common/ofx_runtime_defaults.hpp"
#include "common/runtime_paths.hpp"
#include "ofx_frame_cache.hpp"
#include "ofx_image_utils.hpp"
#include "ofx_logging.hpp"
#include "ofx_model_selection.hpp"
#include "ofx_runtime_client.hpp"
#include "ofx_shared.hpp"

#if defined(__APPLE__)
#include <crt_externs.h>
#include <dlfcn.h>
#include <spawn.h>
#elif defined(_WIN32)
#include <shellapi.h>
#include <windows.h>
#endif

namespace corridorkey::ofx {

namespace {

constexpr const char* kRepoHelpBaseUrl =
    "https://github.com/alexandremendoncaalvaro/CorridorKey-Runtime/blob/"
    "main/help/";

std::string help_doc_url(const char* filename) {
    return std::string(kRepoHelpBaseUrl) + filename;
}

bool open_external_url(const std::string& url) {
#if defined(_WIN32)
    std::wstring wide_url(url.begin(), url.end());
    auto result = reinterpret_cast<std::intptr_t>(
        ShellExecuteW(nullptr, L"open", wide_url.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
    return result > 32;
#elif defined(__APPLE__)
    char* const argv[] = {const_cast<char*>("/usr/bin/open"), const_cast<char*>(url.c_str()),
                          nullptr};
    pid_t pid = 0;
    return posix_spawn(&pid, "/usr/bin/open", nullptr, nullptr, argv, *_NSGetEnviron()) == 0;
#else
    (void)url;
    return false;
#endif
}

std::string backend_label(Backend backend) {
    switch (backend) {
        case Backend::CPU:
            return "cpu";
        case Backend::CoreML:
            return "coreml";
        case Backend::CUDA:
            return "cuda";
        case Backend::TensorRT:
            return "tensorrt";
        case Backend::DirectML:
            return "dml";
        case Backend::WindowsML:
            return "winml";
        case Backend::OpenVINO:
            return "openvino";
        case Backend::MLX:
            return "mlx";
        default:
            return "auto";
    }
}

std::string processing_backend_label(Backend backend) {
    switch (backend) {
        case Backend::CPU:
            return "CPU";
        case Backend::CoreML:
            return "CoreML";
        case Backend::CUDA:
            return "CUDA GPU";
        case Backend::TensorRT:
            return "TensorRT GPU";
        case Backend::DirectML:
            return "DirectML GPU";
        case Backend::WindowsML:
            return "Windows AI";
        case Backend::OpenVINO:
            return "OpenVINO";
        case Backend::MLX:
            return "MLX GPU";
        default:
            return "Auto";
    }
}

std::string processing_device_label(const DeviceInfo& device) {
    if (!device.name.empty()) {
        return device.name;
    }
    return processing_backend_label(device.backend);
}

bool environment_flag_enabled(const char* name) {
    if (auto value = common::environment_variable_copy(name); value.has_value()) {
        return *value == "1" || *value == "true" || *value == "TRUE";
    }
    return false;
}

bool use_runtime_server_default(const std::filesystem::path& runtime_server_path) {
    if (environment_flag_enabled("CORRIDORKEY_OFX_FORCE_INPROCESS")) {
        return false;
    }
    return !runtime_server_path.empty() && std::filesystem::exists(runtime_server_path);
}

bool allow_inprocess_fallback() {
    return environment_flag_enabled("CORRIDORKEY_OFX_ALLOW_INPROCESS_FALLBACK");
}

bool backend_matches_request(const DeviceInfo& effective_device,
                             const DeviceInfo& requested_device);

bool is_windows_gpu_backend(Backend backend) {
    return backend == Backend::TensorRT || backend == Backend::CUDA ||
           backend == Backend::DirectML || backend == Backend::WindowsML ||
           backend == Backend::OpenVINO;
}

std::string execution_engine_label(ExecutionEngine engine) {
    switch (engine) {
        case ExecutionEngine::Official:
            return "ORT TensorRT (Official)";
        case ExecutionEngine::TorchTensorRt:
            return "Torch-TensorRT";
        case ExecutionEngine::Auto:
        default:
            return "Auto (Official)";
    }
}

ExecutionEngine selected_execution_engine(const InstanceData* data) {
    if (data == nullptr || data->execution_engine_param == nullptr) {
        return ExecutionEngine::Auto;
    }

    int choice = kExecutionEngineAuto;
    if (g_suites.parameter->paramGetValue(data->execution_engine_param, &choice) != kOfxStatOK) {
        return ExecutionEngine::Auto;
    }
    return execution_engine_from_choice(choice);
}

ExecutionEngine effective_execution_engine(const InstanceData& data) {
    if (data.use_runtime_server && data.runtime_client != nullptr &&
        data.runtime_client->has_session()) {
        return data.runtime_client->current_execution_engine();
    }
    if (data.engine != nullptr) {
        return data.engine->execution_engine();
    }
    return ExecutionEngine::Auto;
}

bool execution_engine_matches_request(ExecutionEngine requested, ExecutionEngine active) {
    const ExecutionEngine requested_effective =
        requested == ExecutionEngine::Auto ? ExecutionEngine::Official : requested;
    const ExecutionEngine active_effective =
        active == ExecutionEngine::Auto ? ExecutionEngine::Official : active;
    return requested_effective == active_effective;
}

bool execution_engine_selector_enabled(const InstanceData& data) {
#if defined(_WIN32)
    return std::find(data.runtime_capabilities.supported_execution_engines.begin(),
                     data.runtime_capabilities.supported_execution_engines.end(),
                     ExecutionEngine::TorchTensorRt) !=
           data.runtime_capabilities.supported_execution_engines.end();
#else
    (void)data;
    return false;
#endif
}

EngineCreateOptions ofx_engine_options(const DeviceInfo& requested_device,
                                       ExecutionEngine execution_engine,
                                       bool allow_cpu_fallback = false) {
    EngineCreateOptions options;
    if (requested_device.backend != Backend::CPU) {
        options.allow_cpu_fallback = allow_cpu_fallback;
        options.disable_cpu_ep_fallback = false;
    }
    options.execution_engine = execution_engine;
    return options;
}

std::string requested_quality_runtime_label_impl(int quality_mode, int requested_resolution,
                                                 bool cpu_quality_guardrail_active) {
    std::string label;
    if (quality_mode == kQualityAuto && requested_resolution > 0) {
        label = std::string(quality_mode_label(quality_mode)) + " (" +
                "source-size target: " + std::to_string(requested_resolution) + "px)";
    } else {
        label = quality_mode_label(quality_mode);
    }
    if (cpu_quality_guardrail_active) {
        label += " [CPU capped to 512]";
    }
    return label;
}

std::string effective_quality_label(int resolution) {
    if (resolution <= 0) {
        return "Not loaded";
    }
    return std::to_string(resolution) + "px";
}

std::string runtime_artifact_label(const std::filesystem::path& model_path) {
    if (model_path.empty()) {
        return "Not loaded";
    }
    return model_path.filename().string();
}

void sync_runtime_panel_state_from_active_engine(InstanceData* data) {
    if (data == nullptr) {
        return;
    }

    data->runtime_panel_state.requested_quality_mode = data->active_quality_mode;
    data->runtime_panel_state.requested_resolution = data->requested_resolution;
    data->runtime_panel_state.effective_resolution = data->active_resolution;
    data->runtime_panel_state.safe_quality_ceiling_resolution =
        app::max_supported_resolution_for_device(data->device).value_or(0);
    data->runtime_panel_state.cpu_quality_guardrail_active = data->cpu_quality_guardrail_active;
    data->runtime_panel_state.requested_engine = data->requested_execution_engine;
    data->runtime_panel_state.artifact_path = data->model_path;
    if (data->use_runtime_server && data->runtime_client != nullptr &&
        data->runtime_client->has_session()) {
        data->runtime_panel_state.session_prepared = true;
        data->runtime_panel_state.session_ref_count = data->runtime_client->session_ref_count();
        data->runtime_panel_state.effective_engine =
            data->runtime_client->current_execution_engine();
        return;
    }

    data->runtime_panel_state.session_prepared = data->engine != nullptr;
    data->runtime_panel_state.session_ref_count = data->engine != nullptr ? 1 : 0;
    data->runtime_panel_state.effective_engine =
        data->engine != nullptr ? data->engine->execution_engine() : ExecutionEngine::Auto;
}

void set_runtime_panel_state_for_failed_quality_request(
    InstanceData* data, int requested_quality_mode, int requested_resolution,
    bool cpu_quality_guardrail_active, const std::filesystem::path& artifact_path) {
    if (data == nullptr) {
        return;
    }

    data->runtime_panel_state.requested_quality_mode = requested_quality_mode;
    data->runtime_panel_state.requested_resolution = requested_resolution;
    data->runtime_panel_state.effective_resolution = 0;
    data->runtime_panel_state.safe_quality_ceiling_resolution =
        app::max_supported_resolution_for_device(data->device).value_or(0);
    data->runtime_panel_state.cpu_quality_guardrail_active = cpu_quality_guardrail_active;
    data->runtime_panel_state.requested_engine = data->requested_execution_engine;
    data->runtime_panel_state.effective_engine = ExecutionEngine::Auto;
    data->runtime_panel_state.artifact_path = artifact_path;
    data->runtime_panel_state.session_prepared = false;
    data->runtime_panel_state.session_ref_count = 0;
}

bool sync_runtime_panel_session_state_impl(InstanceData* data) {
    if (data == nullptr) {
        return false;
    }

    const bool previous_prepared = data->runtime_panel_state.session_prepared;
    const std::uint64_t previous_ref_count = data->runtime_panel_state.session_ref_count;

    if (data->use_runtime_server && data->runtime_client != nullptr &&
        data->runtime_client->has_session()) {
        data->runtime_panel_state.session_prepared = true;
        data->runtime_panel_state.session_ref_count = data->runtime_client->session_ref_count();
    } else if (data->engine != nullptr) {
        data->runtime_panel_state.session_prepared = true;
        data->runtime_panel_state.session_ref_count = 1;
    } else {
        data->runtime_panel_state.session_prepared = false;
        data->runtime_panel_state.session_ref_count = 0;
    }

    return data->runtime_panel_state.session_prepared != previous_prepared ||
           data->runtime_panel_state.session_ref_count != previous_ref_count;
}

std::uint64_t mix_cache_token(std::uint64_t token, const std::string& value) {
    constexpr std::uint64_t kPrime = 1099511628211ULL;
    for (unsigned char ch : value) {
        token ^= ch;
        token *= kPrime;
    }
    return token;
}

std::uint64_t models_bundle_token(const std::filesystem::path& models_root) {
    constexpr std::uint64_t kOffsetBasis = 1469598103934665603ULL;

    std::error_code error;
    if (!std::filesystem::exists(models_root, error) || error) {
        return 0;
    }

    std::vector<std::string> entries;
    for (const auto& entry : std::filesystem::directory_iterator(models_root, error)) {
        if (error || !entry.is_regular_file()) {
            continue;
        }

        const auto write_time = entry.last_write_time(error);
        if (error) {
            error.clear();
            continue;
        }

        const auto size_bytes = entry.file_size(error);
        if (error) {
            error.clear();
            continue;
        }

        entries.push_back(entry.path().filename().string() + "|" + std::to_string(size_bytes) +
                          "|" + std::to_string(write_time.time_since_epoch().count()));
    }

    std::sort(entries.begin(), entries.end());
    std::uint64_t token = kOffsetBasis;
    for (const auto& entry : entries) {
        token = mix_cache_token(token, entry);
    }
    return token;
}

QualityCompileFailureCacheContext build_quality_compile_failure_cache_context(
    const InstanceData& data, const DeviceInfo& requested_device, int quantization_mode) {
    QualityCompileFailureCacheContext context;
    context.models_root = data.models_root;
    context.models_bundle_token = models_bundle_token(data.models_root);
    context.backend = requested_device.backend;
    context.device_index = requested_device.device_index;
    context.available_memory_mb = requested_device.available_memory_mb;
    context.quantization_mode = quantization_mode;
    return context;
}

bool should_record_quality_compile_failure(Backend backend, const Error& error) {
    return use_quality_compile_failure_cache(backend) && error.code != ErrorCode::IoError;
}

bool should_record_quality_backend_mismatch(Backend backend) {
    return use_quality_compile_failure_cache(backend);
}

std::string truncate_status_message(const std::string& message, std::size_t max_length) {
    if (message.size() <= max_length) {
        return message;
    }
    if (max_length <= 3) {
        return message.substr(0, max_length);
    }
    return message.substr(0, max_length - 3) + "...";
}

std::string format_duration_ms(double ms) {
    if (ms <= 0.0) {
        return "n/a";
    }
    std::ostringstream oss;
    if (ms >= 1000.0) {
        oss << std::fixed << std::setprecision(1) << (ms / 1000.0) << " s";
    } else {
        oss << std::fixed << std::setprecision(1) << ms << " ms";
    }
    return oss.str();
}

std::string runtime_status_runtime_label_impl(const InstanceData& data) {
    if (!data.last_error.empty()) {
        return "Error: " + truncate_status_message(data.last_error, 160);
    }
    std::string status;
    if (!data.color_management_status.empty()) {
        status = truncate_status_message(data.color_management_status, 100);
    }
    if (!data.last_warning.empty()) {
        if (!status.empty()) {
            status += " | ";
        }
        status += "Note: " + truncate_status_message(data.last_warning, 100);
    }
    if (!status.empty()) {
        return status;
    }
    return data.render_count > 0 ? "Ready" : "Idle";
}

std::string runtime_session_runtime_label_impl(const InstanceData& data) {
    if (data.runtime_panel_state.session_prepared) {
        const std::uint64_t shared_node_count =
            std::max<std::uint64_t>(data.runtime_panel_state.session_ref_count, 1);
        if (shared_node_count > 1) {
            return "Shared (" + std::to_string(shared_node_count) + " nodes)";
        }
        return "Dedicated";
    }

    if (!data.last_error.empty()) {
        return "Unavailable";
    }

    return "Loading...";
}

std::string runtime_engine_runtime_label_impl(const InstanceData& data) {
    if (data.runtime_panel_state.effective_engine != ExecutionEngine::Auto) {
        return execution_engine_label(data.runtime_panel_state.effective_engine);
    }
    if (data.runtime_panel_state.requested_engine != ExecutionEngine::Auto) {
        return execution_engine_label(data.runtime_panel_state.requested_engine);
    }
    if (!data.last_error.empty()) {
        return "Unavailable";
    }
    return "Loading...";
}

std::string runtime_safe_quality_ceiling_runtime_label_impl(const InstanceData& data) {
    const int resolution = data.runtime_panel_state.safe_quality_ceiling_resolution;
    if (resolution <= 0) {
        return "Unknown";
    }

    switch (quality_mode_for_resolution(resolution)) {
        case kQualityPreview:
            return "Draft (" + std::to_string(resolution) + "px)";
        case kQualityHigh:
            return "High (" + std::to_string(resolution) + "px)";
        case kQualityUltra:
            return "Ultra (" + std::to_string(resolution) + "px)";
        case kQualityMaximum:
            return "Maximum (" + std::to_string(resolution) + "px)";
        case kQualityAuto:
        default:
            return std::to_string(resolution) + "px";
    }
}

std::string runtime_guide_source_runtime_label_impl(const InstanceData& data) {
    switch (data.last_guide_source) {
        case GuideSourceKind::ExternalAlphaHint:
            return "External Alpha Hint";
        case GuideSourceKind::RoughFallback:
            return "Rough Fallback";
        case GuideSourceKind::Unknown:
        default:
            if (!data.last_error.empty()) {
                return "Unavailable";
            }
            return "Awaiting render";
    }
}

std::string runtime_path_runtime_label_impl(const InstanceData& data) {
    switch (data.last_runtime_path) {
        case RuntimePathKind::Direct:
            return "Direct";
        case RuntimePathKind::ArtifactFallback:
            return "Artifact Fallback";
        case RuntimePathKind::FullModelTiling:
            return "Full-Model Tiling";
        case RuntimePathKind::Unknown:
        default:
            if (!data.last_error.empty()) {
                return "Unavailable";
            }
            return "Awaiting render";
    }
}

std::string runtime_timings_runtime_label_impl(const InstanceData& data) {
    if (data.last_render_work_origin == LastRenderWorkOrigin::None) {
        return "No frame render recorded";
    }

    if (data.last_render_stage_timings.empty()) {
        switch (data.last_render_work_origin) {
            case LastRenderWorkOrigin::SharedCache:
                return "Frame render unavailable | Shared cache";
            case LastRenderWorkOrigin::InstanceCache:
                return "Frame render unavailable | Instance cache";
            case LastRenderWorkOrigin::BackendRender:
                return "Frame render unavailable";
            case LastRenderWorkOrigin::None:
            default:
                return "No frame render recorded";
        }
    }

    double total_ms = 0.0;
    const StageTiming* hottest_stage = nullptr;
    for (const auto& timing : data.last_render_stage_timings) {
        total_ms += timing.total_ms;
        if (hottest_stage == nullptr || timing.total_ms > hottest_stage->total_ms) {
            hottest_stage = &timing;
        }
    }

    std::string label = "Frame render: " + format_duration_ms(total_ms);
    switch (data.last_render_work_origin) {
        case LastRenderWorkOrigin::SharedCache:
            label += " | Shared cache";
            break;
        case LastRenderWorkOrigin::InstanceCache:
            label += " | Instance cache";
            break;
        case LastRenderWorkOrigin::BackendRender:
        case LastRenderWorkOrigin::None:
        default:
            break;
    }
    if (hottest_stage != nullptr && hottest_stage->total_ms > 0.0 && !hottest_stage->name.empty()) {
        label += " | Hotspot: " + truncate_status_message(hottest_stage->name, 36) + " " +
                 format_duration_ms(hottest_stage->total_ms);
    }
    return label;
}

std::string runtime_backend_work_runtime_label_impl(const InstanceData& data) {
    switch (data.last_render_work_origin) {
        case LastRenderWorkOrigin::SharedCache:
            return "Shared cache hit";
        case LastRenderWorkOrigin::InstanceCache:
            return "Instance cache hit";
        case LastRenderWorkOrigin::BackendRender:
            return "Backend render";
        case LastRenderWorkOrigin::None:
        default:
            return "No backend work recorded";
    }
}

void clear_instance_render_caches(InstanceData* data, bool clear_timings) {
    if (data == nullptr) {
        return;
    }

    data->cached_result = {};
    data->cached_result_valid = false;
    data->cached_time = 0.0;
    data->cached_width = 0;
    data->cached_height = 0;
    data->cached_signature = 0;
    data->cached_signature_valid = false;
    data->cached_params = {};
    data->cached_model_path.clear();
    data->cached_render_stage_timings.clear();
    data->cached_screen_color = kDefaultScreenColor;
    data->cached_alpha_black_point = 0.0;
    data->cached_alpha_white_point = 1.0;
    data->cached_alpha_erode = 0.0;
    data->cached_alpha_softness = 0.0;
    data->cached_alpha_gamma = 1.0;
    data->cached_temporal_smoothing = kDefaultTemporalSmoothing;

    data->temporal_alpha = {};
    data->temporal_foreground = {};
    data->temporal_state_valid = false;
    data->temporal_time = 0.0;
    data->temporal_width = 0;
    data->temporal_height = 0;
    data->render_count = 0;

    if (clear_timings) {
        data->last_frame_ms = 0.0;
        data->avg_frame_ms = 0.0;
        data->frame_time_samples = 0;
        data->last_render_work_origin = LastRenderWorkOrigin::None;
        data->last_render_stage_timings.clear();
    }

    data->runtime_panel_dirty = true;
}

void invalidate_active_runtime_session(InstanceData* data, std::string_view reason) {
    if (data == nullptr) {
        return;
    }

    if (data->runtime_client != nullptr) {
        auto release_result = data->runtime_client->release_session();
        if (!release_result) {
            log_message("invalidate_active_runtime_session",
                        std::string(reason) + " release_failed=" + release_result.error().message);
        }
    }

    data->engine.reset();
    data->active_execution_engine = ExecutionEngine::Auto;
    data->runtime_panel_state.effective_engine = ExecutionEngine::Auto;
    clear_instance_render_caches(data, true);
}

double elapsed_ms_since(std::chrono::steady_clock::time_point start_time) {
    const auto elapsed = std::chrono::steady_clock::now() - start_time;
    return std::chrono::duration<double, std::milli>(elapsed).count();
}

void log_stage_timing(std::string_view scope, std::string_view phase,
                      const DeviceInfo& requested_device,
                      const std::filesystem::path& artifact_path, int requested_resolution,
                      int effective_resolution, const StageTiming& timing) {
    log_message(scope, "event=stage phase=" + std::string(phase) + " stage=" + timing.name +
                           " total_ms=" + std::to_string(timing.total_ms) +
                           " requested_backend=" + backend_label(requested_device.backend) +
                           " artifact=" + artifact_path.filename().string() +
                           " requested_resolution=" + std::to_string(requested_resolution) +
                           " effective_resolution=" + std::to_string(effective_resolution));
}

void log_engine_event(std::string_view scope, std::string_view event, std::string_view phase,
                      const DeviceInfo& requested_device, const DeviceInfo& effective_device,
                      const std::filesystem::path& artifact_path, int requested_resolution,
                      int effective_resolution,
                      const std::optional<BackendFallbackInfo>& fallback = std::nullopt,
                      std::string_view detail = {}) {
    std::string message = "event=" + std::string(event) + " phase=" + std::string(phase) +
                          " requested_backend=" + backend_label(requested_device.backend) +
                          " effective_backend=" + backend_label(effective_device.backend) +
                          " requested_device=" + requested_device.name +
                          " effective_device=" + effective_device.name +
                          " artifact=" + artifact_path.filename().string() +
                          " requested_resolution=" + std::to_string(requested_resolution) +
                          " effective_resolution=" + std::to_string(effective_resolution);
    if (fallback.has_value() && !fallback->reason.empty()) {
        message += " fallback_reason=" + fallback->reason;
    }
    if (!detail.empty()) {
        message += " detail=" + std::string(detail);
    }
    log_message(scope, message);
}

void set_string_param_value(OfxParamHandle param, const std::string& value) {
    if (g_suites.parameter == nullptr || param == nullptr) {
        return;
    }
    g_suites.parameter->paramSetValue(param, value.c_str());
}

bool get_bool_param_value(OfxParamHandle param, bool default_value = false) {
    if (g_suites.parameter == nullptr || param == nullptr) {
        return default_value;
    }

    int value = default_value ? 1 : 0;
    if (g_suites.parameter->paramGetValue(param, &value) != kOfxStatOK) {
        return default_value;
    }
    return value != 0;
}

void append_status_note(std::string& status, const std::string& note) {
    if (note.empty()) {
        return;
    }
    if (!status.empty()) {
        status += " | ";
    }
    status += note;
}

bool allow_cpu_fallback_requested(const InstanceData* data) {
    return data != nullptr && get_bool_param_value(data->allow_cpu_fallback_param, false);
}

bool should_reroute_int8_to_cpu(const DeviceInfo& requested_device, int quantization_mode,
                                bool allow_cpu_fallback) {
    return allow_cpu_fallback && quantization_mode == kQuantizationInt8 &&
           is_windows_gpu_backend(requested_device.backend);
}

DeviceInfo cpu_fallback_device_for_request(const DeviceInfo& requested_device) {
    DeviceInfo cpu_device = requested_device;
    cpu_device.name = "Generic CPU";
    cpu_device.backend = Backend::CPU;
    cpu_device.device_index = 0;
    return cpu_device;
}

std::string cpu_fallback_warning_message(bool rerouted_to_cpu) {
    if (!rerouted_to_cpu) {
        return {};
    }
    return "INT8 (Experimental) is running on CPU because Allow CPU Fallback is enabled.";
}

std::string cpu_quality_guardrail_warning_message(int requested_quality_mode,
                                                  bool cpu_quality_guardrail_active) {
    if (!cpu_quality_guardrail_active) {
        return {};
    }
    return "CPU backend is limited to Draft (512) for interactive rendering. " +
           std::string(quality_mode_label(requested_quality_mode)) + " will run as Draft (512).";
}

std::string manual_override_warning_message(const DeviceInfo& requested_device, int quality_mode,
                                            int requested_resolution,
                                            bool allow_unrestricted_quality_attempt) {
    if (!allow_unrestricted_quality_attempt || !is_fixed_quality_mode(quality_mode)) {
        return {};
    }

    auto safe_quality_ceiling = app::max_supported_resolution_for_device(requested_device);
    if (!safe_quality_ceiling.has_value() || requested_resolution <= *safe_quality_ceiling) {
        return {};
    }

    return std::string("Manual quality override above the current safe ceiling: ") +
           quality_mode_label(quality_mode) + " (" + std::to_string(requested_resolution) +
           "px) is being attempted directly. Safe ceiling: " +
           quality_mode_label(quality_mode_for_resolution(*safe_quality_ceiling)) + ".";
}

bool allow_unrestricted_quality_attempt_for_request_impl(const InstanceData& data, int quality_mode,
                                                         const DeviceInfo& requested_device) {
    return is_fixed_quality_mode(quality_mode) &&
           app::runtime_optimization_profile_for_device(data.runtime_capabilities, requested_device)
               .unrestricted_quality_attempt;
}

void update_runtime_panel_values(InstanceData* data) {
    if (data == nullptr) {
        return;
    }

    sync_runtime_panel_session_state_impl(data);

    bool has_session = false;
    if (data->use_runtime_server) {
        has_session = data->runtime_client != nullptr && data->runtime_client->has_session();
    } else {
        has_session = data->engine != nullptr;
    }
    const bool is_loading = !has_session && data->last_error.empty();

    set_string_param_value(data->runtime_processing_param,
                           processing_backend_label(data->device.backend));
    set_string_param_value(data->runtime_device_param, processing_device_label(data->device));
    set_string_param_value(data->runtime_engine_param,
                           is_loading ? "Loading..." : runtime_engine_runtime_label_impl(*data));
    set_string_param_value(
        data->runtime_requested_quality_param,
        requested_quality_runtime_label(data->runtime_panel_state.requested_quality_mode,
                                        data->runtime_panel_state.requested_resolution,
                                        data->runtime_panel_state.cpu_quality_guardrail_active));
    set_string_param_value(
        data->runtime_effective_quality_param,
        is_loading ? "Loading..."
                   : effective_quality_label(data->runtime_panel_state.effective_resolution));
    set_string_param_value(
        data->runtime_safe_quality_ceiling_param,
        is_loading ? "Loading..." : runtime_safe_quality_ceiling_runtime_label(*data));
    set_string_param_value(data->runtime_artifact_param,
                           is_loading
                               ? "Loading..."
                               : runtime_artifact_label(data->runtime_panel_state.artifact_path));
    set_string_param_value(data->runtime_guide_source_param,
                           is_loading ? "Loading..." : runtime_guide_source_runtime_label(*data));
    set_string_param_value(data->runtime_path_param,
                           is_loading ? "Loading..." : runtime_path_runtime_label(*data));
    set_string_param_value(data->runtime_session_param, runtime_session_runtime_label(*data));
    set_string_param_value(data->runtime_status_param,
                           is_loading ? "Loading..." : runtime_status_runtime_label(*data));
    set_string_param_value(data->runtime_timings_param,
                           is_loading ? "Loading..." : runtime_timings_runtime_label(*data));
    set_string_param_value(data->runtime_backend_work_param,
                           is_loading ? "Loading..." : runtime_backend_work_runtime_label(*data));
}

void set_runtime_panel_status(InstanceData* data, const std::string& processing,
                              const std::string& device, const std::string& engine,
                              const std::string& requested_quality,
                              const std::string& effective_quality, const std::string& artifact,
                              const std::string& session, const std::string& status,
                              const std::string& timings, const std::string& backend_work) {
    if (data == nullptr) {
        return;
    }

    set_string_param_value(data->runtime_processing_param, processing);
    set_string_param_value(data->runtime_device_param, device);
    set_string_param_value(data->runtime_engine_param, engine);
    set_string_param_value(data->runtime_requested_quality_param, requested_quality);
    set_string_param_value(data->runtime_effective_quality_param, effective_quality);
    set_string_param_value(data->runtime_artifact_param, artifact);
    set_string_param_value(data->runtime_session_param, session);
    set_string_param_value(data->runtime_status_param, status);
    set_string_param_value(data->runtime_timings_param, timings);
    set_string_param_value(data->runtime_backend_work_param, backend_work);
}

bool backend_matches_request(const Engine& engine, const DeviceInfo& requested_device) {
    return backend_matches_request(engine.current_device(), requested_device);
}

bool backend_matches_request(const DeviceInfo& effective_device,
                             const DeviceInfo& requested_device) {
    if (requested_device.backend == Backend::CPU) {
        return true;
    }
    return effective_device.backend == requested_device.backend;
}

std::string backend_mismatch_message(const std::string& operation,
                                     const DeviceInfo& requested_device, const Engine& engine,
                                     const std::filesystem::path& artifact_path) {
    return operation + " requested backend " + backend_label(requested_device.backend) + " for " +
           artifact_path.filename().string() + " but the engine is using " +
           backend_label(engine.current_device().backend) + ".";
}

std::string backend_mismatch_message(const std::string& operation,
                                     const DeviceInfo& requested_device, const Engine& engine,
                                     const std::filesystem::path& artifact_path,
                                     const std::optional<BackendFallbackInfo>& fallback) {
    std::string message =
        backend_mismatch_message(operation, requested_device, engine, artifact_path);
    if (fallback.has_value() && !fallback->reason.empty()) {
        message += " Reason: " + fallback->reason;
    }
    return message;
}

std::optional<std::filesystem::path> plugin_module_path() {
#if defined(_WIN32)
    HMODULE module = nullptr;
    auto address = reinterpret_cast<LPCWSTR>(&plugin_module_path);
    if (!GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            address, &module)) {
        log_message("plugin_module_path", "GetModuleHandleExW failed.");
        return std::nullopt;
    }

    std::wstring buffer(MAX_PATH, L'\0');
    DWORD length = GetModuleFileNameW(module, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0) {
        log_message("plugin_module_path", "GetModuleFileNameW returned empty path.");
        return std::nullopt;
    }
    buffer.resize(length);
    return std::filesystem::path(buffer);
#elif defined(__APPLE__)
    Dl_info info{};
    if (dladdr(reinterpret_cast<void*>(&plugin_module_path), &info) == 0 ||
        info.dli_fname == nullptr) {
        log_message("plugin_module_path", "dladdr failed to resolve module path.");
        return std::nullopt;
    }
    return std::filesystem::path(info.dli_fname);
#else
    return std::nullopt;
#endif
}

std::filesystem::path resolve_models_root() {
    if (auto override_path = common::environment_variable_copy("CORRIDORKEY_MODELS_DIR");
        override_path.has_value()) {
        log_message("resolve_models_root", std::string("Using override: ") + *override_path);
        return std::filesystem::path(*override_path);
    }

    if (auto module_path = plugin_module_path(); module_path.has_value()) {
        auto resources = module_path->parent_path().parent_path() / "Resources" / "models";
        std::error_code error;
        if (std::filesystem::exists(resources, error) && !error) {
            log_message("resolve_models_root",
                        std::string("Using bundle resources: ") + resources.string());
            return resources;
        }
    }

    auto fallback = common::default_models_root();
    log_message("resolve_models_root", std::string("Using fallback: ") + fallback.string());
    return fallback;
}

app::OfxRuntimePrepareSessionRequest build_prepare_request(
    const BootstrapEngineCandidate& candidate, ExecutionEngine execution_engine,
    bool allow_cpu_fallback = false) {
    app::OfxRuntimePrepareSessionRequest request;
    request.client_instance_id = "bootstrap";
    request.model_path = candidate.executable_model_path;
    request.artifact_name = candidate.requested_model_path.filename().string();
    request.requested_device = candidate.device;
    request.engine_options =
        ofx_engine_options(candidate.device, execution_engine, allow_cpu_fallback);
    request.requested_quality_mode = kQualityAuto;
    request.requested_resolution = candidate.requested_resolution;
    request.effective_resolution = candidate.effective_resolution;
    return request;
}

app::OfxRuntimePrepareSessionRequest build_prepare_request(
    const DeviceInfo& requested_device, const QualityArtifactSelection& selection,
    int requested_quality_mode, ExecutionEngine execution_engine, bool allow_cpu_fallback = false) {
    app::OfxRuntimePrepareSessionRequest request;
    request.client_instance_id = "quality_switch";
    request.model_path = selection.executable_model_path;
    request.artifact_name = selection.executable_model_path.filename().string();
    request.requested_device = requested_device;
    request.engine_options =
        ofx_engine_options(requested_device, execution_engine, allow_cpu_fallback);
    request.requested_quality_mode = requested_quality_mode;
    request.requested_resolution = selection.requested_resolution;
    request.effective_resolution = selection.effective_resolution;
    return request;
}

}  // namespace

bool allow_unrestricted_quality_attempt_for_request(const InstanceData& data, int quality_mode,
                                                    const DeviceInfo& requested_device) {
    return allow_unrestricted_quality_attempt_for_request_impl(data, quality_mode,
                                                               requested_device);
}

std::string requested_quality_runtime_label(int quality_mode, int requested_resolution,
                                            bool cpu_quality_guardrail_active) {
    return requested_quality_runtime_label_impl(quality_mode, requested_resolution,
                                                cpu_quality_guardrail_active);
}

std::string runtime_status_runtime_label(const InstanceData& data) {
    return runtime_status_runtime_label_impl(data);
}

std::string runtime_session_runtime_label(const InstanceData& data) {
    return runtime_session_runtime_label_impl(data);
}

std::string runtime_safe_quality_ceiling_runtime_label(const InstanceData& data) {
    return runtime_safe_quality_ceiling_runtime_label_impl(data);
}

std::string runtime_guide_source_runtime_label(const InstanceData& data) {
    return runtime_guide_source_runtime_label_impl(data);
}

std::string runtime_path_runtime_label(const InstanceData& data) {
    return runtime_path_runtime_label_impl(data);
}

bool sync_runtime_panel_session_state(InstanceData* data) {
    return sync_runtime_panel_session_state_impl(data);
}

std::string runtime_timings_runtime_label(const InstanceData& data) {
    return runtime_timings_runtime_label_impl(data);
}

std::string runtime_backend_work_runtime_label(const InstanceData& data) {
    return runtime_backend_work_runtime_label_impl(data);
}

InstanceData* get_instance_data(OfxImageEffectHandle instance) {
    if (g_suites.property == nullptr || g_suites.image_effect == nullptr) {
        return nullptr;
    }
    OfxPropertySetHandle props = nullptr;
    if (g_suites.image_effect->getPropertySet(instance, &props) != kOfxStatOK) {
        return nullptr;
    }
    void* ptr = nullptr;
    if (g_suites.property->propGetPointer(props, kOfxPropInstanceData, 0, &ptr) != kOfxStatOK) {
        return nullptr;
    }
    return reinterpret_cast<InstanceData*>(ptr);
}

void set_instance_data(OfxImageEffectHandle instance, InstanceData* data) {
    if (g_suites.property == nullptr || g_suites.image_effect == nullptr) {
        return;
    }
    OfxPropertySetHandle props = nullptr;
    if (g_suites.image_effect->getPropertySet(instance, &props) != kOfxStatOK) {
        return;
    }
    g_suites.property->propSetPointer(props, kOfxPropInstanceData, 0, data);
}

void set_param_enabled(OfxParamHandle param, bool enabled);
void sync_dependent_params(InstanceData* data);

OfxStatus create_instance(OfxImageEffectHandle instance) {
    const auto create_start = std::chrono::steady_clock::now();
    const auto log_create_total = [&](std::string_view outcome, std::string_view detail = {}) {
        std::string message = "event=instance_create_total total_ms=" +
                              std::to_string(elapsed_ms_since(create_start)) +
                              " outcome=" + std::string(outcome);
        if (!detail.empty()) {
            message += " detail=" + std::string(detail);
        }
        log_message("create_instance", message);
    };

    if (g_suites.image_effect == nullptr || g_suites.parameter == nullptr) {
        log_message("create_instance", "Missing required suites.");
        log_create_total("missing_suites");
        return kOfxStatErrMissingHostFeature;
    }

    auto data = std::unique_ptr<InstanceData>(new (std::nothrow) InstanceData());
    if (!data) {
        log_message("create_instance", "Failed to allocate InstanceData.");
        log_create_total("oom");
        return kOfxStatErrMemory;
    }
    data->effect = instance;
    data->cpu_quality_guardrail_active = false;
    data->render_count = 0;

    if (g_suites.image_effect->clipGetHandle(instance, "Source", &data->source_clip, nullptr) !=
        kOfxStatOK) {
        log_message("create_instance", "Failed to get Source clip handle.");
        log_create_total("source_clip_failed");
        return kOfxStatFailed;
    }

    g_suites.image_effect->clipGetHandle(instance, kClipAlphaHint, &data->alpha_hint_clip, nullptr);

    if (g_suites.image_effect->clipGetHandle(instance, "Output", &data->output_clip, nullptr) !=
        kOfxStatOK) {
        log_message("create_instance", "Failed to get Output clip handle.");
        log_create_total("output_clip_failed");
        return kOfxStatFailed;
    }

    OfxParamSetHandle param_set;
    if (g_suites.image_effect->getParamSet(instance, &param_set) != kOfxStatOK) {
        log_message("create_instance", "Failed to get param set.");
        log_create_total("param_set_failed");
        return kOfxStatFailed;
    }

    g_suites.parameter->paramGetHandle(param_set, kParamQualityMode, &data->quality_mode_param,
                                       nullptr);
    g_suites.parameter->paramGetHandle(param_set, kParamQualityFallbackMode,
                                       &data->quality_fallback_mode_param, nullptr);
    g_suites.parameter->paramGetHandle(param_set, kParamOutputMode, &data->output_mode_param,
                                       nullptr);
    g_suites.parameter->paramGetHandle(param_set, kParamRefinementMode,
                                       &data->refinement_mode_param, nullptr);
    g_suites.parameter->paramGetHandle(param_set, kParamCoarseResolutionOverride,
                                       &data->coarse_resolution_override_param, nullptr);
    g_suites.parameter->paramGetHandle(param_set, kParamInputColorSpace,
                                       &data->input_color_space_param, nullptr);
    g_suites.parameter->paramGetHandle(param_set, kParamQuantizationMode,
                                       &data->quantization_mode_param, nullptr);
    g_suites.parameter->paramGetHandle(param_set, kParamExecutionEngine,
                                       &data->execution_engine_param, nullptr);
    g_suites.parameter->paramGetHandle(param_set, kParamScreenColor, &data->screen_color_param,
                                       nullptr);
    g_suites.parameter->paramGetHandle(param_set, kParamTemporalSmoothing,
                                       &data->temporal_smoothing_param, nullptr);
    g_suites.parameter->paramGetHandle(param_set, kParamDespillStrength, &data->despill_param,
                                       nullptr);
    g_suites.parameter->paramGetHandle(param_set, kParamSpillMethod, &data->spill_method_param,
                                       nullptr);
    g_suites.parameter->paramGetHandle(param_set, kParamAutoDespeckle, &data->despeckle_param,
                                       nullptr);
    g_suites.parameter->paramGetHandle(param_set, kParamDespeckleSize, &data->despeckle_size_param,
                                       nullptr);
    g_suites.parameter->paramGetHandle(param_set, kParamAlphaBlackPoint,
                                       &data->alpha_black_point_param, nullptr);
    g_suites.parameter->paramGetHandle(param_set, kParamAlphaWhitePoint,
                                       &data->alpha_white_point_param, nullptr);
    g_suites.parameter->paramGetHandle(param_set, kParamAlphaErode, &data->alpha_erode_param,
                                       nullptr);
    g_suites.parameter->paramGetHandle(param_set, kParamAlphaSoftness, &data->alpha_softness_param,
                                       nullptr);
    g_suites.parameter->paramGetHandle(param_set, kParamAlphaGamma, &data->alpha_gamma_param,
                                       nullptr);
    g_suites.parameter->paramGetHandle(param_set, kParamUpscaleMethod, &data->upscale_method_param,
                                       nullptr);
    g_suites.parameter->paramGetHandle(param_set, kParamEnableTiling, &data->enable_tiling_param,
                                       nullptr);
    g_suites.parameter->paramGetHandle(param_set, kParamTileOverlap, &data->tile_overlap_param,
                                       nullptr);
    g_suites.parameter->paramGetHandle(param_set, kParamSourcePassthrough,
                                       &data->source_passthrough_param, nullptr);
    g_suites.parameter->paramGetHandle(param_set, kParamEdgeErode, &data->edge_erode_param,
                                       nullptr);
    g_suites.parameter->paramGetHandle(param_set, kParamEdgeBlur, &data->edge_blur_param, nullptr);
    g_suites.parameter->paramGetHandle(param_set, kParamRuntimeProcessing,
                                       &data->runtime_processing_param, nullptr);
    g_suites.parameter->paramGetHandle(param_set, kParamRuntimeDevice, &data->runtime_device_param,
                                       nullptr);
    g_suites.parameter->paramGetHandle(param_set, kParamRuntimeEngine, &data->runtime_engine_param,
                                       nullptr);
    g_suites.parameter->paramGetHandle(param_set, kParamRuntimeRequestedQuality,
                                       &data->runtime_requested_quality_param, nullptr);
    g_suites.parameter->paramGetHandle(param_set, kParamRuntimeEffectiveQuality,
                                       &data->runtime_effective_quality_param, nullptr);
    g_suites.parameter->paramGetHandle(param_set, kParamRuntimeSafeQualityCeiling,
                                       &data->runtime_safe_quality_ceiling_param, nullptr);
    g_suites.parameter->paramGetHandle(param_set, kParamRuntimeArtifact,
                                       &data->runtime_artifact_param, nullptr);
    g_suites.parameter->paramGetHandle(param_set, kParamRuntimeGuideSource,
                                       &data->runtime_guide_source_param, nullptr);
    g_suites.parameter->paramGetHandle(param_set, kParamRuntimePath, &data->runtime_path_param,
                                       nullptr);
    g_suites.parameter->paramGetHandle(param_set, kParamRuntimeSession,
                                       &data->runtime_session_param, nullptr);
    g_suites.parameter->paramGetHandle(param_set, kParamRuntimeStatus, &data->runtime_status_param,
                                       nullptr);
    g_suites.parameter->paramGetHandle(param_set, kParamRuntimeTimings,
                                       &data->runtime_timings_param, nullptr);
    g_suites.parameter->paramGetHandle(param_set, kParamRuntimeBackendWork,
                                       &data->runtime_backend_work_param, nullptr);
    g_suites.parameter->paramGetHandle(param_set, kParamRenderTimeout, &data->render_timeout_param,
                                       nullptr);
    g_suites.parameter->paramGetHandle(param_set, kParamPrepareTimeout,
                                       &data->prepare_timeout_param, nullptr);
    g_suites.parameter->paramGetHandle(param_set, kParamAllowCpuFallback,
                                       &data->allow_cpu_fallback_param, nullptr);

    sync_dependent_params(data.get());

    set_runtime_panel_status(data.get(), "Initializing...", "Detecting...", "Loading...",
                             "Loading...", "Loading...", "Loading...", "Loading...", "Loading...",
                             "Loading...", "Loading...");

    int render_timeout_s = common::kDefaultOfxRenderTimeoutSeconds;
    int prepare_timeout_s = common::kDefaultOfxPrepareTimeoutSeconds;
    if (data->render_timeout_param != nullptr) {
        g_suites.parameter->paramGetValue(data->render_timeout_param, &render_timeout_s);
    }
    if (data->prepare_timeout_param != nullptr) {
        g_suites.parameter->paramGetValue(data->prepare_timeout_param, &prepare_timeout_s);
    }

    data->runtime_server_path =
        resolve_ofx_runtime_server_binary(plugin_module_path().value_or(""));
    data->use_runtime_server = use_runtime_server_default(data->runtime_server_path);
    if (data->use_runtime_server) {
        OfxRuntimeClientOptions client_options;
        client_options.endpoint = common::default_ofx_runtime_endpoint();
        client_options.server_binary = data->runtime_server_path;
        client_options.request_timeout_ms = render_timeout_s * 1000;
        client_options.prepare_timeout_ms = prepare_timeout_s * 1000;
        auto runtime_client = OfxRuntimeClient::create(std::move(client_options));
        if (!runtime_client) {
            data->last_error = runtime_client.error().message;
            log_message("create_instance",
                        "Runtime client init failed: " + runtime_client.error().message);
            if (!allow_inprocess_fallback()) {
                post_message(kOfxMessageError, data->last_error.c_str(), instance);
                log_create_total("runtime_client_failed", data->last_error);
                return kOfxStatFailed;
            }
            data->use_runtime_server = false;
        } else {
            data->runtime_client = std::move(*runtime_client);
            log_message("create_instance", "Using out-of-process OFX runtime.");
        }
    } else {
        log_message("create_instance", "Using in-process OFX runtime.");
    }

    data->models_root = resolve_models_root();
    DeviceInfo detected_device = auto_detect();
    log_message("create_instance", std::string("Detected device: ") + detected_device.name);
    log_message("create_instance",
                std::string("Detected backend: ") + backend_label(detected_device.backend));
    auto capabilities = app::runtime_capabilities_for_models_root(data->models_root);
    data->runtime_capabilities = capabilities;
    data->requested_execution_engine = selected_execution_engine(data.get());
    log_message("create_instance", std::string("Platform: ") + capabilities.platform);

    int initial_quality_mode = kQualityAuto;
    if (data->quality_mode_param != nullptr) {
        g_suites.parameter->paramGetValue(data->quality_mode_param, &initial_quality_mode);
    }

    DeviceInfo preferred_device = detected_device;
#if defined(__APPLE__)
    if (capabilities.apple_silicon && capabilities.mlx_probe_available &&
        has_mlx_bootstrap_artifacts(data->models_root)) {
        preferred_device =
            DeviceInfo{"Apple Silicon MLX", detected_device.available_memory_mb, Backend::MLX};
    }
#endif
    data->preferred_device = preferred_device;
    data->device = detected_device;
    data->active_quality_mode = initial_quality_mode;
    data->requested_resolution =
        initial_requested_resolution_for_quality_mode(initial_quality_mode);
    data->active_resolution = 0;
    data->runtime_panel_state.requested_quality_mode = initial_quality_mode;
    data->runtime_panel_state.requested_resolution = data->requested_resolution;
    data->runtime_panel_state.effective_resolution = 0;
    data->runtime_panel_state.cpu_quality_guardrail_active = false;
    data->runtime_panel_state.requested_engine = data->requested_execution_engine;
    data->runtime_panel_state.effective_engine = ExecutionEngine::Auto;
    data->runtime_panel_state.artifact_path.clear();
    data->model_path.clear();
    data->last_error.clear();
    sync_dependent_params(data.get());

    if (!should_prepare_bootstrap_during_instance_create(data->use_runtime_server)) {
        log_message("create_instance", "Deferring runtime session bootstrap until first render.");
        update_runtime_panel_values(data.get());
        set_instance_data(instance, data.release());
        log_create_total("success", "bootstrap=deferred");
        return kOfxStatOK;
    }

    auto bootstrap_candidates = build_bootstrap_candidates(
        capabilities, detected_device, data->models_root, data->requested_execution_engine);
    if (bootstrap_candidates.empty()) {
        log_message("create_instance", "No compatible model artifacts found for OFX bootstrap.");
        post_message(kOfxMessageError, "No compatible model artifacts found for this device.",
                     instance);
        log_create_total("no_artifacts");
        return kOfxStatFailed;
    }

    std::string failure_summary;
    const bool bootstrap_allow_cpu_fallback = allow_cpu_fallback_requested(data.get());
    const ExecutionEngine selected_engine = data->requested_execution_engine;
    for (const auto& candidate : bootstrap_candidates) {
        constexpr std::string_view kBootstrapPhase = "bootstrap";
        log_message("create_instance",
                    std::string("Attempting backend: ") + backend_label(candidate.device.backend));
        log_message("create_instance", std::string("Requested model: ") +
                                           candidate.requested_model_path.filename().string());
        if (candidate.executable_model_path != candidate.requested_model_path) {
            log_message("create_instance", std::string("Requested executable artifact: ") +
                                               candidate.executable_model_path.filename().string());
        }

        log_engine_event("create_instance", "engine_create_begin", kBootstrapPhase,
                         candidate.device, candidate.device, candidate.executable_model_path,
                         candidate.requested_resolution, candidate.effective_resolution);

        DeviceInfo effective_device = candidate.device;
        std::optional<BackendFallbackInfo> fallback = std::nullopt;
        if (data->use_runtime_server && data->runtime_client != nullptr) {
            auto prepare_result = data->runtime_client->prepare_session(
                build_prepare_request(candidate, selected_engine, bootstrap_allow_cpu_fallback),
                [&](const StageTiming& timing) {
                    log_stage_timing("create_instance", kBootstrapPhase, candidate.device,
                                     candidate.executable_model_path,
                                     candidate.requested_resolution, candidate.effective_resolution,
                                     timing);
                });
            if (!prepare_result) {
                std::string failure =
                    backend_label(candidate.device.backend) + ": " + prepare_result.error().message;
                log_engine_event("create_instance", "engine_create_error", kBootstrapPhase,
                                 candidate.device, candidate.device,
                                 candidate.executable_model_path, candidate.requested_resolution,
                                 candidate.effective_resolution, std::nullopt,
                                 prepare_result.error().message);
                log_message("create_instance", std::string("Runtime prepare failed: ") + failure);
                if (!failure_summary.empty()) {
                    failure_summary += " | ";
                }
                failure_summary += failure;
                continue;
            }
            effective_device = prepare_result->session.effective_device;
            fallback = prepare_result->session.backend_fallback;
            log_message("create_instance",
                        "Runtime session prepared. reused_existing_session=" +
                            std::to_string(prepare_result->session.reused_existing_session) +
                            " ref_count=" + std::to_string(prepare_result->session.ref_count));
        } else {
            auto engine_result = Engine::create(
                candidate.executable_model_path, candidate.device,
                [&](const StageTiming& timing) {
                    log_stage_timing("create_instance", kBootstrapPhase, candidate.device,
                                     candidate.executable_model_path,
                                     candidate.requested_resolution, candidate.effective_resolution,
                                     timing);
                },
                ofx_engine_options(candidate.device, selected_engine,
                                   bootstrap_allow_cpu_fallback));
            if (!engine_result) {
                std::string failure =
                    backend_label(candidate.device.backend) + ": " + engine_result.error().message;
                log_engine_event("create_instance", "engine_create_error", kBootstrapPhase,
                                 candidate.device, candidate.device,
                                 candidate.executable_model_path, candidate.requested_resolution,
                                 candidate.effective_resolution, std::nullopt,
                                 engine_result.error().message);
                log_message("create_instance", std::string("Engine create failed: ") + failure);
                if (!failure_summary.empty()) {
                    failure_summary += " | ";
                }
                failure_summary += failure;
                continue;
            }

            auto engine = std::move(*engine_result);
            effective_device = engine->current_device();
            fallback = engine->backend_fallback();
            if (!backend_matches_request(*engine, candidate.device)) {
                std::string failure = backend_mismatch_message(
                    "Bootstrap", candidate.device, *engine, candidate.executable_model_path,
                    engine->backend_fallback());
                log_message("create_instance", failure);
                if (!failure_summary.empty()) {
                    failure_summary += " | ";
                }
                failure_summary += failure;
                continue;
            }
            data->engine = std::move(engine);
        }

        log_engine_event("create_instance", "engine_create_result", kBootstrapPhase,
                         candidate.device, effective_device, candidate.executable_model_path,
                         candidate.requested_resolution, candidate.effective_resolution, fallback);
        if (!backend_matches_request(effective_device, candidate.device)) {
            std::string failure =
                "Bootstrap requested backend " + backend_label(candidate.device.backend) + " for " +
                candidate.executable_model_path.filename().string() + " but the runtime is using " +
                backend_label(effective_device.backend) + ".";
            if (fallback.has_value() && !fallback->reason.empty()) {
                failure += " Reason: " + fallback->reason;
            }
            log_message("create_instance", failure);
            if (!failure_summary.empty()) {
                failure_summary += " | ";
            }
            failure_summary += failure;
            continue;
        }

        data->preferred_device = candidate.device;
        data->device = effective_device;
        data->model_path = candidate.executable_model_path;
        data->requested_execution_engine = selected_engine;
        data->active_execution_engine = effective_execution_engine(*data);
        data->cached_result_valid = false;
        data->cached_signature = 0;
        data->cached_signature_valid = false;
        data->active_quality_mode = kQualityAuto;
        data->requested_resolution = candidate.requested_resolution;
        data->active_resolution = candidate.effective_resolution;
        data->cpu_quality_guardrail_active = false;
        data->render_count = 0;
        data->last_error.clear();
        data->last_frame_ms = 0.0;
        data->avg_frame_ms = 0.0;
        data->frame_time_samples = 0;
        data->last_render_work_origin = LastRenderWorkOrigin::None;
        data->last_render_stage_timings.clear();
        sync_runtime_panel_state_from_active_engine(data.get());

        if (candidate.device.backend != detected_device.backend) {
            log_message("create_instance", std::string("Backend fallback: ") +
                                               backend_label(detected_device.backend) + " -> " +
                                               backend_label(candidate.device.backend));
        }
        log_message("create_instance", std::string("Selected model: ") +
                                           candidate.requested_model_path.filename().string());
        if (candidate.executable_model_path != candidate.requested_model_path) {
            log_message("create_instance", std::string("Effective bridge: ") +
                                               candidate.executable_model_path.filename().string());
        }
        log_message("create_instance",
                    std::string("Effective backend: ") + backend_label(data->device.backend));
        log_message("create_instance",
                    "Effective resolution: " + std::to_string(data->active_resolution));
        log_message("create_instance", "Engine created successfully.");
        update_runtime_panel_values(data.get());

        set_instance_data(instance, data.release());
        log_create_total("success");
        return kOfxStatOK;
    }

    data->last_error = failure_summary;
    std::string failure_message = "Failed to load AI engine: " + failure_summary;
    post_message(kOfxMessageError, failure_message.c_str(), instance);
    log_create_total("engine_create_failed", failure_summary);
    return kOfxStatFailed;
}

bool ensure_engine_for_quality(InstanceData* data, int quality_mode, int input_width,
                               int input_height, int quantization_mode,
                               QualityFallbackMode fallback_mode, int coarse_resolution_override,
                               RefinementMode refinement_mode) {
    const auto quality_switch_start = std::chrono::steady_clock::now();
    const auto log_quality_total = [&](std::string_view outcome, std::string_view detail = {}) {
        std::string message = "event=quality_switch_total total_ms=" +
                              std::to_string(elapsed_ms_since(quality_switch_start)) +
                              " quality=" + quality_mode_label(quality_mode) +
                              " outcome=" + std::string(outcome);
        if (!detail.empty()) {
            message += " detail=" + std::string(detail);
        }
        log_message("ensure_engine_for_quality", message);
    };

    if (data == nullptr || (!data->use_runtime_server && data->engine == nullptr) ||
        (data->use_runtime_server && data->runtime_client == nullptr)) {
        log_quality_total("no_engine");
        return true;
    }

    const ExecutionEngine requested_execution_engine = selected_execution_engine(data);
    data->requested_execution_engine = requested_execution_engine;

    DeviceInfo original_requested_device = data->preferred_device;
    if (original_requested_device.backend == Backend::Auto) {
        original_requested_device = data->device;
    }

    const bool allow_cpu_fallback = allow_cpu_fallback_requested(data);
    const bool reroute_int8_to_cpu = should_reroute_int8_to_cpu(
        original_requested_device, quantization_mode, allow_cpu_fallback);
    DeviceInfo requested_device = reroute_int8_to_cpu
                                      ? cpu_fallback_device_for_request(original_requested_device)
                                      : original_requested_device;
    const int requested_quality_mode = quality_mode;
    const int effective_quality_mode =
        clamp_quality_mode_for_cpu_backend(requested_device.backend, requested_quality_mode);
    const bool cpu_quality_guardrail_active = effective_quality_mode != requested_quality_mode;
    const int requested_resolution =
        resolve_target_resolution(requested_quality_mode, input_width, input_height);
    const bool allow_unrestricted_quality_attempt = allow_unrestricted_quality_attempt_for_request(
        *data, requested_quality_mode, original_requested_device);
    const std::string cpu_fallback_warning = cpu_fallback_warning_message(reroute_int8_to_cpu);
    const std::string cpu_quality_guardrail_warning =
        cpu_quality_guardrail_warning_message(requested_quality_mode, cpu_quality_guardrail_active);
    const std::string manual_override_warning =
        manual_override_warning_message(original_requested_device, requested_quality_mode,
                                        requested_resolution, allow_unrestricted_quality_attempt);
    data->runtime_panel_state.requested_quality_mode = requested_quality_mode;
    data->runtime_panel_state.requested_resolution = requested_resolution;
    data->runtime_panel_state.cpu_quality_guardrail_active = cpu_quality_guardrail_active;
    const auto compile_cache_context =
        build_quality_compile_failure_cache_context(*data, requested_device, quantization_mode);
    prepare_quality_compile_failure_cache(data->quality_compile_failure_cache,
                                          compile_cache_context);
    if (auto unsupported_quantization = unsupported_quantization_message(
            original_requested_device.backend, quantization_mode, allow_cpu_fallback);
        unsupported_quantization.has_value()) {
        data->last_error = *unsupported_quantization;
        log_message("ensure_engine_for_quality", data->last_error);
        update_runtime_panel(data);
        log_quality_total("unsupported_quantization", data->last_error);
        return false;
    }
    auto unsupported_quality =
        fallback_mode == QualityFallbackMode::Direct
            ? unsupported_quality_message(requested_device, requested_quality_mode,
                                          requested_resolution, allow_unrestricted_quality_attempt)
            : std::nullopt;
    if (unsupported_quality.has_value()) {
        data->last_warning.clear();
        data->last_error = *unsupported_quality;
        set_runtime_panel_state_for_failed_quality_request(
            data, requested_quality_mode, requested_resolution, cpu_quality_guardrail_active,
            artifact_path_for_backend(data->models_root, requested_device.backend,
                                      requested_resolution));
        log_message(
            "ensure_engine_for_quality",
            "event=quality_guardrail requested_backend=" + backend_label(requested_device.backend) +
                " requested_device=" + requested_device.name +
                " available_memory_mb=" + std::to_string(requested_device.available_memory_mb) +
                " requested_resolution=" + std::to_string(requested_resolution) +
                " detail=" + *unsupported_quality);
        log_message("ensure_engine_for_quality", data->last_error);
        update_runtime_panel(data);
        log_quality_total("unsupported_quality", data->last_error);
        return false;
    }
    auto selections = quality_artifact_candidates(
        data->models_root, requested_device.backend, effective_quality_mode, input_width,
        input_height, quantization_mode, requested_device.available_memory_mb, fallback_mode,
        coarse_resolution_override, allow_unrestricted_quality_attempt, requested_execution_engine);
    const auto original_selections = selections;
    data->cpu_quality_guardrail_active = cpu_quality_guardrail_active;
    if (!cpu_fallback_warning.empty()) {
        log_message("ensure_engine_for_quality", cpu_fallback_warning);
    }
    if (!cpu_quality_guardrail_warning.empty()) {
        log_message("ensure_engine_for_quality", cpu_quality_guardrail_warning);
    }
    if (!manual_override_warning.empty()) {
        log_message("ensure_engine_for_quality", manual_override_warning);
    }
    if (selections.empty()) {
        const auto expected_artifacts = expected_quality_artifact_paths(
            data->models_root, requested_device.backend, effective_quality_mode, input_width,
            input_height, quantization_mode, requested_device.available_memory_mb, fallback_mode,
            coarse_resolution_override, allow_unrestricted_quality_attempt);
        data->last_error = missing_quality_artifact_message(
            data->models_root, requested_device.backend, effective_quality_mode, input_width,
            input_height, quantization_mode, cpu_quality_guardrail_active,
            requested_device.available_memory_mb, fallback_mode, coarse_resolution_override,
            allow_unrestricted_quality_attempt, requested_execution_engine);
        if (auto expected_artifact = primary_expected_artifact_path(expected_artifacts);
            expected_artifact.has_value()) {
            set_runtime_panel_state_for_failed_quality_request(
                data, requested_quality_mode, requested_resolution, cpu_quality_guardrail_active,
                *expected_artifact);
        }
        log_message("ensure_engine_for_quality", data->last_error);
        update_runtime_panel(data);
        log_quality_total("missing_artifact", data->last_error);
        return false;
    }

    if (refinement_mode != RefinementMode::Auto) {
        std::vector<QualityArtifactSelection> supported_by_refinement_mode;
        supported_by_refinement_mode.reserve(selections.size());
        std::optional<Error> refinement_error = std::nullopt;
        for (const auto& selection : selections) {
            auto validation = app::validate_refinement_mode_for_artifact(
                selection.executable_model_path, refinement_mode);
            if (validation) {
                supported_by_refinement_mode.push_back(selection);
            } else if (!refinement_error.has_value()) {
                refinement_error = validation.error();
            }
        }

        if (supported_by_refinement_mode.empty()) {
            data->last_error = refinement_error.has_value()
                                   ? refinement_error->message
                                   : "No packaged quality artifact supports the requested "
                                     "refinement strategy override.";
            if (!selections.empty()) {
                set_runtime_panel_state_for_failed_quality_request(
                    data, requested_quality_mode, requested_resolution,
                    cpu_quality_guardrail_active, selections.front().executable_model_path);
            }
            log_message("ensure_engine_for_quality", data->last_error);
            update_runtime_panel(data);
            log_quality_total("unsupported_refinement_mode", data->last_error);
            return false;
        }

        selections = std::move(supported_by_refinement_mode);
    }

    if (should_abort_quality_fallback_after_compile_failure(
            requested_device.backend, requested_quality_mode, cpu_quality_guardrail_active,
            selections.front())) {
        if (auto cached_failure = cached_quality_compile_failure(
                data->quality_compile_failure_cache, compile_cache_context, selections.front());
            cached_failure.has_value()) {
            data->last_warning.clear();
            data->last_error = cached_failure->error_message;
            set_runtime_panel_state_for_failed_quality_request(
                data, requested_quality_mode, requested_resolution, cpu_quality_guardrail_active,
                cached_failure->selection.executable_model_path);
            update_runtime_panel(data);
            log_quality_total("cached_compile_failure", data->last_error);
            return false;
        }
    }

    selections = filter_quality_artifacts_with_compile_cache(
        selections, data->quality_compile_failure_cache, compile_cache_context);
    if (selections.empty()) {
        data->last_error =
            "TensorRT RTX already rejected all packaged quality candidates for this device and "
            "model bundle in the current plugin session.";
        if (!original_selections.empty()) {
            set_runtime_panel_state_for_failed_quality_request(
                data, requested_quality_mode, requested_resolution, cpu_quality_guardrail_active,
                original_selections.front().executable_model_path);
        }
        update_runtime_panel(data);
        log_quality_total("compile_cache_exhausted", data->last_error);
        return false;
    }

    if (!data->use_runtime_server && data->engine != nullptr) {
        data->device = data->engine->current_device();
    } else if (data->use_runtime_server && data->runtime_client != nullptr &&
               data->runtime_client->has_session()) {
        data->device = data->runtime_client->current_device();
    }
    bool current_backend_matches = backend_matches_request(data->device, requested_device);

    for (const auto& selection : selections) {
        const bool session_alive = data->use_runtime_server ? (data->runtime_client != nullptr &&
                                                               data->runtime_client->has_session())
                                                            : (data->engine != nullptr);
        const bool current_engine_matches = execution_engine_matches_request(
            requested_execution_engine, effective_execution_engine(*data));
        if (current_backend_matches && session_alive && current_engine_matches &&
            selection.executable_model_path == data->model_path &&
            selection.effective_resolution == data->active_resolution) {
            std::string runtime_warning = cpu_fallback_warning;
            append_status_note(runtime_warning, cpu_quality_guardrail_warning);
            append_status_note(runtime_warning, manual_override_warning);
            append_status_note(runtime_warning,
                               quality_fallback_warning(requested_quality_mode, selection));
            data->active_quality_mode = requested_quality_mode;
            data->requested_resolution = requested_resolution;
            data->active_resolution = selection.effective_resolution;
            data->requested_execution_engine = requested_execution_engine;
            data->active_execution_engine = effective_execution_engine(*data);
            data->cpu_quality_guardrail_active = cpu_quality_guardrail_active;
            data->last_warning = runtime_warning;
            data->last_error.clear();
            sync_runtime_panel_state_from_active_engine(data);
            update_runtime_panel_values(data);
            log_quality_total("reused_engine");
            return true;
        }

        constexpr std::string_view kQualitySwitchPhase = "quality_switch";
        log_engine_event("ensure_engine_for_quality", "engine_create_begin", kQualitySwitchPhase,
                         requested_device, requested_device, selection.executable_model_path,
                         requested_resolution, selection.effective_resolution);

        DeviceInfo effective_device = requested_device;
        std::optional<BackendFallbackInfo> fallback = std::nullopt;
        if (data->use_runtime_server && data->runtime_client != nullptr) {
            set_string_param_value(
                data->runtime_status_param,
                "Preparing " + std::string(quality_mode_label(requested_quality_mode)) + "...");
            set_string_param_value(data->runtime_timings_param, "Loading...");
            auto prepare_result = data->runtime_client->prepare_session(
                build_prepare_request(requested_device, selection, requested_quality_mode,
                                      requested_execution_engine, allow_cpu_fallback),
                [&](const StageTiming& timing) {
                    log_stage_timing("ensure_engine_for_quality", kQualitySwitchPhase,
                                     requested_device, selection.executable_model_path,
                                     requested_resolution, selection.effective_resolution, timing);
                });
            if (!prepare_result) {
                data->last_error = "Failed to prepare runtime session for " +
                                   std::string(quality_mode_label(requested_quality_mode)) +
                                   " using " + selection.executable_model_path.filename().string() +
                                   ": " + prepare_result.error().message;
                log_engine_event("ensure_engine_for_quality", "engine_create_error",
                                 kQualitySwitchPhase, requested_device, requested_device,
                                 selection.executable_model_path, requested_resolution,
                                 selection.effective_resolution, std::nullopt,
                                 prepare_result.error().message);
                log_message("ensure_engine_for_quality", data->last_error);
                if (should_record_quality_compile_failure(requested_device.backend,
                                                          prepare_result.error())) {
                    record_quality_compile_failure(data->quality_compile_failure_cache,
                                                   compile_cache_context, selection,
                                                   data->last_error);
                }
                if (should_abort_quality_fallback_after_compile_failure(
                        requested_device.backend, requested_quality_mode,
                        cpu_quality_guardrail_active, selection)) {
                    data->last_warning.clear();
                    set_runtime_panel_state_for_failed_quality_request(
                        data, requested_quality_mode, requested_resolution,
                        cpu_quality_guardrail_active, selection.executable_model_path);
                    update_runtime_panel(data);
                    log_quality_total("compile_failure", data->last_error);
                    return false;
                }
                continue;
            }
            effective_device = prepare_result->session.effective_device;
            fallback = prepare_result->session.backend_fallback;
            log_message("ensure_engine_for_quality",
                        "Runtime session prepared. reused_existing_session=" +
                            std::to_string(prepare_result->session.reused_existing_session) +
                            " ref_count=" + std::to_string(prepare_result->session.ref_count));
        } else {
            set_string_param_value(
                data->runtime_status_param,
                "Preparing " + std::string(quality_mode_label(requested_quality_mode)) + "...");
            set_string_param_value(data->runtime_timings_param, "Loading...");
            auto engine_result = Engine::create(
                selection.executable_model_path, requested_device,
                [&](const StageTiming& timing) {
                    log_stage_timing("ensure_engine_for_quality", kQualitySwitchPhase,
                                     requested_device, selection.executable_model_path,
                                     requested_resolution, selection.effective_resolution, timing);
                },
                ofx_engine_options(requested_device, requested_execution_engine,
                                   allow_cpu_fallback));
            if (!engine_result) {
                data->last_error = "Failed to create engine for " +
                                   std::string(quality_mode_label(requested_quality_mode)) +
                                   " using " + selection.executable_model_path.filename().string() +
                                   ": " + engine_result.error().message;
                log_engine_event("ensure_engine_for_quality", "engine_create_error",
                                 kQualitySwitchPhase, requested_device, requested_device,
                                 selection.executable_model_path, requested_resolution,
                                 selection.effective_resolution, std::nullopt,
                                 engine_result.error().message);
                log_message("ensure_engine_for_quality", data->last_error);
                if (should_record_quality_compile_failure(requested_device.backend,
                                                          engine_result.error())) {
                    record_quality_compile_failure(data->quality_compile_failure_cache,
                                                   compile_cache_context, selection,
                                                   data->last_error);
                }
                if (should_abort_quality_fallback_after_compile_failure(
                        requested_device.backend, requested_quality_mode,
                        cpu_quality_guardrail_active, selection)) {
                    data->last_warning.clear();
                    set_runtime_panel_state_for_failed_quality_request(
                        data, requested_quality_mode, requested_resolution,
                        cpu_quality_guardrail_active, selection.executable_model_path);
                    update_runtime_panel(data);
                    log_quality_total("compile_failure", data->last_error);
                    return false;
                }
                continue;
            }

            auto engine = std::move(*engine_result);
            effective_device = engine->current_device();
            fallback = engine->backend_fallback();
            if (!backend_matches_request(*engine, requested_device)) {
                data->last_error = backend_mismatch_message(
                    "Quality switch", requested_device, *engine, selection.executable_model_path,
                    engine->backend_fallback());
                log_message("ensure_engine_for_quality", data->last_error);
                if (should_record_quality_backend_mismatch(requested_device.backend)) {
                    record_quality_compile_failure(data->quality_compile_failure_cache,
                                                   compile_cache_context, selection,
                                                   data->last_error);
                }
                if (should_abort_quality_fallback_after_compile_failure(
                        requested_device.backend, requested_quality_mode,
                        cpu_quality_guardrail_active, selection)) {
                    data->last_warning.clear();
                    set_runtime_panel_state_for_failed_quality_request(
                        data, requested_quality_mode, requested_resolution,
                        cpu_quality_guardrail_active, selection.executable_model_path);
                    update_runtime_panel(data);
                    log_quality_total("backend_mismatch", data->last_error);
                    return false;
                }
                continue;
            }
            data->engine = std::move(engine);
        }

        log_engine_event("ensure_engine_for_quality", "engine_create_result", kQualitySwitchPhase,
                         requested_device, effective_device, selection.executable_model_path,
                         requested_resolution, selection.effective_resolution, fallback);
        if (!backend_matches_request(effective_device, requested_device)) {
            data->last_error =
                "Quality switch requested backend " + backend_label(requested_device.backend) +
                " for " + selection.executable_model_path.filename().string() +
                " but the runtime is using " + backend_label(effective_device.backend) + ".";
            if (fallback.has_value() && !fallback->reason.empty()) {
                data->last_error += " Reason: " + fallback->reason;
            }
            log_message("ensure_engine_for_quality", data->last_error);
            if (should_record_quality_backend_mismatch(requested_device.backend)) {
                record_quality_compile_failure(data->quality_compile_failure_cache,
                                               compile_cache_context, selection, data->last_error);
            }
            if (should_abort_quality_fallback_after_compile_failure(
                    requested_device.backend, requested_quality_mode, cpu_quality_guardrail_active,
                    selection)) {
                data->last_warning.clear();
                set_runtime_panel_state_for_failed_quality_request(
                    data, requested_quality_mode, requested_resolution,
                    cpu_quality_guardrail_active, selection.executable_model_path);
                update_runtime_panel(data);
                log_quality_total("backend_mismatch", data->last_error);
                return false;
            }
            continue;
        }

        data->device = effective_device;
        data->model_path = selection.executable_model_path;
        data->requested_execution_engine = requested_execution_engine;
        data->active_execution_engine = effective_execution_engine(*data);
        data->cached_result_valid = false;
        data->cached_signature = 0;
        data->cached_signature_valid = false;
        data->active_quality_mode = requested_quality_mode;
        data->requested_resolution = requested_resolution;
        data->active_resolution = selection.effective_resolution;
        data->cpu_quality_guardrail_active = cpu_quality_guardrail_active;
        data->render_count = 0;
        data->last_error.clear();
        data->last_frame_ms = 0.0;
        data->avg_frame_ms = 0.0;
        data->frame_time_samples = 0;
        data->last_render_work_origin = LastRenderWorkOrigin::None;
        data->last_render_stage_timings.clear();
        data->last_warning = cpu_fallback_warning;
        append_status_note(data->last_warning, cpu_quality_guardrail_warning);
        append_status_note(data->last_warning, manual_override_warning);
        append_status_note(data->last_warning,
                           quality_fallback_warning(requested_quality_mode, selection));
        if (!data->last_warning.empty()) {
            log_message("ensure_engine_for_quality", "fallback_note=" + data->last_warning);
        }
        sync_runtime_panel_state_from_active_engine(data);
        log_message("ensure_engine_for_quality",
                    std::string("Switched to artifact ") +
                        selection.executable_model_path.filename().string());
        log_message("ensure_engine_for_quality",
                    std::string("Effective backend: ") + backend_label(data->device.backend));
        log_message("ensure_engine_for_quality",
                    "Effective resolution: " + std::to_string(data->active_resolution));
        update_runtime_panel_values(data);
        log_quality_total("success");
        return true;
    }

    log_quality_total("exhausted_candidates", data->last_error);
    update_runtime_panel(data);
    return false;
}

void update_runtime_panel(InstanceData* data) {
    if (data == nullptr) {
        return;
    }
    data->runtime_panel_dirty = true;
    if (data->in_render) {
        return;
    }
    data->runtime_panel_dirty = false;
    update_runtime_panel_values(data);
}

void flush_runtime_panel(InstanceData* data) {
    if (data != nullptr && data->runtime_panel_dirty && !data->in_render) {
        data->runtime_panel_dirty = false;
        update_runtime_panel_values(data);
    }
}

OfxStatus begin_sequence_render(OfxImageEffectHandle instance, OfxPropertySetHandle /*in_args*/) {
    InstanceData* data = get_instance_data(instance);
    if (data == nullptr) {
        return kOfxStatReplyDefault;
    }

    clear_instance_render_caches(data, true);
    flush_runtime_panel(data);
    log_message("begin_sequence_render", "Sequence render state reset.");
    return kOfxStatOK;
}

OfxStatus end_sequence_render(OfxImageEffectHandle instance, OfxPropertySetHandle /*in_args*/) {
    InstanceData* data = get_instance_data(instance);
    if (data == nullptr) {
        return kOfxStatReplyDefault;
    }

    clear_instance_render_caches(data, false);
    flush_runtime_panel(data);
    log_message("end_sequence_render", "Sequence render caches cleared.");
    return kOfxStatOK;
}

OfxStatus purge_caches(OfxImageEffectHandle instance) {
    if (g_frame_cache != nullptr) {
        g_frame_cache->clear();
    }

    if (instance != nullptr) {
        if (InstanceData* data = get_instance_data(instance); data != nullptr) {
            clear_instance_render_caches(data, true);
            flush_runtime_panel(data);
        }
    }

    log_message("purge_caches", "Host requested cache purge.");
    return kOfxStatOK;
}

OfxStatus get_regions_of_interest(OfxImageEffectHandle /*instance*/, OfxPropertySetHandle in_args,
                                  OfxPropertySetHandle out_args) {
    if (in_args == nullptr || out_args == nullptr || g_suites.property == nullptr) {
        return kOfxStatReplyDefault;
    }

    double roi[4] = {};
    if (g_suites.property->propGetDoubleN(in_args, kOfxImageEffectPropRegionOfInterest, 4, roi) !=
        kOfxStatOK) {
        return kOfxStatReplyDefault;
    }

    const std::string source_roi_property =
        std::string("OfxImageClipPropRoI_") + kOfxImageEffectSimpleSourceClipName;
    const std::string hint_roi_property = std::string("OfxImageClipPropRoI_") + kClipAlphaHint;

    g_suites.property->propSetDoubleN(out_args, source_roi_property.c_str(), 4, roi);
    g_suites.property->propSetDoubleN(out_args, hint_roi_property.c_str(), 4, roi);
    return kOfxStatOK;
}

OfxStatus is_identity(OfxImageEffectHandle /*instance*/, OfxPropertySetHandle /*in_args*/,
                      OfxPropertySetHandle /*out_args*/) {
    return kOfxStatReplyDefault;
}

void set_param_enabled(OfxParamHandle param, bool enabled) {
    if (param == nullptr) {
        return;
    }
    OfxPropertySetHandle props = nullptr;
    if (g_suites.parameter->paramGetPropertySet(param, &props) == kOfxStatOK) {
        g_suites.property->propSetInt(props, kOfxParamPropEnabled, 0, enabled ? 1 : 0);
    }
}

void sync_dependent_params(InstanceData* data) {
    if (data == nullptr) {
        return;
    }

    int tiling_enabled = 0;
    if (data->enable_tiling_param) {
        g_suites.parameter->paramGetValue(data->enable_tiling_param, &tiling_enabled);
    }
    set_param_enabled(data->tile_overlap_param, tiling_enabled != 0);

    int despeckle_enabled = 0;
    if (data->despeckle_param) {
        g_suites.parameter->paramGetValue(data->despeckle_param, &despeckle_enabled);
    }
    set_param_enabled(data->despeckle_size_param, despeckle_enabled != 0);

    int source_passthrough = 0;
    if (data->source_passthrough_param) {
        g_suites.parameter->paramGetValue(data->source_passthrough_param, &source_passthrough);
    }
    set_param_enabled(data->edge_erode_param, source_passthrough != 0);
    set_param_enabled(data->edge_blur_param, source_passthrough != 0);

    set_param_enabled(data->execution_engine_param, execution_engine_selector_enabled(*data));
}

OfxStatus instance_changed(OfxImageEffectHandle instance, OfxPropertySetHandle in_args) {
    InstanceData* data = get_instance_data(instance);
    if (data == nullptr) {
        return kOfxStatReplyDefault;
    }
    if (in_args != nullptr && g_suites.property != nullptr) {
        std::string changed_param;
        if (get_string(in_args, kOfxPropName, changed_param)) {
            if (changed_param == kParamOpenStartHereGuide ||
                changed_param == kParamOpenQualityGuide ||
                changed_param == kParamOpenAlphaHintGuide ||
                changed_param == kParamOpenRecoverDetailsGuide ||
                changed_param == kParamOpenTilingGuide ||
                changed_param == kParamOpenResolveTutorial ||
                changed_param == kParamOpenTroubleshooting) {
                std::string url;
                if (changed_param == kParamOpenStartHereGuide) {
                    url = help_doc_url("OFX_PANEL_GUIDE.md#start-here");
                } else if (changed_param == kParamOpenQualityGuide) {
                    url = help_doc_url("OFX_PANEL_GUIDE.md#quality");
                } else if (changed_param == kParamOpenAlphaHintGuide) {
                    url = help_doc_url("OFX_PANEL_GUIDE.md#alpha-hint");
                } else if (changed_param == kParamOpenRecoverDetailsGuide) {
                    url = help_doc_url("OFX_PANEL_GUIDE.md#recover-original-details");
                } else if (changed_param == kParamOpenTilingGuide) {
                    url = help_doc_url("OFX_PANEL_GUIDE.md#tiling");
                } else if (changed_param == kParamOpenResolveTutorial) {
                    url = help_doc_url("OFX_RESOLVE_TUTORIALS.md");
                } else {
                    url = help_doc_url("TROUBLESHOOTING.md");
                }

                if (!open_external_url(url)) {
                    post_message(kOfxMessageError,
                                 ("Failed to open documentation URL: " + url).c_str(), instance);
                }
                return kOfxStatOK;
            }
            if (changed_param == kParamEnableTiling || changed_param == kParamAutoDespeckle ||
                changed_param == kParamSourcePassthrough) {
                sync_dependent_params(data);
            }
            if (changed_param == kParamRenderTimeout || changed_param == kParamPrepareTimeout) {
                if (data->runtime_client) {
                    int render_t = common::kDefaultOfxRenderTimeoutSeconds;
                    int prepare_t = common::kDefaultOfxPrepareTimeoutSeconds;
                    if (data->render_timeout_param) {
                        g_suites.parameter->paramGetValue(data->render_timeout_param, &render_t);
                    }
                    if (data->prepare_timeout_param) {
                        g_suites.parameter->paramGetValue(data->prepare_timeout_param, &prepare_t);
                    }
                    data->runtime_client->set_request_timeout_ms(render_t * 1000);
                    data->runtime_client->set_prepare_timeout_ms(prepare_t * 1000);
                }
            }
            if (changed_param == kParamExecutionEngine || changed_param == kParamAllowCpuFallback) {
                data->requested_execution_engine = selected_execution_engine(data);
                invalidate_active_runtime_session(
                    data, "event=runtime_reconfigure param=" + changed_param);
                data->model_path.clear();
                data->active_resolution = 0;
                sync_runtime_panel_state_from_active_engine(data);
                data->runtime_panel_dirty = true;
            }
            if (changed_param == kParamQuantizationMode ||
                changed_param == kParamAllowCpuFallback) {
                int quant = 0;
                if (data->quantization_mode_param &&
                    g_suites.parameter->paramGetValue(data->quantization_mode_param, &quant) ==
                        kOfxStatOK) {
                    DeviceInfo requested_device = data->preferred_device;
                    if (requested_device.backend == Backend::Auto) {
                        requested_device = data->device;
                    }
                    const bool allow_cpu_fallback = allow_cpu_fallback_requested(data);
                    if (auto unsupported_quantization = unsupported_quantization_message(
                            requested_device.backend, quant, allow_cpu_fallback);
                        unsupported_quantization.has_value()) {
                        data->last_error = *unsupported_quantization;
                        data->quantization_error_active = true;
                        data->runtime_panel_dirty = true;
                    } else if (data->quantization_error_active) {
                        data->last_error.clear();
                        data->quantization_error_active = false;
                        data->runtime_panel_dirty = true;
                    }
                }
            }
        }
        std::string change_reason;
        if (get_string(in_args, kOfxPropChangeReason, change_reason) &&
            change_reason == kOfxChangePluginEdited) {
            return kOfxStatOK;
        }
    }
    if (data->runtime_panel_dirty && !data->in_render) {
        data->runtime_panel_dirty = false;
        update_runtime_panel_values(data);
    }
    return kOfxStatOK;
}

OfxStatus destroy_instance(OfxImageEffectHandle instance) {
    InstanceData* data = get_instance_data(instance);
    if (data != nullptr) {
        delete data;
        set_instance_data(instance, nullptr);
    }
    log_message("destroy_instance", "Instance destroyed.");
    return kOfxStatOK;
}

}  // namespace corridorkey::ofx
