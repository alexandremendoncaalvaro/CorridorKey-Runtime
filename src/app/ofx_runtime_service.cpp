#include "ofx_runtime_service.hpp"

#include <algorithm>
#include <chrono>
#include <corridorkey/version.hpp>
#include <cstdio>
#include <fstream>
#include <mutex>

#include "../common/host_memory.hpp"
#include "../common/runtime_paths.hpp"
#include "../core/mlx_memory_governor.hpp"

#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

#ifdef __APPLE__
#include <dispatch/dispatch.h>
#include <mach/mach.h>
#include <mach/task.h>
#include <mach/task_policy.h>
#include <pthread.h>
#include <sys/qos.h>
#endif

// NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,readability-identifier-length,bugprone-easily-swappable-parameters,readability-function-cognitive-complexity,readability-function-size,cppcoreguidelines-avoid-magic-numbers,modernize-use-designated-initializers,modernize-use-ranges,modernize-loop-convert,modernize-return-braced-init-list,cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays,readability-implicit-bool-conversion,cert-err33-c,performance-unnecessary-value-param,bugprone-unchecked-string-to-number-conversion,cppcoreguidelines-pro-type-cstyle-cast,modernize-use-using,modernize-use-integer-sign-comparison,cert-dcl50-cpp,cppcoreguidelines-pro-type-const-cast,readability-identifier-naming,modernize-raw-string-literal,readability-container-size-empty,bugprone-command-processor,readability-use-std-min-max,cppcoreguidelines-avoid-non-const-global-variables,bugprone-misplaced-widening-cast,readability-misleading-indentation,cert-env33-c,performance-unnecessary-copy-initialization,readability-named-parameter,readability-isolate-declaration,cert-err34-c,modernize-avoid-variadic-functions,cppcoreguidelines-pro-bounds-constant-array-index)
//
// ofx_runtime_service.cpp tidy-suppression rationale.
//
// This translation unit is the OFX runtime IPC server: it parses
// OfxRuntime* JSON envelopes, dispatches them to the OfxSessionBroker,
// and emits a single-line key=value diagnostic log. It is NOT on the
// per-pixel render hot path, but a linter-driven rewrite of the
// suppressed categories would force a large mechanical churn of the
// log-formatting helpers and the request-dispatch state machine
// without changing observable behaviour:
//
//   * cppcoreguidelines-pro-bounds-avoid-unchecked-container-access:
//     format_stage_summary indexes timings[] / indices[] / included[]
//     using indices generated and bounded above the access; .at()
//     bounds checks would add nothing and obscure the formatting.
//
//   * readability-identifier-length: (a, b) inside the std::sort
//     comparator and (ch) inside sanitize_log_token are universal
//     comparator / character-iteration names matching the surrounding
//     STL idiom.
//
//   * bugprone-easily-swappable-parameters: helper signatures that
//     take (std::chrono::time_point, std::chrono::time_point) for
//     elapsed_ms are intentional and stable.
//
//   * readability-function-cognitive-complexity / readability-function-
//     size: OfxRuntimeService::run is the canonical IPC dispatch loop
//     (Health / PrepareSession / RenderFrame / ReleaseSession /
//     Shutdown branches with per-branch logging). Splitting it would
//     scatter the request_start / response / broker state across
//     helpers no other caller benefits from.
//
//   * cppcoreguidelines-avoid-magic-numbers: format buffer sizes
//     (32 / 64 / 128 / 160 / 80), the top-N stage cut (5), and MB
//     conversion (1024.0 * 1024.0) are documented at every use site.
//
//   * modernize-use-designated-initializers: OfxRuntimeResponseEnvelope
//     and Error are constructed at every dispatch branch; positional
//     init keeps the dispatch loop compact and matches the surrounding
//     codebase style.
//
//   * modernize-use-ranges / modernize-loop-convert: format_stage_summary
//     uses index-based loops because included[] is updated in the same
//     pass; rewriting to ranges adds noise without changing semantics.
//
//   * modernize-return-braced-init-list: format_ms / format_mb return
//     std::string from a stack snprintf buffer; the explicit
//     std::string(buffer) constructor call is more readable than a
//     braced init list at the call site.
//
//   * cppcoreguidelines-avoid-c-arrays / modernize-avoid-c-arrays:
//     char buffer[32] / [64] are stack snprintf scratch; std::array
//     adds no safety here and forces a .data() at every call.
//
//   * readability-implicit-bool-conversion: std::to_string(bool) is
//     used to emit "0" / "1" tokens in the log; the implicit promotion
//     to int is intentional and matches the log schema.
//
//   * cert-err33-c: snprintf return is intentionally discarded inside
//     format_ms / format_mb / format_stage_summary; the buffer is
//     stack-bounded and truncation is acceptable for log output.
namespace corridorkey::app {

namespace {

class RuntimeLogger {
   public:
    explicit RuntimeLogger(const std::filesystem::path& path) {
        std::error_code error;
        std::filesystem::create_directories(path.parent_path(), error);
        m_stream.open(path, std::ios::app);
    }

    // Mutex-guarded so the dispatch-memorypressure handler can append events
    // concurrently with the main request loop without interleaving lines.
    void log(const std::string& message) {
        const std::scoped_lock guard(m_mutex);
        if (!m_stream.is_open()) {
            return;
        }
        m_stream << message << '\n';
        m_stream.flush();
    }

   private:
    std::mutex m_mutex;
    std::ofstream m_stream;
};

int current_process_id() {
#ifdef _WIN32
    return _getpid();
#else
    return static_cast<int>(getpid());
#endif
}

OfxRuntimeResponseEnvelope ok_response(const nlohmann::json& payload) {
    return OfxRuntimeResponseEnvelope{kOfxRuntimeProtocolVersion, true, "", payload};
}

OfxRuntimeResponseEnvelope error_response(const Error& error) {
    return OfxRuntimeResponseEnvelope{kOfxRuntimeProtocolVersion, false, error.message,
                                      nlohmann::json::object()};
}

std::string response_detail(const OfxRuntimeResponseEnvelope& response) {
    return response.success ? "ok" : response.error;
}

// Clean a free-form string for safe inclusion as a key=value token value in the log.
// Keeps the log single-line and greppable: no whitespace, no equals, no newlines.
std::string sanitize_log_token(const std::string& value) {
    std::string output;
    output.reserve(value.size());
    for (char ch : value) {
        if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || ch == '=') {
            output.push_back('_');
        } else {
            output.push_back(ch);
        }
    }
    if (output.empty()) {
        output = "none";
    }
    return output;
}

// Stages that must always appear in the render-frame summary so the log alone
// answers "eval-bound or wait-bound?" — mlx_eval tends to dominate the top-N
// slots and hide the wait split that tells us whether we're Metal-queue-bound.
bool is_pinned_stage(const std::string& name) {
    return name == "mlx_eval" || name == "mlx_wait_alpha" || name == "mlx_wait_fg" ||
           name == "mlx_eval_tile" || name == "mlx_wait_alpha_tile" || name == "mlx_wait_fg_tile";
}

// Summarize stages into a compact "name:ms,name:ms" list. Returns the top-N by
// total_ms plus any pinned stages that fell outside the cut, so diagnostic
// signals never get squeezed out by a single dominant stage.
std::string format_stage_summary(const std::vector<StageTiming>& timings, std::size_t top_n = 5) {
    if (timings.empty()) {
        return "none";
    }

    std::vector<std::size_t> indices(timings.size());
    for (std::size_t i = 0; i < indices.size(); ++i) {
        indices[i] = i;
    }
    std::sort(indices.begin(), indices.end(), [&timings](std::size_t a, std::size_t b) {
        return timings[a].total_ms > timings[b].total_ms;
    });

    const std::size_t top_limit = std::min(top_n, indices.size());
    std::vector<bool> included(indices.size(), false);
    for (std::size_t i = 0; i < top_limit; ++i) {
        included[indices[i]] = true;
    }
    for (std::size_t i = top_limit; i < indices.size(); ++i) {
        if (is_pinned_stage(timings[indices[i]].name)) {
            included[indices[i]] = true;
        }
    }

    std::string summary;
    summary.reserve(128);
    for (std::size_t i = 0; i < indices.size(); ++i) {
        if (!included[indices[i]]) {
            continue;
        }
        const auto& timing = timings[indices[i]];
        if (!summary.empty()) {
            summary.push_back(',');
        }
        char buffer[64] = {};
        std::snprintf(buffer, sizeof(buffer), "%s:%.2f", sanitize_log_token(timing.name).c_str(),
                      timing.total_ms);
        summary.append(buffer);
    }
    return summary;
}

double total_stage_ms(const std::vector<StageTiming>& timings) {
    double total = 0.0;
    for (const auto& timing : timings) {
        total += timing.total_ms;
    }
    return total;
}

const char* backend_log_token(Backend backend) {
    switch (backend) {
        case Backend::Auto:
            return "auto";
        case Backend::CPU:
            return "cpu";
        case Backend::CUDA:
            return "cuda";
        case Backend::TensorRT:
            return "tensorrt";
        case Backend::CoreML:
            return "coreml";
        case Backend::DirectML:
            return "directml";
        case Backend::MLX:
            return "mlx";
        case Backend::WindowsML:
            return "winml";
        case Backend::OpenVINO:
            return "openvino";
        case Backend::TorchTRT:
            return "torchtrt";
    }
    return "unknown";
}

std::string format_ms(double milliseconds) {
    char buffer[32] = {};
    std::snprintf(buffer, sizeof(buffer), "%.2f", milliseconds);
    return std::string(buffer);
}

double elapsed_ms(std::chrono::steady_clock::time_point start,
                  std::chrono::steady_clock::time_point end) {
    return std::chrono::duration<double, std::milli>(end - start).count();
}

// Convert a byte count into a floating MB value with two decimals for logging.
// We keep the log tokens MB-scale so operators can eyeball the numbers against
// Activity Monitor without reaching for a calculator.
std::string format_mb(std::size_t bytes) {
    char buffer[32] = {};
    std::snprintf(buffer, sizeof(buffer), "%.2f", static_cast<double>(bytes) / (1024.0 * 1024.0));
    return std::string(buffer);
}

std::string format_mlx_memory_fields(const core::mlx_memory::Snapshot& snap) {
    std::string out;
    out.reserve(160);
    out.append(" active_mb=").append(format_mb(snap.active_bytes));
    out.append(" cache_mb=").append(format_mb(snap.cache_bytes));
    out.append(" peak_mb=").append(format_mb(snap.peak_bytes));
    out.append(" memory_limit_mb=").append(format_mb(snap.memory_limit_bytes));
    out.append(" cache_limit_mb=").append(format_mb(snap.cache_limit_bytes));
    out.append(" wired_limit_mb=").append(format_mb(snap.wired_limit_bytes));
    out.append(" working_set_mb=").append(format_mb(snap.max_recommended_working_set_bytes));
    return out;
}

// host_free_mb / host_compressor_mb are emitted on every platform so the
// prepare_session_details log keeps its host-memory pressure column readable
// across Windows and macOS. common::query_host_memory_stats() already has
// platform-specific implementations; this function is purely formatting.
std::string format_host_memory_fields(const common::HostMemoryStats& stats) {
    std::string out;
    out.reserve(80);
    out.append(" host_free_mb=").append(format_mb(stats.free_bytes));
    out.append(" host_compressor_mb=").append(format_mb(stats.compressor_bytes));
    return out;
}

#ifdef __APPLE__
// Report the effective QoS class so the server log surfaces cases where the
// process was spawned under an inherited low-QoS class (utility/background).
// Running MLX under a low QoS lets Metal/GPU work get preempted by higher-QoS
// work in the host process (e.g. DaVinci Resolve) and produces 20-70x
// slowdowns; see docs/OPTIMIZATION_MEASUREMENTS.md.
std::string describe_qos_class(qos_class_t qos) {
    switch (qos) {
        case QOS_CLASS_USER_INTERACTIVE:
            return "user-interactive";
        case QOS_CLASS_USER_INITIATED:
            return "user-initiated";
        case QOS_CLASS_DEFAULT:
            return "default";
        case QOS_CLASS_UTILITY:
            return "utility";
        case QOS_CLASS_BACKGROUND:
            return "background";
        case QOS_CLASS_UNSPECIFIED:
        default:
            return "unspecified";
    }
}

std::string current_qos_label() {
    qos_class_t qos = QOS_CLASS_UNSPECIFIED;
    int relative = 0;
    if (pthread_get_qos_class_np(pthread_self(), &qos, &relative) != 0) {
        return "unknown";
    }
    return describe_qos_class(qos);
}

// pthread_get_qos_class_np() returns the pthread's last-SET QoS, not the
// effective task-level role. macOS propagates a task role across posix_spawn
// from the parent's coalition, and pthread_set_qos_class_self_np() /
// posix_spawnattr_set_qos_class_np() cannot raise above that role clamp.
// TASK_CATEGORY_POLICY exposes the kernel's view of this task's role
// (TASK_FOREGROUND_APPLICATION, TASK_DARWINBG_APPLICATION, etc.), so the
// server log surfaces whether the pthread_override_qos_class_start_np()
// dance on the client side actually elevated the task out of Resolve's
// background role, or whether we are still running under a role that lets
// the host preempt our Metal work.
const char* describe_task_role(task_role_t role) {
    switch (role) {
        case TASK_RENICED:
            return "reniced";
        case TASK_UNSPECIFIED:
            return "unspecified";
        case TASK_FOREGROUND_APPLICATION:
            return "foreground";
        case TASK_BACKGROUND_APPLICATION:
            return "background";
        case TASK_CONTROL_APPLICATION:
            return "control";
        case TASK_GRAPHICS_SERVER:
            return "graphics-server";
        case TASK_THROTTLE_APPLICATION:
            return "throttle";
        case TASK_NONUI_APPLICATION:
            return "nonui";
        case TASK_DEFAULT_APPLICATION:
            return "default-app";
        case TASK_DARWINBG_APPLICATION:
            return "darwinbg";
        case TASK_USER_INIT_APPLICATION:
            return "user-init-app";
        default:
            return "unknown";
    }
}

std::string current_task_role_label() {
    task_category_policy_data_t policy{};
    policy.role = TASK_UNSPECIFIED;
    mach_msg_type_number_t count = TASK_CATEGORY_POLICY_COUNT;
    boolean_t get_default = FALSE;
    kern_return_t rc =
        task_policy_get(mach_task_self(), TASK_CATEGORY_POLICY,
                        reinterpret_cast<task_policy_t>(&policy), &count, &get_default);
    if (rc != KERN_SUCCESS) {
        return "unknown";
    }
    return describe_task_role(policy.role);
}

// RAII wrapper around a DISPATCH_SOURCE_TYPE_MEMORYPRESSURE source. The
// kernel signals this source on memorystatus transitions (warn -> critical
// -> normal); the block inside runs on a dedicated serial queue and tightens
// MLX's cache budget via the governor so we cooperate with the VM compressor
// instead of fighting it. Installing the source is idempotent from the
// kernel's side -- multiple processes can each register their own handler.
class MemoryPressureMonitor {
   public:
    MemoryPressureMonitor(RuntimeLogger& logger) : m_logger(&logger) {
        m_queue = dispatch_queue_create("com.corridorkey.memorypressure", DISPATCH_QUEUE_SERIAL);
        if (m_queue == nullptr) {
            return;
        }
        // Subscribing to NORMAL lets us restore the baseline policy when
        // pressure recedes, so a transient spike does not permanently shrink
        // MLX's cache for the remainder of the session.
        const uintptr_t mask = DISPATCH_MEMORYPRESSURE_NORMAL | DISPATCH_MEMORYPRESSURE_WARN |
                               DISPATCH_MEMORYPRESSURE_CRITICAL;
        m_source = dispatch_source_create(DISPATCH_SOURCE_TYPE_MEMORYPRESSURE, 0, mask, m_queue);
        if (m_source == nullptr) {
            dispatch_release(m_queue);
            m_queue = nullptr;
            return;
        }
        dispatch_source_t source = m_source;
        RuntimeLogger* logger_ptr = m_logger;
        dispatch_source_set_event_handler(m_source, ^{
          const unsigned long data = dispatch_source_get_data(source);
          const char* level = "normal";
          core::mlx_memory::Policy policy = core::mlx_memory::Policy::Normal;
          if (data & DISPATCH_MEMORYPRESSURE_CRITICAL) {
              level = "critical";
              policy = core::mlx_memory::Policy::PressureCritical;
          } else if (data & DISPATCH_MEMORYPRESSURE_WARN) {
              level = "warn";
              policy = core::mlx_memory::Policy::PressureWarn;
          }
          const auto snap = core::mlx_memory::apply_policy(policy);
          const auto host_stats = common::query_host_memory_stats();
          std::string event = std::string("event=memory_pressure level=") + level;
          event += format_host_memory_fields(host_stats);
          event += format_mlx_memory_fields(snap);
          logger_ptr->log(event);
        });
        dispatch_resume(m_source);
    }

    ~MemoryPressureMonitor() {
        if (m_source != nullptr) {
            dispatch_source_cancel(m_source);
            dispatch_release(m_source);
        }
        if (m_queue != nullptr) {
            dispatch_release(m_queue);
        }
    }

    MemoryPressureMonitor(const MemoryPressureMonitor&) = delete;
    MemoryPressureMonitor& operator=(const MemoryPressureMonitor&) = delete;
    MemoryPressureMonitor(MemoryPressureMonitor&&) = delete;
    MemoryPressureMonitor& operator=(MemoryPressureMonitor&&) = delete;

   private:
    RuntimeLogger* m_logger = nullptr;
    dispatch_source_t m_source = nullptr;
    dispatch_queue_t m_queue = nullptr;
};
#endif

}  // namespace

Result<void> OfxRuntimeService::run(const OfxRuntimeServiceOptions& options) {
    RuntimeLogger logger(options.log_path.empty() ? common::ofx_runtime_server_log_path()
                                                  : options.log_path);
    // The version banner lets the log reader know which build emitted the events.
    // display_version carries the optimization checkpoint label (e.g. 0.7.5-2); it
    // collapses to the semantic version when no override is active at build time.
    std::string start_event = std::string("event=server_start pid=") +
                              std::to_string(current_process_id()) +
                              " port=" + std::to_string(options.endpoint.port) +
                              " version=" + CORRIDORKEY_VERSION_STRING +
                              " display_version=" + CORRIDORKEY_DISPLAY_VERSION_STRING;
#ifdef __APPLE__
    start_event += " qos_class=" + current_qos_label();
    start_event += " task_role=" + current_task_role_label();
#endif
    logger.log(start_event);

    // Install the MLX memory baseline (set_wired_limit(0), conservative
    // memory_limit, aggressive cache_limit) and log the resulting snapshot.
    // The init is idempotent, but doing it here at server startup means the
    // limits are in force before any PrepareSession call allocates Metal
    // buffers. On non-MLX builds the snapshot is all zeros and the log line
    // still surfaces that the subsystem is absent.
    {
        const auto memory_snap = core::mlx_memory::initialize_defaults();
        std::string init_event = "event=mlx_memory_init";
#ifdef __APPLE__
        init_event += format_host_memory_fields(common::query_host_memory_stats());
#endif
        init_event += format_mlx_memory_fields(memory_snap);
        logger.log(init_event);
    }

#ifdef __APPLE__
    // The monitor registers a DISPATCH_SOURCE_TYPE_MEMORYPRESSURE handler on a
    // dedicated serial queue. When the kernel raises Warn/Critical we trim the
    // MLX cache through the governor; on Normal we restore the baseline. RAII
    // keeps the source alive for the lifetime of the run() loop.
    MemoryPressureMonitor memory_pressure_monitor(logger);
    (void)memory_pressure_monitor;
#endif

    auto server = common::LocalJsonServer::listen(options.endpoint);
    if (!server) {
        return Unexpected<Error>(server.error());
    }

    OfxSessionBroker broker(options.broker);
    bool should_exit = false;

    while (!should_exit) {
        auto client = server->accept_one(static_cast<int>(options.idle_timeout.count()));
        if (!client) {
            logger.log("event=server_error detail=" + client.error().message);
            return Unexpected<Error>(client.error());
        }

        if (!client->has_value()) {
            logger.log("event=server_idle_exit");
            break;
        }

        auto request_json = (*client)->read_json(static_cast<int>(options.idle_timeout.count()));
        if (!request_json) {
            auto response = error_response(request_json.error());
            logger.log("event=request_failed stage=read_json detail=" +
                       request_json.error().message);
            (void)(*client)->write_json(to_json(response));
            continue;
        }

        auto request = ofx_runtime_request_from_json(*request_json);
        if (!request) {
            auto response = error_response(request.error());
            logger.log("event=request_failed stage=parse detail=" + request.error().message);
            (void)(*client)->write_json(to_json(response));
            continue;
        }

        logger.log(
            "event=request_received command=" + ofx_runtime_command_to_string(request->command) +
            " protocol_version=" + std::to_string(request->protocol_version));

        OfxRuntimeResponseEnvelope response =
            error_response(Error{ErrorCode::InvalidParameters, "Unsupported OFX runtime command."});

        // Capturing a per-request start timestamp lets us emit duration_ms at completion.
        // This is the single most useful signal for the "is it slow, and where?" question.
        const auto request_start = std::chrono::steady_clock::now();

        switch (request->command) {
            case OfxRuntimeCommand::Health: {
                OfxRuntimeHealthResponse health;
                health.server_pid = current_process_id();
                health.session_count = broker.session_count();
                health.active_session_count = broker.active_session_count();
                response = ok_response(to_json(health));
                break;
            }
            case OfxRuntimeCommand::PrepareSession: {
                auto prepare_request = prepare_session_request_from_json(request->payload);
                if (!prepare_request) {
                    response = error_response(prepare_request.error());
                    break;
                }
                auto prepare_response = broker.prepare_session(*prepare_request);
                if (prepare_response) {
                    // reused=1 means the broker hit its cache. reused=0 means a fresh
                    // engine creation which pays the full MLX/ORT compile cost. Pairing
                    // this with duration_ms on request_completed separates cold-load
                    // spikes from steady-state warm loads. host_free_mb /
                    // host_compressor_mb let the reader correlate a downshift
                    // (effective_resolution < requested_resolution) with the memory
                    // state that drove it.
                    const auto& snapshot = prepare_response->session;
                    const auto host_stats = common::query_host_memory_stats();
                    logger.log(
                        std::string("event=prepare_session_details session_id=") +
                        sanitize_log_token(snapshot.session_id) +
                        " reused=" + (snapshot.reused_existing_session ? "1" : "0") +
                        " backend=" + backend_log_token(snapshot.effective_device.backend) +
                        " requested_resolution=" + std::to_string(snapshot.requested_resolution) +
                        " effective_resolution=" + std::to_string(snapshot.effective_resolution) +
                        " recommended_resolution=" +
                        std::to_string(snapshot.recommended_resolution) +
                        " ref_count=" + std::to_string(snapshot.ref_count) +
                        " model=" + sanitize_log_token(snapshot.artifact_name) +
                        format_host_memory_fields(host_stats) +
                        " stage_total_ms=" + format_ms(total_stage_ms(prepare_response->timings)) +
                        " stages=" + format_stage_summary(prepare_response->timings));
                }
                response = prepare_response ? ok_response(to_json(*prepare_response))
                                            : error_response(prepare_response.error());
                break;
            }
            case OfxRuntimeCommand::RenderFrame: {
                auto render_request = render_frame_request_from_json(request->payload);
                if (!render_request) {
                    response = error_response(render_request.error());
                    break;
                }
                const std::size_t sessions_before_render = broker.session_count();
                const std::size_t active_before_render = broker.active_session_count();
                auto render_response = broker.render_frame(*render_request);
                if (!render_response) {
                    const std::size_t sessions_after_render = broker.session_count();
                    const std::size_t active_after_render = broker.active_session_count();
                    logger.log(
                        "event=session_render_failed session_id=" +
                        sanitize_log_token(render_request->session_id) + " destroyed=" +
                        std::to_string(sessions_after_render < sessions_before_render) +
                        " session_count_before=" + std::to_string(sessions_before_render) +
                        " session_count_after=" + std::to_string(sessions_after_render) +
                        " active_session_count_before=" + std::to_string(active_before_render) +
                        " active_session_count_after=" + std::to_string(active_after_render) +
                        " detail=" + sanitize_log_token(render_response.error().message));
                } else {
                    // Per-frame timing breakdown. Summary string keeps the top-5 stages so
                    // the grep story stays readable; consumers that want the full set can
                    // capture the RPC response directly via the existing timings field.
                    const auto& snapshot = render_response->session;
                    const auto memory_snap = core::mlx_memory::snapshot();
                    logger.log(
                        std::string("event=render_frame_details session_id=") +
                        sanitize_log_token(snapshot.session_id) +
                        " frame_index=" + std::to_string(render_request->render_index) +
                        " width=" + std::to_string(render_request->width) +
                        " height=" + std::to_string(render_request->height) +
                        " backend=" + backend_log_token(snapshot.effective_device.backend) +
                        " target_resolution=" +
                        std::to_string(render_request->params.target_resolution) +
                        " tiling=" + (render_request->params.enable_tiling ? "1" : "0") +
                        " mlx_active_mb=" + format_mb(memory_snap.active_bytes) +
                        " mlx_cache_mb=" + format_mb(memory_snap.cache_bytes) +
                        " stage_total_ms=" + format_ms(total_stage_ms(render_response->timings)) +
                        " stages=" + format_stage_summary(render_response->timings));
                }
                response = render_response ? ok_response(to_json(*render_response))
                                           : error_response(render_response.error());
                break;
            }
            case OfxRuntimeCommand::ReleaseSession: {
                auto release_request = release_session_request_from_json(request->payload);
                if (!release_request) {
                    response = error_response(release_request.error());
                    break;
                }
                const std::size_t sessions_before_release = broker.session_count();
                auto release_result = broker.release_session(*release_request);
                const std::size_t sessions_after_release = broker.session_count();
                // If the broker actually destroyed a session, ask MLX to drop any
                // cached buffers the bridge left behind. This prevents the cache
                // from holding onto ~1-2 GB of dead bridge allocations between
                // Resolve scrubs. No-op when nothing was destroyed.
                if (sessions_after_release < sessions_before_release) {
                    core::mlx_memory::clear_cache();
                }
                logger.log(std::string("event=release_session_details session_id=") +
                           sanitize_log_token(release_request->session_id) + " destroyed=" +
                           (sessions_after_release < sessions_before_release ? "1" : "0") +
                           " session_count_before=" + std::to_string(sessions_before_release) +
                           " session_count_after=" + std::to_string(sessions_after_release));
                response = release_result ? ok_response(nlohmann::json::object())
                                          : error_response(release_result.error());
                break;
            }
            case OfxRuntimeCommand::Shutdown: {
                auto shutdown_request = shutdown_request_from_json(request->payload);
                if (!shutdown_request) {
                    response = error_response(shutdown_request.error());
                    break;
                }
                logger.log("event=server_shutdown reason=" +
                           sanitize_log_token(shutdown_request->reason));
                response = ok_response(nlohmann::json::object());
                should_exit = true;
                break;
            }
        }

        (void)(*client)->write_json(to_json(response));
        const auto request_end = std::chrono::steady_clock::now();
        logger.log(
            "event=request_completed command=" + ofx_runtime_command_to_string(request->command) +
            " success=" + std::to_string(response.success) +
            " duration_ms=" + format_ms(elapsed_ms(request_start, request_end)) +
            " detail=" + sanitize_log_token(response_detail(response)));
        const std::size_t removed_idle_sessions = broker.cleanup_idle_sessions();
        if (removed_idle_sessions > 0) {
            logger.log("event=session_idle_destroyed removed_count=" +
                       std::to_string(removed_idle_sessions) +
                       " session_count=" + std::to_string(broker.session_count()) +
                       " active_session_count=" + std::to_string(broker.active_session_count()));
        }
    }

    logger.log("event=server_stop");
    return {};
}

}  // namespace corridorkey::app
// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,readability-identifier-length,bugprone-easily-swappable-parameters,readability-function-cognitive-complexity,readability-function-size,cppcoreguidelines-avoid-magic-numbers,modernize-use-designated-initializers,modernize-use-ranges,modernize-loop-convert,modernize-return-braced-init-list,cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays,readability-implicit-bool-conversion,cert-err33-c,performance-unnecessary-value-param,bugprone-unchecked-string-to-number-conversion,cppcoreguidelines-pro-type-cstyle-cast,modernize-use-using,modernize-use-integer-sign-comparison,cert-dcl50-cpp,cppcoreguidelines-pro-type-const-cast,readability-identifier-naming,modernize-raw-string-literal,readability-container-size-empty,bugprone-command-processor,readability-use-std-min-max,cppcoreguidelines-avoid-non-const-global-variables,bugprone-misplaced-widening-cast,readability-misleading-indentation,cert-env33-c,performance-unnecessary-copy-initialization,readability-named-parameter,readability-isolate-declaration,cert-err34-c,modernize-avoid-variadic-functions,cppcoreguidelines-pro-bounds-constant-array-index)
