#include "ofx_runtime_service.hpp"

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

}  // namespace

Result<void> OfxRuntimeService::run(const OfxRuntimeServiceOptions& options) {
    RuntimeLogger logger(options.log_path.empty() ? common::ofx_runtime_server_log_path()
                                                  : options.log_path);
    logger.log("event=server_start pid=" + std::to_string(current_process_id()) +
               " port=" + std::to_string(options.endpoint.port));

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
                auto render_response = broker.render_frame(*render_request);
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
                auto release_result = broker.release_session(*release_request);
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
                logger.log("event=server_shutdown reason=" + shutdown_request->reason);
                response = ok_response(nlohmann::json::object());
                should_exit = true;
                break;
            }
        }

        (*client)->write_json(to_json(response));
        logger.log(
            "event=request_completed command=" + ofx_runtime_command_to_string(request->command) +
            " success=" + std::to_string(response.success) +
            " detail=" + response_detail(response));
        broker.cleanup_idle_sessions();
    }

    logger.log("event=server_stop");
    return {};
}

}  // namespace corridorkey::app
