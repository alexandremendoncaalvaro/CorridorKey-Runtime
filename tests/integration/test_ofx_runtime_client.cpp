#include <atomic>
#include <catch2/catch_all.hpp>
#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <thread>

#include "app/ofx_runtime_protocol.hpp"
#include "common/local_ipc.hpp"
#include "common/shared_memory_transport.hpp"
#include "plugins/ofx/ofx_runtime_client.hpp"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

using namespace corridorkey;
using namespace corridorkey::app;
using namespace corridorkey::common;
using namespace corridorkey::ofx;

namespace {

std::uint16_t reserve_local_port() {
#if defined(_WIN32)
    WSADATA data;
    REQUIRE(WSAStartup(MAKEWORD(2, 2), &data) == 0);
    SOCKET socket_handle = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    REQUIRE(socket_handle != INVALID_SOCKET);
#else
    int socket_handle = socket(AF_INET, SOCK_STREAM, 0);
    REQUIRE(socket_handle >= 0);
#endif

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(0);

    REQUIRE(bind(socket_handle, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0);

    sockaddr_in bound_address{};
#if defined(_WIN32)
    int length = sizeof(bound_address);
    REQUIRE(getsockname(socket_handle, reinterpret_cast<sockaddr*>(&bound_address), &length) == 0);
    closesocket(socket_handle);
#else
    socklen_t length = sizeof(bound_address);
    REQUIRE(getsockname(socket_handle, reinterpret_cast<sockaddr*>(&bound_address), &length) == 0);
    close(socket_handle);
#endif

    return ntohs(bound_address.sin_port);
}

OfxRuntimeResponseEnvelope ok_response(const nlohmann::json& payload) {
    OfxRuntimeResponseEnvelope response;
    response.success = true;
    response.payload = payload;
    return response;
}

OfxRuntimeResponseEnvelope error_response(const std::string& message) {
    OfxRuntimeResponseEnvelope response;
    response.success = false;
    response.error = message;
    response.payload = nlohmann::json::object();
    return response;
}

void fill_transport_result(SharedFrameTransport& transport, float alpha_value, float fg_value) {
    auto alpha = transport.alpha_view();
    auto foreground = transport.foreground_view();
    std::fill(alpha.data.begin(), alpha.data.end(), alpha_value);
    std::fill(foreground.data.begin(), foreground.data.end(), fg_value);
}

}  // namespace

TEST_CASE("ofx runtime client recovers when the runtime loses the current session",
          "[integration][ofx][runtime][regression]") {
    const auto port = reserve_local_port();
    const LocalJsonEndpoint endpoint{"127.0.0.1", port};

    std::atomic<int> prepare_count = 0;
    std::atomic<int> render_count = 0;
    std::atomic<int> release_count = 0;
    std::optional<Error> server_error;
    std::atomic<bool> stop_server = false;

    std::thread server_thread([&]() {
        auto server = LocalJsonServer::listen(endpoint);
        if (!server) {
            server_error = server.error();
            return;
        }

        while (!stop_server.load()) {
            auto client = server->accept_one(500);
            if (!client) {
                server_error = client.error();
                return;
            }
            if (!client->has_value()) {
                continue;
            }

            auto request_json = (*client)->read_json(2000);
            if (!request_json) {
                server_error = request_json.error();
                return;
            }

            auto request = ofx_runtime_request_from_json(*request_json);
            if (!request) {
                server_error = request.error();
                return;
            }

            switch (request->command) {
                case OfxRuntimeCommand::Health: {
                    OfxRuntimeHealthResponse health;
                    health.server_pid = 4242;
                    health.session_count = 1;
                    health.active_session_count = 1;
                    (*client)->write_json(to_json(ok_response(to_json(health))));
                    break;
                }
                case OfxRuntimeCommand::PrepareSession: {
                    auto parsed = prepare_session_request_from_json(request->payload);
                    if (!parsed) {
                        server_error = parsed.error();
                        return;
                    }

                    const int current_prepare = ++prepare_count;
                    OfxRuntimeSessionSnapshot snapshot;
                    snapshot.session_id =
                        current_prepare == 1 ? "session-initial" : "session-recovered";
                    snapshot.model_path = parsed->model_path;
                    snapshot.artifact_name = parsed->artifact_name;
                    snapshot.requested_device = parsed->requested_device;
                    snapshot.effective_device = parsed->requested_device;
                    snapshot.requested_quality_mode = parsed->requested_quality_mode;
                    snapshot.requested_resolution = parsed->requested_resolution;
                    snapshot.effective_resolution = parsed->effective_resolution;
                    snapshot.recommended_resolution = parsed->effective_resolution;
                    snapshot.ref_count = 1;
                    snapshot.reused_existing_session = current_prepare > 1;

                    OfxRuntimePrepareSessionResponse response;
                    response.session = snapshot;
                    (*client)->write_json(to_json(ok_response(to_json(response))));
                    break;
                }
                case OfxRuntimeCommand::RenderFrame: {
                    auto parsed = render_frame_request_from_json(request->payload);
                    if (!parsed) {
                        server_error = parsed.error();
                        return;
                    }

                    const int current_render = ++render_count;
                    if (current_render == 1) {
                        (*client)->write_json(to_json(
                            error_response("Runtime session is not prepared: lost-session")));
                        break;
                    }

                    auto transport = SharedFrameTransport::open(parsed->shared_frame_path);
                    if (!transport) {
                        server_error = transport.error();
                        return;
                    }
                    fill_transport_result(*transport, 0.75F, 0.25F);

                    OfxRuntimeSessionSnapshot snapshot;
                    snapshot.session_id = "session-recovered";
                    snapshot.requested_device = DeviceInfo{"RTX Test", 16384, Backend::TensorRT};
                    snapshot.effective_device = snapshot.requested_device;
                    snapshot.requested_resolution = parsed->params.target_resolution;
                    snapshot.effective_resolution = parsed->params.target_resolution;
                    snapshot.recommended_resolution = parsed->params.target_resolution;
                    snapshot.ref_count = 1;

                    OfxRuntimeRenderFrameResponse response;
                    response.session = snapshot;
                    (*client)->write_json(to_json(ok_response(to_json(response))));
                    break;
                }
                case OfxRuntimeCommand::ReleaseSession: {
                    ++release_count;
                    (*client)->write_json(to_json(ok_response(nlohmann::json::object())));
                    break;
                }
                case OfxRuntimeCommand::Shutdown: {
                    stop_server = true;
                    (*client)->write_json(to_json(ok_response(nlohmann::json::object())));
                    break;
                }
            }
        }
    });

    bool ready = false;
    for (int attempt = 0; attempt < 20; ++attempt) {
        auto health_response = send_json_request(
            endpoint,
            to_json(OfxRuntimeRequestEnvelope{.command = OfxRuntimeCommand::Health,
                                              .payload = nlohmann::json::object()}),
            500);
        if (health_response) {
            ready = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    REQUIRE(ready);

    OfxRuntimeClientOptions options;
    options.endpoint = endpoint;
    options.request_timeout_ms = 2000;
    auto client = OfxRuntimeClient::create(options);
    REQUIRE(client.has_value());

    OfxRuntimePrepareSessionRequest prepare_request;
    prepare_request.client_instance_id = "client-a";
    prepare_request.model_path = "models/corridorkey_fp16_512.onnx";
    prepare_request.artifact_name = "corridorkey_fp16_512.onnx";
    prepare_request.requested_device = DeviceInfo{"RTX Test", 16384, Backend::TensorRT};
    prepare_request.requested_quality_mode = 1;
    prepare_request.requested_resolution = 512;
    prepare_request.effective_resolution = 512;
    prepare_request.engine_options.allow_cpu_fallback = false;
    prepare_request.engine_options.disable_cpu_ep_fallback = true;

    auto prepared = (*client)->prepare_session(prepare_request);
    REQUIRE(prepared.has_value());
    CHECK(prepare_count.load() == 1);

    ImageBuffer rgb_buffer(4, 2, 3);
    ImageBuffer hint_buffer(4, 2, 1);
    std::fill(rgb_buffer.view().data.begin(), rgb_buffer.view().data.end(), 0.5F);
    std::fill(hint_buffer.view().data.begin(), hint_buffer.view().data.end(), 1.0F);

    InferenceParams params;
    params.target_resolution = 512;
    params.batch_size = 1;

    auto frame = (*client)->process_frame(rgb_buffer.view(), hint_buffer.view(), params, 0);
    REQUIRE(frame.has_value());
    CHECK(render_count.load() == 2);
    CHECK(prepare_count.load() == 2);
    CHECK(frame->alpha.const_view().data.front() == Catch::Approx(0.75F));
    CHECK(frame->foreground.const_view().data.front() == Catch::Approx(0.25F));

    auto released = (*client)->release_session();
    REQUIRE(released.has_value());
    CHECK(release_count.load() == 1);

    auto shutdown_response = send_json_request(
        endpoint,
        to_json(OfxRuntimeRequestEnvelope{.command = OfxRuntimeCommand::Shutdown,
                                          .payload = to_json(OfxRuntimeShutdownRequest{"test"})}),
        2000);
    REQUIRE(shutdown_response.has_value());
    stop_server = true;
    server_thread.join();
    REQUIRE_FALSE(server_error.has_value());
}

TEST_CASE("ofx runtime client surfaces protocol mismatches from a stale runtime server",
          "[integration][ofx][runtime][regression]") {
    const auto port = reserve_local_port();
    const LocalJsonEndpoint endpoint{"127.0.0.1", port};

    std::optional<Error> server_error;
    std::atomic<bool> stop_server = false;

    std::thread server_thread([&]() {
        auto server = LocalJsonServer::listen(endpoint);
        if (!server) {
            server_error = server.error();
            return;
        }

        while (!stop_server.load()) {
            auto client = server->accept_one(500);
            if (!client) {
                server_error = client.error();
                return;
            }
            if (!client->has_value()) {
                continue;
            }

            auto request_json = (*client)->read_json(2000);
            if (!request_json) {
                server_error = request_json.error();
                return;
            }

            OfxRuntimeResponseEnvelope response;
            response.protocol_version = kOfxRuntimeProtocolVersion + 1;
            response.success = true;
            response.payload = to_json(OfxRuntimeHealthResponse{4242, 0, 0});
            (*client)->write_json(to_json(response));
        }
    });

    bool ready = false;
    for (int attempt = 0; attempt < 20; ++attempt) {
        auto probe = send_json_request(
            endpoint,
            to_json(OfxRuntimeRequestEnvelope{.command = OfxRuntimeCommand::Health,
                                              .payload = nlohmann::json::object()}),
            500);
        if (probe) {
            ready = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    REQUIRE(ready);

    OfxRuntimeClientOptions options;
    options.endpoint = endpoint;
    options.request_timeout_ms = 1000;
    auto client = OfxRuntimeClient::create(options);
    REQUIRE(client.has_value());

    auto health = (*client)->health();
    REQUIRE_FALSE(health.has_value());
    CHECK(health.error().message.find("Unsupported OFX runtime protocol version") !=
          std::string::npos);

    stop_server = true;
    server_thread.join();
    REQUIRE_FALSE(server_error.has_value());
}
