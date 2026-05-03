#include "ofx_runtime_client.hpp"

#include <cerrno>
#include <chrono>
#include <thread>
#include <vector>

#include "common/runtime_paths.hpp"
#include "common/shared_memory_transport.hpp"
#include "ofx_logging.hpp"

#if defined(__APPLE__)
#include <pthread.h>
#include <pthread/spawn.h>
#include <signal.h>
#include <spawn.h>
#include <sys/qos.h>
extern char** environ;
#elif defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <signal.h>
#include <spawn.h>
extern char** environ;
#endif

namespace corridorkey::ofx {

namespace {

bool same_device_info(const DeviceInfo& lhs, const DeviceInfo& rhs) {
    return lhs.name == rhs.name && lhs.available_memory_mb == rhs.available_memory_mb &&
           lhs.backend == rhs.backend && lhs.device_index == rhs.device_index;
}

bool same_engine_options(const EngineCreateOptions& lhs, const EngineCreateOptions& rhs) {
    return lhs.allow_cpu_fallback == rhs.allow_cpu_fallback &&
           lhs.disable_cpu_ep_fallback == rhs.disable_cpu_ep_fallback;
}

bool same_prepare_request(const app::OfxRuntimePrepareSessionRequest& lhs,
                          const app::OfxRuntimePrepareSessionRequest& rhs) {
    return lhs.client_instance_id == rhs.client_instance_id && lhs.model_path == rhs.model_path &&
           lhs.artifact_name == rhs.artifact_name &&
           same_device_info(lhs.requested_device, rhs.requested_device) &&
           same_engine_options(lhs.engine_options, rhs.engine_options) &&
           lhs.requested_quality_mode == rhs.requested_quality_mode &&
           lhs.requested_resolution == rhs.requested_resolution &&
           lhs.effective_resolution == rhs.effective_resolution;
}

app::OfxRuntimeSessionSnapshot with_prepare_request_metadata(
    app::OfxRuntimeSessionSnapshot snapshot, const app::OfxRuntimePrepareSessionRequest& request) {
    snapshot.model_path = request.model_path;
    snapshot.artifact_name = request.artifact_name;
    snapshot.requested_device = request.requested_device;
    snapshot.requested_quality_mode = request.requested_quality_mode;
    snapshot.requested_resolution = request.requested_resolution;
    snapshot.effective_resolution = request.effective_resolution;
    return snapshot;
}

Result<nlohmann::json> unwrap_response(const nlohmann::json& json) {
    auto envelope = app::ofx_runtime_response_from_json(json);
    if (!envelope) {
        return Unexpected<Error>(envelope.error());
    }
    if (!envelope->success) {
        return Unexpected<Error>(Error{ErrorCode::InferenceFailed, envelope->error});
    }
    return envelope->payload;
}

bool is_session_missing_error(const Error& error) {
    return error.code == ErrorCode::InferenceFailed &&
           error.message.find("Runtime session is not prepared") != std::string::npos;
}

bool is_transport_error(const Error& error) {
    return error.code == ErrorCode::IoError;
}

bool is_timeout_error(const Error& error) {
    if (error.code != ErrorCode::IoError) {
        return false;
    }
    return error.message.find("timed out") != std::string::npos ||
           error.message.find("Timed out") != std::string::npos;
}

bool is_protocol_mismatch_error(const Error& error) {
    return error.code == ErrorCode::InvalidParameters &&
           error.message.find("Unsupported OFX runtime protocol version") != std::string::npos;
}

bool is_restartable_server_error(const Error& error) {
    return is_transport_error(error) || is_protocol_mismatch_error(error);
}

Result<void> terminate_server_process(int server_pid) {
    if (server_pid <= 0) {
        return {};
    }

#if defined(_WIN32)
    HANDLE process =
        OpenProcess(SYNCHRONIZE | PROCESS_TERMINATE, FALSE, static_cast<DWORD>(server_pid));
    if (process == nullptr) {
        return {};
    }

    if (!TerminateProcess(process, 0)) {
        CloseHandle(process);
        return Unexpected<Error>(
            Error{ErrorCode::IoError, "Failed to terminate the stale OFX runtime server."});
    }

    WaitForSingleObject(process, 5000);
    CloseHandle(process);
    return {};
#else
    if (kill(server_pid, SIGTERM) != 0 && errno != ESRCH) {
        return Unexpected<Error>(
            Error{ErrorCode::IoError, "Failed to terminate the stale OFX runtime server."});
    }
    return {};
#endif
}

// Probe whether a previously-spawned sidecar PID is still alive.
//
// Why this exists: when a Health probe times out during sidecar startup we
// otherwise cannot tell apart "process is alive but slow to bind" from
// "process exited before binding the port". Issue #56 produced exactly the
// second case (Nuke 17.0v2 host) and the client polled a dead PID for the
// full launch_timeout_ms before surfacing a generic "Timed out" string.
// Returning false here lets the caller short-circuit with an actionable
// "process exited during startup" error.
//
// Failure to open the PID handle is treated as "exited" so the caller
// fails fast. The conservative direction is well-defined: if the sidecar
// is somehow alive but the handle cannot be opened, the next Health poll
// will succeed and recover; if the sidecar is dead, we save the user the
// remaining timeout.
bool is_process_alive(int server_pid) {
    if (server_pid <= 0) {
        return false;
    }

#if defined(_WIN32)
    HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, static_cast<DWORD>(server_pid));
    if (process == nullptr) {
        return false;
    }
    const DWORD wait_result = WaitForSingleObject(process, 0);
    CloseHandle(process);
    return wait_result == WAIT_TIMEOUT;
#else
    if (kill(server_pid, 0) == 0) {
        return true;
    }
    return errno != ESRCH;
#endif
}

// Compose a timeout / early-exit error message that names every artefact
// the user needs to diagnose the failure: the resolved loopback port, the
// configured timeout, the server log path, and the server binary path.
// help/TROUBLESHOOTING.md "Logs and Bug Report Guidance" already directs
// users at the LOCALAPPDATA log directory; embedding the exact filename
// here turns the failure dialog into a one-step pointer.
std::string compose_launch_failure_message(const std::string& reason,
                                           const common::LocalJsonEndpoint& endpoint,
                                           int timeout_ms,
                                           const std::filesystem::path& server_binary) {
    std::string message = reason;
    message += " (endpoint=";
    message += endpoint.host;
    message += ":";
    message += std::to_string(endpoint.port);
    message += ", timeout=";
    message += std::to_string(timeout_ms);
    message += "ms, server_log=";
    message += common::ofx_runtime_server_log_path().string();
    message += ", server_binary=";
    message += server_binary.string();
    message += ").";
    return message;
}

void replay_stage_timings(const std::vector<StageTiming>& timings, StageTimingCallback on_stage) {
    if (!on_stage) {
        return;
    }
    for (const auto& timing : timings) {
        on_stage(timing);
    }
}

}  // namespace

std::filesystem::path resolve_ofx_runtime_server_binary(
    const std::filesystem::path& plugin_module_path) {
    if (auto override_path = common::environment_variable_copy("CORRIDORKEY_OFX_RUNTIME_SERVER");
        override_path.has_value()) {
        return std::filesystem::path(*override_path);
    }

#if defined(_WIN32)
    auto win64_dir = plugin_module_path.parent_path();
    return win64_dir / "corridorkey_ofx_runtime_server.exe";
#elif defined(__linux__)
    // Linux OFX bundle layout places the plugin binary and the CLI side by
    // side under Contents/Linux-x86_64/, per the OpenFX portable bundle
    // convention. The CLI exposes an `ofx-runtime-server` subcommand so we
    // do not ship a separate server binary.
    auto linux_dir = plugin_module_path.parent_path();
    return linux_dir / "corridorkey";
#else
    auto bundle_root = plugin_module_path.parent_path().parent_path();
    return bundle_root / "Resources" / "bin" / "corridorkey";
#endif
}

Result<std::unique_ptr<OfxRuntimeClient>> OfxRuntimeClient::create(
    OfxRuntimeClientOptions options) {
    auto client = std::unique_ptr<OfxRuntimeClient>(new OfxRuntimeClient(std::move(options)));
    return client;
}

OfxRuntimeClient::OfxRuntimeClient(OfxRuntimeClientOptions options)
    : m_options(std::move(options)) {}

OfxRuntimeClient::~OfxRuntimeClient() {
    auto release_result = release_session();
    if (!release_result) {
        log_message("ofx_runtime_client",
                    "release_session_failed detail=" + release_result.error().message);
    }
}

Result<app::OfxRuntimeHealthResponse> OfxRuntimeClient::health() {
    auto response = send_command(app::OfxRuntimeCommand::Health, nlohmann::json::object());
    if (!response) {
        return Unexpected<Error>(response.error());
    }
    auto parsed = app::health_response_from_json(*response);
    if (!parsed) {
        return Unexpected<Error>(parsed.error());
    }
    update_server_health(*parsed);
    return parsed;
}

Result<app::OfxRuntimePrepareSessionResponse> OfxRuntimeClient::prepare_session(
    const app::OfxRuntimePrepareSessionRequest& request, StageTimingCallback on_stage) {
    if (!m_session.session_id.empty() && m_last_prepare_request.has_value() &&
        same_prepare_request(*m_last_prepare_request, request)) {
        return app::OfxRuntimePrepareSessionResponse{
            with_prepare_request_metadata(m_session, request), {}};
    }

    if (!m_session.session_id.empty()) {
        auto release_result = release_session();
        if (!release_result && !is_transport_error(release_result.error())) {
            return Unexpected<Error>(release_result.error());
        }
    }

    // Mirror the client's RPC-level prepare timeout into the server-side
    // request so the server can bound its own prewarm phase. Without this
    // the server might spend the entire timeout on MLX JIT compile and
    // leave no budget for the network round trip. We deliberately send a
    // slightly shorter value (90% of the client RPC budget) so the server
    // has time to return an error or timeout-status response before the
    // client transport gives up.
    app::OfxRuntimePrepareSessionRequest outgoing = request;
    if (outgoing.prepare_timeout_ms <= 0) {
        outgoing.prepare_timeout_ms = (m_options.prepare_timeout_ms * 9) / 10;
    }
    auto payload = app::to_json(outgoing);
    auto ensure_result = ensure_server_running();
    if (!ensure_result) {
        return Unexpected<Error>(ensure_result.error());
    }
    auto response = send_command_without_launch(app::OfxRuntimeCommand::PrepareSession, payload,
                                                m_options.prepare_timeout_ms);
    if (!response) {
        return Unexpected<Error>(response.error());
    }

    auto parsed = app::prepare_session_response_from_json(*response);
    if (!parsed) {
        return Unexpected<Error>(parsed.error());
    }

    parsed->session = with_prepare_request_metadata(parsed->session, request);
    update_session_snapshot(parsed->session);
    m_last_prepare_request = request;
    replay_stage_timings(parsed->timings, on_stage);
    return parsed;
}

Result<FrameResult> OfxRuntimeClient::process_frame(const Image& rgb, const Image& alpha_hint,
                                                    const InferenceParams& params,
                                                    std::uint64_t render_index,
                                                    StageTimingCallback on_stage) {
    if (m_session.session_id.empty()) {
        return Unexpected<Error>(
            Error{ErrorCode::InvalidParameters, "OFX runtime session is not prepared."});
    }

    const auto transport_path = common::next_ofx_shared_frame_path();
    auto render_result = [&]() -> Result<FrameResult> {
        auto transport =
            common::SharedFrameTransport::create(transport_path, rgb.width, rgb.height);
        if (!transport) {
            return Unexpected<Error>(transport.error());
        }

        std::copy(rgb.data.begin(), rgb.data.end(), transport->rgb_view().data.begin());
        std::copy(alpha_hint.data.begin(), alpha_hint.data.end(),
                  transport->hint_view().data.begin());

        app::OfxRuntimeRenderFrameRequest request;
        request.session_id = m_session.session_id;
        request.shared_frame_path = transport_path;
        request.width = rgb.width;
        request.height = rgb.height;
        request.params = params;
        request.render_index = render_index;

        auto send_render_request = [&]() {
            return send_command(app::OfxRuntimeCommand::RenderFrame, app::to_json(request));
        };

        auto response = send_render_request();
        if (!response && is_timeout_error(response.error())) {
            log_message("ofx_runtime_client",
                        "event=render_timeout reason=" + response.error().message);
            auto restart_result = restart_server(response.error().message);
            if (!restart_result) {
                return Unexpected<Error>(restart_result.error());
            }
            auto recover_result = recover_runtime_session(on_stage);
            if (!recover_result) {
                return Unexpected<Error>(recover_result.error());
            }
            request.session_id = m_session.session_id;
            response = send_render_request();
        }
        if (!response &&
            (is_transport_error(response.error()) || is_session_missing_error(response.error()))) {
            log_message("ofx_runtime_client",
                        "event=render_frame_recover reason=" + response.error().message);
            auto recover_result = recover_runtime_session(on_stage);
            if (!recover_result) {
                return Unexpected<Error>(recover_result.error());
            }
            request.session_id = m_session.session_id;
            response = send_render_request();
        }
        if (!response) {
            if (response.error().code == ErrorCode::InferenceFailed) {
                invalidate_session("event=render_frame_invalidated detail=" +
                                   response.error().message);
            }
            return Unexpected<Error>(response.error());
        }

        auto parsed = app::render_frame_response_from_json(*response);
        if (!parsed) {
            return Unexpected<Error>(parsed.error());
        }

        update_session_snapshot(parsed->session);
        replay_stage_timings(parsed->timings, on_stage);

        FrameResult result;
        result.alpha = ImageBuffer(rgb.width, rgb.height, 1);
        std::copy(transport->alpha_view().data.begin(), transport->alpha_view().data.end(),
                  result.alpha.view().data.begin());
        if (!params.output_alpha_only) {
            result.foreground = ImageBuffer(rgb.width, rgb.height, 3);
            std::copy(transport->foreground_view().data.begin(),
                      transport->foreground_view().data.end(),
                      result.foreground.view().data.begin());
        }
        return result;
    }();

    std::error_code cleanup_error;
    std::filesystem::remove(transport_path, cleanup_error);
    if (!render_result) {
        if (cleanup_error) {
            log_message("ofx_runtime_client",
                        "event=shared_frame_cleanup_failed path=" + transport_path.string() +
                            " detail=" + cleanup_error.message());
        }
        return Unexpected<Error>(render_result.error());
    }
    if (cleanup_error) {
        log_message("ofx_runtime_client",
                    "event=shared_frame_cleanup_failed path=" + transport_path.string() +
                        " detail=" + cleanup_error.message());
    }
    return render_result;
}

Result<void> OfxRuntimeClient::release_session() {
    if (m_session.session_id.empty()) {
        m_last_prepare_request = std::nullopt;
        return {};
    }

    app::OfxRuntimeReleaseSessionRequest request;
    request.session_id = m_session.session_id;
    auto response =
        send_command_without_launch(app::OfxRuntimeCommand::ReleaseSession, app::to_json(request));
    m_session = {};
    m_last_prepare_request = std::nullopt;
    if (!response && !is_transport_error(response.error())) {
        return Unexpected<Error>(response.error());
    }
    return {};
}

DeviceInfo OfxRuntimeClient::current_device() const {
    return m_session.effective_device;
}

std::optional<BackendFallbackInfo> OfxRuntimeClient::backend_fallback() const {
    return m_session.backend_fallback;
}

bool OfxRuntimeClient::has_session() const {
    return !m_session.session_id.empty();
}

std::uint64_t OfxRuntimeClient::session_ref_count() const {
    return m_session.ref_count;
}

void OfxRuntimeClient::set_request_timeout_ms(int ms) {
    m_options.request_timeout_ms = ms;
}

void OfxRuntimeClient::set_prepare_timeout_ms(int ms) {
    m_options.prepare_timeout_ms = ms;
}

Result<void> OfxRuntimeClient::ensure_server_running() {
    const auto wait_for_server_ready = [&]() -> Result<void> {
        const auto start_time = std::chrono::steady_clock::now();
        while (std::chrono::steady_clock::now() - start_time <
               std::chrono::milliseconds(m_options.launch_timeout_ms)) {
            auto poll = send_command_without_launch(app::OfxRuntimeCommand::Health,
                                                    nlohmann::json::object());
            if (poll) {
                auto health = app::health_response_from_json(*poll);
                if (health) {
                    update_server_health(*health);
                    log_message("ofx_runtime_client",
                                "event=server_ready pid=" + std::to_string(m_server_pid));
                    return {};
                }
                return Unexpected<Error>(health.error());
            }
            // The sidecar PID was captured by launch_server() before this
            // poll loop runs. If it has already exited, polling Health for
            // the remaining timeout is wasted time and produces a generic
            // "Timed out" error that does not point at the real cause.
            if (m_server_pid > 0 && !is_process_alive(m_server_pid)) {
                const std::string reason = "OFX runtime server process (pid=" +
                                           std::to_string(m_server_pid) +
                                           ") exited during startup";
                log_message("ofx_runtime_client", "event=server_exited_during_startup pid=" +
                                                      std::to_string(m_server_pid));
                return Unexpected<Error>(Error{
                    ErrorCode::IoError,
                    compose_launch_failure_message(reason, m_options.endpoint,
                                                   m_options.launch_timeout_ms,
                                                   m_options.server_binary),
                });
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(150));
        }

        return Unexpected<Error>(Error{
            ErrorCode::IoError,
            compose_launch_failure_message("Timed out waiting for the OFX runtime server to start",
                                           m_options.endpoint, m_options.launch_timeout_ms,
                                           m_options.server_binary),
        });
    };

    auto health_response =
        send_command_without_launch(app::OfxRuntimeCommand::Health, nlohmann::json::object());
    if (health_response) {
        auto health = app::health_response_from_json(*health_response);
        if (!health) {
            return Unexpected<Error>(health.error());
        }
        update_server_health(*health);
        return {};
    }

    if (is_protocol_mismatch_error(health_response.error())) {
        if (m_server_pid > 0) {
            auto restart = restart_server(health_response.error().message);
            if (restart) {
                return restart;
            }
        }
        return Unexpected<Error>(health_response.error());
    }

    if (m_server_pid > 0 && is_restartable_server_error(health_response.error())) {
        auto restart = restart_server(health_response.error().message);
        if (restart) {
            return restart;
        }
    }

    auto launch_result = launch_server();
    if (!launch_result) {
        return Unexpected<Error>(launch_result.error());
    }
    return wait_for_server_ready();
}

Result<nlohmann::json> OfxRuntimeClient::send_command(app::OfxRuntimeCommand command,
                                                      const nlohmann::json& payload) {
    auto ensure_result = ensure_server_running();
    if (!ensure_result) {
        return Unexpected<Error>(ensure_result.error());
    }

    return send_command_without_launch(command, payload);
}

Result<nlohmann::json> OfxRuntimeClient::send_command_without_launch(
    app::OfxRuntimeCommand command, const nlohmann::json& payload) const {
    return send_command_without_launch(command, payload, m_options.request_timeout_ms);
}

Result<nlohmann::json> OfxRuntimeClient::send_command_without_launch(app::OfxRuntimeCommand command,
                                                                     const nlohmann::json& payload,
                                                                     int timeout_ms) const {
    app::OfxRuntimeRequestEnvelope envelope;
    envelope.command = command;
    envelope.payload = payload;

    auto response =
        common::send_json_request(m_options.endpoint, app::to_json(envelope), timeout_ms);
    if (!response) {
        return Unexpected<Error>(response.error());
    }
    return unwrap_response(*response);
}

Result<void> OfxRuntimeClient::recover_runtime_session(StageTimingCallback on_stage) {
    if (!m_last_prepare_request.has_value()) {
        return Unexpected<Error>(
            Error{ErrorCode::IoError, "The OFX runtime session was lost and cannot be recovered."});
    }

    log_message("ofx_runtime_client", "event=recover_session_begin");
    m_session = {};

    auto ensure_result = ensure_server_running();
    if (!ensure_result) {
        return Unexpected<Error>(ensure_result.error());
    }
    // Same reasoning as OfxRuntimeClient::prepare_session(): propagate a
    // shorter server-side prewarm budget so the server cannot consume the
    // full RPC budget inside MLX JIT compile.
    app::OfxRuntimePrepareSessionRequest outgoing = *m_last_prepare_request;
    if (outgoing.prepare_timeout_ms <= 0) {
        outgoing.prepare_timeout_ms = (m_options.prepare_timeout_ms * 9) / 10;
    }
    auto response =
        send_command_without_launch(app::OfxRuntimeCommand::PrepareSession, app::to_json(outgoing),
                                    m_options.prepare_timeout_ms);
    if (!response) {
        return Unexpected<Error>(response.error());
    }

    auto parsed = app::prepare_session_response_from_json(*response);
    if (!parsed) {
        return Unexpected<Error>(parsed.error());
    }

    update_session_snapshot(parsed->session);
    replay_stage_timings(parsed->timings, on_stage);
    log_message("ofx_runtime_client", "event=recover_session_result reused_existing_session=" +
                                          std::to_string(parsed->session.reused_existing_session) +
                                          " session_id=" + parsed->session.session_id);
    return {};
}

Result<void> OfxRuntimeClient::restart_server(const std::string& reason) {
    log_message("ofx_runtime_client", "event=restart_server_begin pid=" +
                                          std::to_string(m_server_pid) + " reason=" + reason);
    auto terminate_result = terminate_server_process(m_server_pid);
    if (!terminate_result) {
        return Unexpected<Error>(terminate_result.error());
    }

    m_server_pid = 0;

    auto launch_result = launch_server();
    if (!launch_result) {
        return Unexpected<Error>(launch_result.error());
    }

    const auto start_time = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - start_time <
           std::chrono::milliseconds(m_options.launch_timeout_ms)) {
        auto poll =
            send_command_without_launch(app::OfxRuntimeCommand::Health, nlohmann::json::object());
        if (poll) {
            auto health = app::health_response_from_json(*poll);
            if (health) {
                update_server_health(*health);
                log_message("ofx_runtime_client",
                            "event=restart_server_result pid=" + std::to_string(m_server_pid));
                return {};
            }
            return Unexpected<Error>(health.error());
        }
        if (m_server_pid > 0 && !is_process_alive(m_server_pid)) {
            const std::string exit_reason = "Restarted OFX runtime server process (pid=" +
                                            std::to_string(m_server_pid) +
                                            ") exited during startup";
            log_message("ofx_runtime_client", "event=restart_server_exited pid=" +
                                                  std::to_string(m_server_pid));
            return Unexpected<Error>(Error{
                ErrorCode::IoError,
                compose_launch_failure_message(exit_reason, m_options.endpoint,
                                               m_options.launch_timeout_ms,
                                               m_options.server_binary),
            });
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
    }

    return Unexpected<Error>(Error{
        ErrorCode::IoError,
        compose_launch_failure_message("Timed out waiting for the restarted OFX runtime server",
                                       m_options.endpoint, m_options.launch_timeout_ms,
                                       m_options.server_binary),
    });
}

Result<void> OfxRuntimeClient::launch_server() {
    if (m_options.server_binary.empty() || !std::filesystem::exists(m_options.server_binary)) {
        return Unexpected<Error>(
            Error{ErrorCode::IoError,
                  "OFX runtime server binary was not found: " + m_options.server_binary.string()});
    }

    log_message("ofx_runtime_client",
                "event=launch_server path=" + m_options.server_binary.string());

#if defined(_WIN32)
    std::wstring command_line = L"\"" + m_options.server_binary.wstring() +
                                L"\" ofx-runtime-server --endpoint-port " +
                                std::to_wstring(m_options.endpoint.port) + L" --idle-timeout-ms " +
                                std::to_wstring(m_options.idle_timeout_ms);

    STARTUPINFOW startup_info{};
    startup_info.cb = sizeof(startup_info);
    startup_info.dwFlags = STARTF_USESHOWWINDOW;
    startup_info.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION process_info{};
    if (!CreateProcessW(nullptr, command_line.data(), nullptr, nullptr, FALSE,
                        DETACHED_PROCESS | CREATE_NO_WINDOW, nullptr, nullptr, &startup_info,
                        &process_info)) {
        return Unexpected<Error>(
            Error{ErrorCode::IoError, "Failed to launch the OFX runtime server process."});
    }

    // Capture the spawned PID before closing the handles so the wait loop
    // in ensure_server_running() / restart_server() can probe whether the
    // child is still alive. Without this, m_server_pid stays 0 until a
    // Health response arrives; in the failure mode behind issue #56 that
    // response never arrives, and the client polls a dead process for the
    // full launch_timeout_ms. The handles themselves do not need to remain
    // open: is_process_alive() reopens by PID via OpenProcess(SYNCHRONIZE).
    m_server_pid = static_cast<int>(process_info.dwProcessId);
    log_message("ofx_runtime_client",
                "event=launch_server_spawned pid=" + std::to_string(m_server_pid));
    CloseHandle(process_info.hThread);
    CloseHandle(process_info.hProcess);
#else
    std::string port = std::to_string(m_options.endpoint.port);
    std::string idle_timeout = std::to_string(m_options.idle_timeout_ms);
    std::vector<char*> argv = {const_cast<char*>(m_options.server_binary.c_str()),
                               const_cast<char*>("ofx-runtime-server"),
                               const_cast<char*>("--endpoint-port"),
                               port.data(),
                               const_cast<char*>("--idle-timeout-ms"),
                               idle_timeout.data(),
                               nullptr};
    pid_t pid = 0;
#if defined(__APPLE__)
    // DaVinci Resolve invokes OFX render actions on UTILITY/BACKGROUND QoS
    // worker threads. macOS clamps posix_spawn child's task QoS to the
    // calling thread's effective QoS and silently clamps any subsequent
    // pthread_set_qos_class_self_np / posix_spawnattr_set_qos_class_np to
    // that ceiling (these APIs can LOWER but not RAISE). On Apple Silicon
    // a low-QoS child has its Metal work preempted by the host's higher-
    // QoS Metal workload, causing 10-70x per-frame slowdowns.
    //
    // To raise the ceiling we use pthread_override_qos_class_start_np,
    // which is the documented override API used by libdispatch to avoid
    // priority inversion. An active override elevates the thread's
    // effective QoS for the duration of the call. The spawnattr + server-
    // side pthread_set_qos_class_self_np then succeed because the ceiling
    // allows it.
    qos_class_t caller_qos = QOS_CLASS_UNSPECIFIED;
    int caller_relative = 0;
    pthread_get_qos_class_np(pthread_self(), &caller_qos, &caller_relative);
    log_message("ofx_runtime_client", std::string("event=launch_server_parent_qos qos=") +
                                          std::to_string(static_cast<int>(caller_qos)));
    pthread_override_t qos_override =
        pthread_override_qos_class_start_np(pthread_self(), QOS_CLASS_USER_INITIATED, 0);

    posix_spawnattr_t spawn_attrs;
    int attr_rc = posix_spawnattr_init(&spawn_attrs);
    posix_spawnattr_t* attrs_ptr = nullptr;
    if (attr_rc == 0) {
        posix_spawnattr_set_qos_class_np(&spawn_attrs, QOS_CLASS_USER_INITIATED);
        attrs_ptr = &spawn_attrs;
    }
    int spawn_rc = posix_spawn(&pid, m_options.server_binary.c_str(), nullptr, attrs_ptr,
                               argv.data(), environ);
    if (attr_rc == 0) {
        posix_spawnattr_destroy(&spawn_attrs);
    }

    if (qos_override != nullptr) {
        pthread_override_qos_class_end_np(qos_override);
    }

    if (spawn_rc != 0) {
        return Unexpected<Error>(
            Error{ErrorCode::IoError, "Failed to launch the OFX runtime server process."});
    }
#else
    if (posix_spawn(&pid, m_options.server_binary.c_str(), nullptr, nullptr, argv.data(),
                    environ) != 0) {
        return Unexpected<Error>(
            Error{ErrorCode::IoError, "Failed to launch the OFX runtime server process."});
    }
#endif
    // Mirrors the Win32 branch above: capture the spawned PID before the
    // wait loop runs so a child that exits before binding the port can be
    // detected and surfaced instead of polling a dead process for the full
    // launch_timeout_ms.
    m_server_pid = static_cast<int>(pid);
    log_message("ofx_runtime_client",
                "event=launch_server_spawned pid=" + std::to_string(m_server_pid));
#endif

    return {};
}

void OfxRuntimeClient::invalidate_session(const std::string& reason) {
    if (!m_session.session_id.empty()) {
        log_message("ofx_runtime_client", reason + " session_id=" + m_session.session_id);
    } else {
        log_message("ofx_runtime_client", reason);
    }
    m_session = {};
}

void OfxRuntimeClient::update_session_snapshot(const app::OfxRuntimeSessionSnapshot& snapshot) {
    m_session = snapshot;
}

void OfxRuntimeClient::update_server_health(const app::OfxRuntimeHealthResponse& health) {
    m_server_pid = health.server_pid;
}

}  // namespace corridorkey::ofx
