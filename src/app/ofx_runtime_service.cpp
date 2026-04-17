#include "ofx_runtime_service.hpp"

#include <algorithm>
#include <chrono>
#include <corridorkey/version.hpp>
#include <cstdio>
#include <fstream>

#include "../common/runtime_paths.hpp"

#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

namespace corridorkey::app {

namespace {

class RuntimeLogger {
   public:
    explicit RuntimeLogger(const std::filesystem::path& path) {
        std::error_code error;
        std::filesystem::create_directories(path.parent_path(), error);
        m_stream.open(path, std::ios::app);
    }

    void log(const std::string& message) {
        if (!m_stream.is_open()) {
            return;
        }
        m_stream << message << '\n';
        m_stream.flush();
    }

   private:
    std::ofstream m_stream;
};

int current_process_id() {
#if defined(_WIN32)
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

// Summarize the top-N stages by total_ms into a compact "name:ms,name:ms" list.
// The log consumer can grep the summary without needing the full per-stage JSON.
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

    const std::size_t limit = std::min(top_n, indices.size());
    std::string summary;
    summary.reserve(64);
    for (std::size_t i = 0; i < limit; ++i) {
        const auto& timing = timings[indices[i]];
        if (i > 0) {
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

}  // namespace

Result<void> OfxRuntimeService::run(const OfxRuntimeServiceOptions& options) {
    RuntimeLogger logger(options.log_path.empty() ? common::ofx_runtime_server_log_path()
                                                  : options.log_path);
    // The version banner lets the log reader know which build emitted the events.
    // display_version carries the optimization checkpoint label (e.g. 0.7.5-2); it
    // collapses to the semantic version when no override is active at build time.
    logger.log(std::string("event=server_start pid=") + std::to_string(current_process_id()) +
               " port=" + std::to_string(options.endpoint.port) +
               " version=" + CORRIDORKEY_VERSION_STRING +
               " display_version=" + CORRIDORKEY_DISPLAY_VERSION_STRING);

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
            (*client)->write_json(to_json(response));
            continue;
        }

        auto request = ofx_runtime_request_from_json(*request_json);
        if (!request) {
            auto response = error_response(request.error());
            logger.log("event=request_failed stage=parse detail=" + request.error().message);
            (*client)->write_json(to_json(response));
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
                    // spikes from steady-state warm loads.
                    const auto& snapshot = prepare_response->session;
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

        (*client)->write_json(to_json(response));
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
