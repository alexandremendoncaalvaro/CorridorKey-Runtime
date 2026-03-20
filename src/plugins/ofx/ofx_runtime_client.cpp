#include "ofx_runtime_client.hpp"

#include <chrono>
#include <thread>
#include <vector>

#include "common/runtime_paths.hpp"
#include "common/shared_memory_transport.hpp"
#include "ofx_logging.hpp"

#if defined(__APPLE__)
#include <spawn.h>
extern char** environ;
#elif defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace corridorkey::ofx {

namespace {

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
    return win64_dir / "corridorkey.exe";
#else
    auto bundle_root = plugin_module_path.parent_path().parent_path();
    return bundle_root / "Resources" / "bin" / "corridorkey";
#endif
}

Result<std::unique_ptr<OfxRuntimeClient>> OfxRuntimeClient::create(OfxRuntimeClientOptions options) {
    auto client = std::unique_ptr<OfxRuntimeClient>(new OfxRuntimeClient(std::move(options)));
    return client;
}

OfxRuntimeClient::OfxRuntimeClient(OfxRuntimeClientOptions options) : m_options(std::move(options)) {}

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
    return app::health_response_from_json(*response);
}

Result<app::OfxRuntimePrepareSessionResponse> OfxRuntimeClient::prepare_session(
    const app::OfxRuntimePrepareSessionRequest& request, StageTimingCallback on_stage) {
    if (m_session.model_path == request.model_path &&
        m_session.requested_device.backend == request.requested_device.backend &&
        m_session.artifact_name == request.artifact_name && !m_session.session_id.empty()) {
        return app::OfxRuntimePrepareSessionResponse{m_session, {}};
    }

    if (!m_session.session_id.empty()) {
        auto release_result = release_session();
        if (!release_result && !is_transport_error(release_result.error())) {
            return Unexpected<Error>(release_result.error());
        }
    }

    auto payload = app::to_json(request);
    auto response = send_command(app::OfxRuntimeCommand::PrepareSession, payload);
    if (!response) {
        return Unexpected<Error>(response.error());
    }

    auto parsed = app::prepare_session_response_from_json(*response);
    if (!parsed) {
        return Unexpected<Error>(parsed.error());
    }

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
        result.foreground = ImageBuffer(rgb.width, rgb.height, 3);
        std::copy(transport->alpha_view().data.begin(), transport->alpha_view().data.end(),
                  result.alpha.view().data.begin());
        std::copy(transport->foreground_view().data.begin(),
                  transport->foreground_view().data.end(),
                  result.foreground.view().data.begin());
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

Result<void> OfxRuntimeClient::ensure_server_running() {
    auto health_response =
        send_command_without_launch(app::OfxRuntimeCommand::Health, nlohmann::json::object());
    if (health_response) {
        return {};
    }

    auto launch_result = launch_server();
    if (!launch_result) {
        return Unexpected<Error>(launch_result.error());
    }

    const auto start_time = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - start_time <
           std::chrono::milliseconds(m_options.launch_timeout_ms)) {
        auto poll = common::send_json_request(
            m_options.endpoint,
            app::to_json(app::OfxRuntimeRequestEnvelope{app::kOfxRuntimeProtocolVersion,
                                                       app::OfxRuntimeCommand::Health,
                                                       nlohmann::json::object()}),
            1000);
        if (poll) {
            auto payload = unwrap_response(*poll);
            if (payload) {
                log_message("ofx_runtime_client", "event=server_ready");
                return {};
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
    }

    return Unexpected<Error>(
        Error{ErrorCode::IoError, "Timed out waiting for the OFX runtime server to start."});
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

    app::OfxRuntimeRequestEnvelope envelope;
    envelope.command = command;
    envelope.payload = payload;

    auto response = common::send_json_request(m_options.endpoint, app::to_json(envelope),
                                              m_options.request_timeout_ms);
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

    auto response = send_command(app::OfxRuntimeCommand::PrepareSession,
                                 app::to_json(*m_last_prepare_request));
    if (!response) {
        return Unexpected<Error>(response.error());
    }

    auto parsed = app::prepare_session_response_from_json(*response);
    if (!parsed) {
        return Unexpected<Error>(parsed.error());
    }

    update_session_snapshot(parsed->session);
    replay_stage_timings(parsed->timings, on_stage);
    log_message("ofx_runtime_client",
                "event=recover_session_result reused_existing_session=" +
                    std::to_string(parsed->session.reused_existing_session) + " session_id=" +
                    parsed->session.session_id);
    return {};
}

Result<void> OfxRuntimeClient::launch_server() {
    if (m_options.server_binary.empty() || !std::filesystem::exists(m_options.server_binary)) {
        return Unexpected<Error>(
            Error{ErrorCode::IoError, "OFX runtime server binary was not found: " +
                                          m_options.server_binary.string()});
    }

    log_message("ofx_runtime_client",
                "event=launch_server path=" + m_options.server_binary.string());

#if defined(_WIN32)
    std::wstring command_line =
        L"\"" + m_options.server_binary.wstring() + L"\" ofx-runtime-server --endpoint-port " +
        std::to_wstring(m_options.endpoint.port) + L" --idle-timeout-ms " +
        std::to_wstring(m_options.idle_timeout_ms);

    STARTUPINFOW startup_info {};
    startup_info.cb = sizeof(startup_info);
    PROCESS_INFORMATION process_info {};
    if (!CreateProcessW(nullptr, command_line.data(), nullptr, nullptr, FALSE,
                        DETACHED_PROCESS | CREATE_NO_WINDOW, nullptr, nullptr, &startup_info,
                        &process_info)) {
        return Unexpected<Error>(
            Error{ErrorCode::IoError, "Failed to launch the OFX runtime server process."});
    }

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
    if (posix_spawn(&pid, m_options.server_binary.c_str(), nullptr, nullptr, argv.data(), environ) !=
        0) {
        return Unexpected<Error>(
            Error{ErrorCode::IoError, "Failed to launch the OFX runtime server process."});
    }
#endif

    return {};
}

void OfxRuntimeClient::update_session_snapshot(const app::OfxRuntimeSessionSnapshot& snapshot) {
    m_session = snapshot;
}

}  // namespace corridorkey::ofx
