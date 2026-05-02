#include <algorithm>
#include <atomic>
#include <chrono>
#include <corridorkey/engine.hpp>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <mutex>
#include <new>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "app/runtime_contracts.hpp"
#include "app/version_check.hpp"
#include "common/ofx_runtime_defaults.hpp"
#include "common/runtime_paths.hpp"
#include "ofx_backend_matching.hpp"
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
#elif defined(__linux__)
#include <dlfcn.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;
#endif

namespace corridorkey::ofx {

void set_param_secret(OfxParamHandle param, bool secret);

namespace {

constexpr const char* kRepoHelpBaseUrl =
    "https://github.com/alexandremendoncaalvaro/CorridorKey-Runtime/blob/"
    "main/help/";
constexpr const char* kReleasesIndexUrl =
    "https://github.com/alexandremendoncaalvaro/CorridorKey-Runtime/releases/latest";

std::string help_doc_url(const char* filename) {
    return std::string(kRepoHelpBaseUrl) + filename;
}

struct UpdateCheckState {
    std::atomic<bool> started{false};
    std::atomic<bool> done{false};
    std::mutex mutex;
    std::optional<app::CachedCheck> cache;
};

UpdateCheckState& update_check_state() {
    static UpdateCheckState state;
    return state;
}

void kickoff_global_update_check(bool force_refresh = false) {
    auto& state = update_check_state();
    if (force_refresh) {
        state.done.store(false, std::memory_order_release);
        state.started.store(false, std::memory_order_release);
    }
    bool expected = false;
    if (!state.started.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        return;
    }
    std::thread([&state, force_refresh]() {
        app::VersionCheckOptions options;
        options.current_version = CORRIDORKEY_DISPLAY_VERSION_STRING;
        options.include_prereleases = true;
        if (force_refresh) {
            options.cache_ttl = std::chrono::seconds(0);
        }
        (void)app::check_for_update(options);
        auto cache = app::read_cache(app::default_cache_path());
        {
            std::lock_guard<std::mutex> lock(state.mutex);
            state.cache = cache;
        }
        state.done.store(true, std::memory_order_release);
    }).detach();
}

std::optional<app::UpdateInfo> current_update_info(bool include_prereleases) {
    auto& state = update_check_state();
    if (!state.done.load(std::memory_order_acquire)) {
        return std::nullopt;
    }
    std::lock_guard<std::mutex> lock(state.mutex);
    if (!state.cache.has_value()) {
        return std::nullopt;
    }
    return app::select_update(*state.cache, CORRIDORKEY_DISPLAY_VERSION_STRING,
                              include_prereleases);
}

std::string update_banner_text(const app::UpdateInfo& info) {
    return std::string("New version available: v") + info.latest_version +
           (info.is_prerelease ? " (pre-release)" : "");
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
#elif defined(__linux__)
    // xdg-open is the freedesktop.org standard URL opener on Linux. It is
    // resolved from PATH because its concrete location varies across
    // distributions (often /usr/bin, sometimes /usr/local/bin).
    char* const argv[] = {const_cast<char*>("xdg-open"), const_cast<char*>(url.c_str()), nullptr};
    pid_t pid = 0;
    return posix_spawnp(&pid, "xdg-open", nullptr, nullptr, argv, environ) == 0;
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
        case Backend::TorchTRT:
            return "torchtrt";
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
        case Backend::TorchTRT:
            return "Torch-TensorRT GPU";
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

bool runtime_server_binary_present(const std::filesystem::path& runtime_server_path) {
    return !runtime_server_path.empty() && std::filesystem::exists(runtime_server_path);
}

bool is_windows_gpu_backend(Backend backend) {
    return backend == Backend::TensorRT || backend == Backend::CUDA ||
           backend == Backend::DirectML || backend == Backend::WindowsML ||
           backend == Backend::OpenVINO;
}

EngineCreateOptions ofx_engine_options(const DeviceInfo& requested_device) {
    EngineCreateOptions options;
    options.allow_cpu_fallback = false;
    options.disable_cpu_ep_fallback = (requested_device.backend != Backend::CPU);
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

void sync_runtime_panel_state_from_active_session(InstanceData* data) {
    if (data == nullptr) {
        return;
    }

    data->runtime_panel_state.requested_quality_mode = data->active_quality_mode;
    data->runtime_panel_state.requested_resolution = data->requested_resolution;
    data->runtime_panel_state.effective_resolution = data->active_resolution;
    data->runtime_panel_state.safe_quality_ceiling_resolution =
        app::max_supported_resolution_for_device(data->device).value_or(0);
    data->runtime_panel_state.cpu_quality_guardrail_active = data->cpu_quality_guardrail_active;
    data->runtime_panel_state.artifact_path = data->model_path;
    const bool client_has_session =
        data->runtime_client != nullptr && data->runtime_client->has_session();
    data->runtime_panel_state.session_prepared = client_has_session;
    data->runtime_panel_state.session_ref_count =
        client_has_session ? data->runtime_client->session_ref_count() : 0;
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

    if (data->runtime_client != nullptr && data->runtime_client->has_session()) {
        data->runtime_panel_state.session_prepared = true;
        data->runtime_panel_state.session_ref_count = data->runtime_client->session_ref_count();
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

        entries.push_back(
            entry.path().filename().string() + "|" + std::to_string(size_bytes) + "|" +
            std::to_string(static_cast<long long>(write_time.time_since_epoch().count())));
    }

    std::sort(entries.begin(), entries.end());
    std::uint64_t token = kOffsetBasis;
    for (const auto& entry : entries) {
        token = mix_cache_token(token, entry);
    }
    return token;
}

QualityCompileFailureCacheContext build_quality_compile_failure_cache_context(
    const InstanceData& data, const DeviceInfo& requested_device) {
    QualityCompileFailureCacheContext context;
    context.models_root = data.models_root;
    context.models_bundle_token = models_bundle_token(data.models_root);
    context.backend = requested_device.backend;
    context.device_index = requested_device.device_index;
    context.available_memory_mb = requested_device.available_memory_mb;
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

bool is_nested_stage_name(std::string_view stage_name, std::string_view candidate_parent_name) {
    return stage_name.size() > candidate_parent_name.size() + 1 &&
           stage_name.compare(0, candidate_parent_name.size(), candidate_parent_name) == 0 &&
           stage_name[candidate_parent_name.size()] == '_';
}

bool stage_has_parent(const std::vector<StageTiming>& timings, std::size_t stage_index) {
    const auto& stage_name = timings[stage_index].name;
    for (std::size_t index = 0; index < timings.size(); ++index) {
        if (index == stage_index) {
            continue;
        }
        if (is_nested_stage_name(stage_name, timings[index].name)) {
            return true;
        }
    }
    return false;
}

bool stage_has_children(const std::vector<StageTiming>& timings, std::size_t stage_index) {
    const auto& stage_name = timings[stage_index].name;
    for (std::size_t index = 0; index < timings.size(); ++index) {
        if (index == stage_index) {
            continue;
        }
        if (is_nested_stage_name(timings[index].name, stage_name)) {
            return true;
        }
    }
    return false;
}

double exclusive_stage_total_ms(const std::vector<StageTiming>& timings) {
    double total_ms = 0.0;
    for (std::size_t index = 0; index < timings.size(); ++index) {
        if (stage_has_parent(timings, index)) {
            continue;
        }
        total_ms += timings[index].total_ms;
    }
    return total_ms;
}

const StageTiming* hottest_actionable_stage(const std::vector<StageTiming>& timings) {
    const StageTiming* hottest_stage = nullptr;
    for (std::size_t index = 0; index < timings.size(); ++index) {
        if (stage_has_children(timings, index)) {
            continue;
        }
        if (hottest_stage == nullptr || timings[index].total_ms > hottest_stage->total_ms) {
            hottest_stage = &timings[index];
        }
    }

    if (hottest_stage != nullptr) {
        return hottest_stage;
    }

    for (const auto& timing : timings) {
        if (hottest_stage == nullptr || timing.total_ms > hottest_stage->total_ms) {
            hottest_stage = &timing;
        }
    }
    return hottest_stage;
}

std::string runtime_timings_runtime_label_impl(const InstanceData& data) {
    const double backend_total_ms = exclusive_stage_total_ms(data.last_render_stage_timings);
    const StageTiming* hottest_stage = hottest_actionable_stage(data.last_render_stage_timings);
    const double last_ms = data.last_frame_ms > 0.0 ? data.last_frame_ms : backend_total_ms;
    if (last_ms <= 0.0) {
        return "No frames processed";
    }

    const double avg_ms = data.avg_frame_ms > 0.0 ? data.avg_frame_ms : last_ms;
    std::string label = format_duration_ms(last_ms) + " | Avg: " + format_duration_ms(avg_ms);
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

double elapsed_ms_since(std::chrono::steady_clock::time_point start_time) {
    const auto elapsed = std::chrono::steady_clock::now() - start_time;
    return std::chrono::duration<double, std::milli>(elapsed).count();
}

// Surface ErrorCode::InsufficientMemory from prepare_session as a blocking
// OFX alert. The broker returns this code when the requested model shape
// will not fit the current GPU working-set headroom. Quietly downshifting
// to a smaller shape is out of scope (that would change matting edge
// quality behind the user's back), so the correct response is to tell the
// user what happened and what they can do about it.
bool maybe_surface_insufficient_memory(OfxImageEffectHandle instance, const Error& error) {
    if (error.code != ErrorCode::InsufficientMemory) {
        return false;
    }
    std::string message =
        "Not enough GPU memory for the current model. Lower Target Resolution or "
        "Quality, or close other GPU-intensive apps (browsers, other Resolve "
        "projects) and try again.\n\nDetail: " +
        error.message;
    post_message(kOfxMessageError, message.c_str(), instance);
    return true;
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
        log_message("update_runtime_panel_values", "skip reason=null_data");
        return;
    }

    // OFX 1.4 / 1.5 threading model: paramSetValue is main-thread-only per
    // ofxParam.h:1088 ("paramSetValue should only be called from within a
    // kOfxActionInstanceChanged or interact action"). If we are inside a
    // render-thread action (Render, BeginSequenceRender, EndSequenceRender,
    // IsIdentity, GetRegionOfDefinition, GetRegionsOfInterest,
    // GetFramesNeeded) defer by marking dirty so the next main-thread
    // action flushes the panel.
    //
    // DaVinci Resolve has historically tolerated render-thread paramSetValue
    // through internal locking, and the Resolve user experience has long
    // depended on the runtime status panel updating live during render.
    // Per the OFX 1.4 spec on kOfxPropName, plugins are expected to apply
    // host-specific workarounds for known host quirks. Resolve therefore
    // takes the permissive path here while Foundry Nuke and any other host
    // we have not validated take the canonical defer-to-main-thread path.
    // References:
    // https://openfx.readthedocs.io/en/main/Reference/ofxThreadSafety.html
    // https://openfx.readthedocs.io/en/main/Reference/ofxRendering.html
    // https://openfx.readthedocs.io/en/main/Reference/ofxPropertiesByObject.html
    if ((data->in_render || data->in_render_sequence) && !is_resolve_host()) {
        data->runtime_panel_dirty = true;
        log_message("update_runtime_panel_values",
                    std::string("defer reason=render_thread in_render=") +
                        (data->in_render ? "1" : "0") + " in_render_sequence=" +
                        (data->in_render_sequence ? "1" : "0"));
        return;
    }
    log_message("update_runtime_panel_values", "enter flush=full");
    data->runtime_panel_dirty = false;

    sync_runtime_panel_session_state_impl(data);

    const bool has_session =
        data->runtime_client != nullptr && data->runtime_client->has_session();
    const bool is_loading = !has_session && data->last_error.empty();
    const bool has_recorded_frame_timing =
        data->last_frame_ms > 0.0 || !data->last_render_stage_timings.empty();

    set_string_param_value(data->runtime_processing_param,
                           processing_backend_label(data->device.backend));
    set_string_param_value(data->runtime_device_param, processing_device_label(data->device));
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
    set_string_param_value(data->runtime_timings_param, is_loading && !has_recorded_frame_timing
                                                            ? "Loading..."
                                                            : runtime_timings_runtime_label(*data));
    set_string_param_value(data->runtime_backend_work_param,
                           is_loading ? "Loading..." : runtime_backend_work_runtime_label(*data));

    const bool include_prereleases = get_bool_param_value(data->include_pre_releases_param, false);
    auto info = current_update_info(include_prereleases);
    const bool show_banner = info.has_value();
    set_param_secret(data->update_status_param, !show_banner);
    set_param_secret(data->open_update_page_param, !show_banner);
    if (show_banner) {
        set_string_param_value(data->update_status_param, update_banner_text(*info));
    } else {
        set_string_param_value(data->update_status_param, "");
    }
    log_message("update_runtime_panel_values",
                std::string("exit ok banner=") + (show_banner ? "1" : "0"));
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
#elif defined(__APPLE__) || defined(__linux__)
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
    const DeviceInfo& requested_device, const QualityArtifactSelection& selection,
    int requested_quality_mode) {
    app::OfxRuntimePrepareSessionRequest request;
    request.client_instance_id = "quality_switch";
    request.model_path = selection.executable_model_path;
    request.artifact_name = selection.executable_model_path.filename().string();
    request.requested_device = requested_device;
    request.engine_options = ofx_engine_options(requested_device);
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
void set_param_secret(OfxParamHandle param, bool secret);
void sync_dependent_params(InstanceData* data);

bool ensure_runtime_client(InstanceData* data, OfxImageEffectHandle instance) {
    if (data == nullptr) {
        return false;
    }
    if (data->runtime_client != nullptr) {
        return true;
    }

    if (data->runtime_server_path.empty()) {
        data->runtime_server_path =
            resolve_ofx_runtime_server_binary(plugin_module_path().value_or(""));
    }
    if (!runtime_server_binary_present(data->runtime_server_path)) {
        // The .ofx is the host's address space; running ORT/TRT-RTX in it
        // collides with hosts that pre-load their own CUDA stack (Foundry
        // Nuke ships cudart64_12.dll 12.8 next to its executable, which the
        // Win32 loader binds before our 12.9 copy because module-name
        // uniqueness is keyed on basename). The runtime server lives in a
        // separate process, so its loader is independent. Refuse to start
        // without it rather than papering over the failure.
        data->last_error =
            "CorridorKey runtime server binary not found alongside the OFX bundle.";
        log_message("ensure_runtime_client", data->last_error);
        post_message(kOfxMessageError, data->last_error.c_str(), instance);
        return false;
    }

    int render_timeout_s = common::kDefaultOfxRenderTimeoutSeconds;
    int prepare_timeout_s = common::kDefaultOfxPrepareTimeoutSeconds;
    if (data->render_timeout_param != nullptr) {
        g_suites.parameter->paramGetValue(data->render_timeout_param, &render_timeout_s);
    }
    if (data->prepare_timeout_param != nullptr) {
        g_suites.parameter->paramGetValue(data->prepare_timeout_param, &prepare_timeout_s);
    }

    OfxRuntimeClientOptions client_options;
    client_options.endpoint = common::default_ofx_runtime_endpoint();
    client_options.server_binary = data->runtime_server_path;
    client_options.request_timeout_ms = render_timeout_s * 1000;
    client_options.prepare_timeout_ms = prepare_timeout_s * 1000;
    auto runtime_client = OfxRuntimeClient::create(std::move(client_options));
    if (!runtime_client) {
        data->last_error = runtime_client.error().message;
        log_message("ensure_runtime_client",
                    "Runtime client init failed: " + runtime_client.error().message);
        post_message(kOfxMessageError, data->last_error.c_str(), instance);
        return false;
    }
    data->runtime_client = std::move(*runtime_client);
    log_message("ensure_runtime_client", "Using out-of-process OFX runtime.");
    return true;
}

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
    g_suites.parameter->paramGetHandle(param_set, kParamUpdateStatus, &data->update_status_param,
                                       nullptr);
    g_suites.parameter->paramGetHandle(param_set, kParamOpenUpdatePage,
                                       &data->open_update_page_param, nullptr);
    g_suites.parameter->paramGetHandle(param_set, kParamIncludePreReleases,
                                       &data->include_pre_releases_param, nullptr);

    kickoff_global_update_check();

    // sync_dependent_params and set_runtime_panel_status used to run here.
    // Both invoke the OFX parameter suite (set_param_enabled and
    // paramSetValue) inside kOfxImageEffectActionCreateInstance, which
    // strict hosts (Foundry Nuke 17) reject by crashing the host. The
    // dependent-param initial enabled state is now expressed in
    // describe_in_context via define_int_param's enabled flag, and the
    // runtime status panel is populated on first render via
    // update_runtime_panel — both align with the canonical OFX 1.4
    // contract that scopes createInstance to handle caching only.

    // The runtime server path resolution, binary-presence check, and IPC
    // client init were previously done here. They are now deferred to the
    // first render via ensure_runtime_client(). Per OFX 1.4, createInstance
    // is for caching handles and allocating per-instance state only;
    // spawning a subprocess from this action triggers host-side stalls and
    // crashes on stricter hosts (Foundry Nuke 17).
    data->models_root = resolve_models_root();
    // Device detection and capability probes call into windows_rtx_probe /
    // mlx_probe / linux_cuda_probe, which transitively reference ONNX Runtime
    // symbols. Importing onnxruntime.dll into the .ofx address space conflicts
    // with hosts that pre-load their own CUDA stack (Foundry Nuke ships
    // cudart64_12.dll 12.8 next to its executable, which the Win32 loader
    // binds before our 12.9 copy because module-name uniqueness is keyed on
    // basename). The runtime server in its own process has its own loader and
    // is the only place we exercise the inference stack. The .ofx therefore
    // ships with placeholder device info; the real DeviceInfo and capability
    // payload are populated by the server's prepare_session response on first
    // render.
    DeviceInfo detected_device;
    detected_device.backend = Backend::Auto;
    detected_device.name = "Pending runtime server bootstrap";
    detected_device.available_memory_mb = 0;
    log_message("create_instance",
                "Device detection deferred to runtime server (out-of-process loader isolation).");
    RuntimeCapabilities capabilities;
#if defined(__APPLE__)
    capabilities.platform = "macos";
#elif defined(_WIN32)
    capabilities.platform = "windows";
#else
    capabilities.platform = "linux";
#endif
    data->runtime_capabilities = capabilities;
    log_message("create_instance", std::string("Platform: ") + capabilities.platform);

    int initial_quality_mode = kQualityPreview;
    if (data->quality_mode_param != nullptr) {
        g_suites.parameter->paramGetValue(data->quality_mode_param, &initial_quality_mode);
    }

    DeviceInfo preferred_device = detected_device;
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
    data->runtime_panel_state.artifact_path.clear();
    data->model_path.clear();
    data->last_error.clear();

    log_message("create_instance", "Deferring runtime session bootstrap until first render.");
    // update_runtime_panel_values used to run here; it calls paramSetValue
    // for each runtime-status string param plus set_param_secret on the
    // update banner toggles, both of which are side-effects forbidden in
    // canonical createInstance and reject by strict hosts. The render path
    // (and instance_changed) already drive update_runtime_panel before the
    // user sees results, so the panel converges to the correct state on
    // first interaction.

    // Natron-documented Nuke fix for the update banner secret quirk: the
    // params were declared visible (secret=false) in describe so that a
    // future reveal is permitted; here we set them secret at the end of
    // create_instance when no banner is yet known. The first instance has
    // no cached update result, so default to hidden. The flush triggered by
    // the next instance_changed or sync_private_data reveals the banner if
    // the GitHub update-check thread reports one.
    // Reference: https://github.com/MrKepzie/Natron/wiki/OpenFX-plugin-programming-guide-(Advanced-issues)
    set_param_secret(data->update_status_param, true);
    set_param_secret(data->open_update_page_param, true);

    set_instance_data(instance, data.release());
    log_create_total("success", "bootstrap=deferred");
    return kOfxStatOK;
}

bool ensure_engine_for_quality(InstanceData* data, int quality_mode, int input_width,
                               int input_height, QualityFallbackMode fallback_mode,
                               int coarse_resolution_override, RefinementMode refinement_mode,
                               std::string_view screen_color) {
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

    if (data == nullptr || data->runtime_client == nullptr) {
        log_quality_total("no_engine");
        return true;
    }

    DeviceInfo requested_device = data->preferred_device;
    if (requested_device.backend == Backend::Auto) {
        requested_device = data->device;
    }

    const int requested_quality_mode = quality_mode;
    const int requested_resolution =
        resolve_target_resolution(requested_quality_mode, input_width, input_height);
    const bool allow_unrestricted_quality_attempt =
        allow_unrestricted_quality_attempt_for_request(*data, requested_quality_mode,
                                                       requested_device);
    const std::string manual_override_warning =
        manual_override_warning_message(requested_device, requested_quality_mode,
                                        requested_resolution, allow_unrestricted_quality_attempt);
    // CPU rendering retired with INT8: the quality-guardrail and CPU-fallback
    // warning paths can never fire at runtime now. The local stubs below keep
    // helpers like set_runtime_panel_state_for_failed_quality_request and
    // should_abort_quality_fallback_after_compile_failure callable while the
    // wider panel-state struct cleanup is staged in a follow-up commit.
    constexpr bool cpu_quality_guardrail_active = false;
    const std::string cpu_fallback_warning;
    const std::string cpu_quality_guardrail_warning;
    data->runtime_panel_state.requested_quality_mode = requested_quality_mode;
    data->runtime_panel_state.requested_resolution = requested_resolution;
    const auto compile_cache_context =
        build_quality_compile_failure_cache_context(*data, requested_device);
    prepare_quality_compile_failure_cache(data->quality_compile_failure_cache,
                                          compile_cache_context);
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
        data->models_root, requested_device.backend, requested_quality_mode, input_width,
        input_height, requested_device.available_memory_mb, fallback_mode,
        coarse_resolution_override, allow_unrestricted_quality_attempt, screen_color);
    const auto original_selections = selections;
    if (!manual_override_warning.empty()) {
        log_message("ensure_engine_for_quality", manual_override_warning);
    }
    if (selections.empty()) {
        const auto expected_artifacts = expected_quality_artifact_paths(
            data->models_root, requested_device.backend, requested_quality_mode, input_width,
            input_height, requested_device.available_memory_mb, fallback_mode,
            coarse_resolution_override, allow_unrestricted_quality_attempt, screen_color);
        data->last_error = missing_quality_artifact_message(
            data->models_root, requested_device.backend, requested_quality_mode, input_width,
            input_height, requested_device.available_memory_mb, fallback_mode,
            coarse_resolution_override, allow_unrestricted_quality_attempt, screen_color);
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

    if (data->runtime_client != nullptr && data->runtime_client->has_session()) {
        data->device = data->runtime_client->current_device();
    }
    bool current_backend_matches = backend_matches_request(data->device, requested_device);

    for (const auto& selection : selections) {
        const bool session_alive =
            data->runtime_client != nullptr && data->runtime_client->has_session();
        if (current_backend_matches && session_alive &&
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
            data->cpu_quality_guardrail_active = cpu_quality_guardrail_active;
            data->last_warning = runtime_warning;
            data->last_error.clear();
            sync_runtime_panel_state_from_active_session(data);
            update_runtime_panel(data);
            log_quality_total("reused_engine");
            return true;
        }

        constexpr std::string_view kQualitySwitchPhase = "quality_switch";
        log_engine_event("ensure_engine_for_quality", "engine_create_begin", kQualitySwitchPhase,
                         requested_device, requested_device, selection.executable_model_path,
                         requested_resolution, selection.effective_resolution);

        DeviceInfo effective_device = requested_device;
        std::optional<BackendFallbackInfo> fallback = std::nullopt;
        // The previous code called set_string_param_value directly here to
        // surface a transient "Preparing ..." spinner on the runtime status
        // line. That bypasses the render-thread guard and crashes strict OFX
        // hosts. Mark the panel dirty instead; the next main-thread action
        // flushes the latest status. The transient spinner is the canonical
        // OFX trade-off for thread-safe paramSetValue.
        update_runtime_panel(data);
        auto prepare_result = data->runtime_client->prepare_session(
            build_prepare_request(requested_device, selection, requested_quality_mode),
            [&](const StageTiming& timing) {
                log_stage_timing("ensure_engine_for_quality", kQualitySwitchPhase, requested_device,
                                 selection.executable_model_path, requested_resolution,
                                 selection.effective_resolution, timing);
            });
        if (!prepare_result) {
            data->last_error = "Failed to prepare runtime session for " +
                               std::string(quality_mode_label(requested_quality_mode)) + " using " +
                               selection.executable_model_path.filename().string() + ": " +
                               prepare_result.error().message;
            log_engine_event("ensure_engine_for_quality", "engine_create_error", kQualitySwitchPhase,
                             requested_device, requested_device, selection.executable_model_path,
                             requested_resolution, selection.effective_resolution, std::nullopt,
                             prepare_result.error().message);
            log_message("ensure_engine_for_quality", data->last_error);
            // Out-of-memory during a live quality switch is actionable:
            // alert the user so they can relax Target Resolution instead
            // of getting a silent fallback. Stop iterating candidates.
            if (maybe_surface_insufficient_memory(data->effect, prepare_result.error())) {
                set_runtime_panel_state_for_failed_quality_request(
                    data, requested_quality_mode, requested_resolution,
                    cpu_quality_guardrail_active, selection.executable_model_path);
                update_runtime_panel(data);
                log_quality_total("insufficient_memory", data->last_error);
                return false;
            }
            if (should_record_quality_compile_failure(requested_device.backend,
                                                      prepare_result.error())) {
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
        sync_runtime_panel_state_from_active_session(data);
        log_message("ensure_engine_for_quality",
                    std::string("Switched to artifact ") +
                        selection.executable_model_path.filename().string());
        log_message("ensure_engine_for_quality",
                    std::string("Effective backend: ") + backend_label(data->device.backend));
        log_message("ensure_engine_for_quality",
                    "Effective resolution: " + std::to_string(data->active_resolution));
        update_runtime_panel(data);
        log_quality_total("success");
        return true;
    }

    log_quality_total("exhausted_candidates", data->last_error);
    update_runtime_panel(data);
    return false;
}

void update_runtime_panel(InstanceData* data) {
    // update_runtime_panel_values now owns the render-thread guard. Mark
    // dirty up front so a deferred call still has the right signal even when
    // the body short-circuits.
    if (data == nullptr) {
        return;
    }
    data->runtime_panel_dirty = true;
    update_runtime_panel_values(data);
}

void flush_runtime_panel(InstanceData* data) {
    if (data == nullptr || !data->runtime_panel_dirty) {
        return;
    }
    update_runtime_panel_values(data);
}

OfxStatus begin_sequence_render(OfxImageEffectHandle instance, OfxPropertySetHandle /*in_args*/) {
    InstanceData* data = get_instance_data(instance);
    if (data == nullptr) {
        return kOfxStatReplyDefault;
    }

    // Mark the render sequence so any update_runtime_panel call between begin
    // and end (including from ensure_engine_for_quality) defers paramSetValue
    // to the next main-thread action.
    data->in_render_sequence = true;
    clear_instance_render_caches(data, false);
    log_message("begin_sequence_render", "Sequence render state reset.");
    return kOfxStatOK;
}

OfxStatus end_sequence_render(OfxImageEffectHandle instance, OfxPropertySetHandle /*in_args*/) {
    InstanceData* data = get_instance_data(instance);
    if (data == nullptr) {
        return kOfxStatReplyDefault;
    }

    clear_instance_render_caches(data, false);
    log_message("end_sequence_render", "Sequence render caches cleared.");
    // Clear the sequence flag last so anything triggered above still defers
    // paramSetValue. The flush happens on the next main-thread action.
    data->in_render_sequence = false;
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

void set_param_secret(OfxParamHandle param, bool secret) {
    if (param == nullptr) {
        return;
    }
    OfxPropertySetHandle props = nullptr;
    if (g_suites.parameter->paramGetPropertySet(param, &props) == kOfxStatOK) {
        g_suites.property->propSetInt(props, kOfxParamPropSecret, 0, secret ? 1 : 0);
    }
}

void sync_dependent_params(InstanceData* data) {
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
}

OfxStatus instance_changed(OfxImageEffectHandle instance, OfxPropertySetHandle in_args) {
    InstanceData* data = get_instance_data(instance);
    if (data == nullptr) {
        return kOfxStatReplyDefault;
    }
    if (in_args != nullptr && g_suites.property != nullptr) {
        std::string changed_param;
        if (get_string(in_args, kOfxPropName, changed_param)) {
            if (changed_param == kParamOpenUpdatePage) {
                const bool include_prereleases =
                    get_bool_param_value(data->include_pre_releases_param, false);
                auto info = current_update_info(include_prereleases);
                const std::string url =
                    info.has_value() ? info->release_url : std::string(kReleasesIndexUrl);
                if (!open_external_url(url)) {
                    post_message(kOfxMessageError, ("Failed to open release page: " + url).c_str(),
                                 instance);
                }
                return kOfxStatOK;
            }
            if (changed_param == kParamCheckUpdates) {
                kickoff_global_update_check(true);
                data->runtime_panel_dirty = true;
                return kOfxStatOK;
            }
            if (changed_param == kParamIncludePreReleases) {
                data->runtime_panel_dirty = true;
            }
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
                    url = help_doc_url(select_tutorial_doc(g_host_name).c_str());
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

OfxStatus sync_private_data(OfxImageEffectHandle instance) {
    InstanceData* data = get_instance_data(instance);
    if (data == nullptr) {
        log_message("sync_private_data", "skip reason=null_instance_data");
        return kOfxStatReplyDefault;
    }
    // Main-thread action: flush any panel state that was marked dirty during
    // a render sequence. Hosts call this before save/serialize, which is
    // exactly when the user needs the runtime status reflected in the project
    // file. paramSetValue is allowed here under the canonical SyncPrivateData
    // contract (OFX 1.5 vendor/openfx/include/ofxCore.h:298-301: "synchronise
    // any private data structures to its parameter set").
    //
    // Foundry Nuke 17 sessions in 2026-04 logs went silent mid-sync at
    // 17:15:42 after an Alpha Hint render. Without entry/exit telemetry we
    // could not tell whether the call entered, hung inside flush, or crashed
    // afterwards. The boundary log lines here are the smallest instrumentation
    // that distinguishes those three possibilities in the next UAT.
    log_message("sync_private_data",
                std::string("enter dirty=") + (data->runtime_panel_dirty ? "1" : "0") +
                    " in_render=" + (data->in_render ? "1" : "0") + " in_render_sequence=" +
                    (data->in_render_sequence ? "1" : "0"));
    flush_runtime_panel(data);
    log_message("sync_private_data", "exit ok");
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
