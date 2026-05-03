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

//
// Test-file tidy-suppression rationale.
//
// Test fixtures legitimately use single-letter loop locals, magic
// numbers (resolution rungs, pixel coordinates, expected error counts),
// std::vector::operator[] on indices the test itself just constructed,
// and Catch2 / aggregate-init styles that pre-date the project's
// tightened .clang-tidy ruleset. The test source is verified
// behaviourally by ctest; converting every site to bounds-checked /
// designated-init / ranges form would obscure intent without changing
// what the tests prove. The same suppressions are documented and
// applied on the src/ tree where the underlying APIs live.
//
// NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,readability-identifier-length,bugprone-easily-swappable-parameters,readability-function-cognitive-complexity,readability-function-size,cppcoreguidelines-avoid-magic-numbers,modernize-use-designated-initializers,readability-uppercase-literal-suffix,readability-math-missing-parentheses,modernize-use-ranges,modernize-use-starts-ends-with,modernize-use-emplace,modernize-use-auto,modernize-loop-convert,modernize-avoid-c-style-cast,modernize-return-braced-init-list,readability-implicit-bool-conversion,readability-container-contains,readability-redundant-member-init,readability-redundant-string-init,bugprone-narrowing-conversions,cppcoreguidelines-narrowing-conversions,readability-avoid-nested-conditional-operator,modernize-use-nodiscard,readability-make-member-function-const,cppcoreguidelines-pro-type-reinterpret-cast,bugprone-implicit-widening-of-multiplication-result,readability-redundant-inline-specifier,cppcoreguidelines-prefer-member-initializer,performance-unnecessary-value-param,readability-use-concise-preprocessor-directives,readability-else-after-return,readability-string-compare,bugprone-exception-escape,cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays,bugprone-branch-clone,cert-err33-c,readability-redundant-declaration,readability-qualified-auto,modernize-use-scoped-lock,modernize-use-bool-literals,cppcoreguidelines-init-variables,cppcoreguidelines-special-member-functions,cppcoreguidelines-owning-memory,cppcoreguidelines-no-malloc,performance-enum-size,performance-avoid-endl,bugprone-unchecked-optional-access,bugprone-unchecked-string-to-number-conversion,cppcoreguidelines-pro-type-cstyle-cast,modernize-use-using,modernize-use-integer-sign-comparison,cert-dcl50-cpp,cppcoreguidelines-pro-type-const-cast,readability-identifier-naming,modernize-raw-string-literal,readability-container-size-empty,bugprone-command-processor,readability-use-std-min-max,cppcoreguidelines-avoid-non-const-global-variables,bugprone-misplaced-widening-cast,readability-misleading-indentation,cert-env33-c,performance-unnecessary-copy-initialization,readability-named-parameter,readability-isolate-declaration,cert-err34-c,modernize-avoid-variadic-functions,cppcoreguidelines-pro-bounds-constant-array-index)

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
                    (void)(*client)->write_json(to_json(ok_response(to_json(health))));
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
                    (void)(*client)->write_json(to_json(ok_response(to_json(response))));
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
                        (void)(*client)->write_json(to_json(
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
                    (void)(*client)->write_json(to_json(ok_response(to_json(response))));
                    break;
                }
                case OfxRuntimeCommand::ReleaseSession: {
                    ++release_count;
                    (void)(*client)->write_json(to_json(ok_response(nlohmann::json::object())));
                    break;
                }
                case OfxRuntimeCommand::Shutdown: {
                    stop_server = true;
                    (void)(*client)->write_json(to_json(ok_response(nlohmann::json::object())));
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

TEST_CASE("ofx runtime client skips foreground hydration for alpha-only requests",
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

            auto request = ofx_runtime_request_from_json(*request_json);
            if (!request) {
                server_error = request.error();
                return;
            }

            switch (request->command) {
                case OfxRuntimeCommand::Health: {
                    OfxRuntimeHealthResponse health;
                    health.server_pid = 4343;
                    health.session_count = 1;
                    health.active_session_count = 1;
                    (void)(*client)->write_json(to_json(ok_response(to_json(health))));
                    break;
                }
                case OfxRuntimeCommand::PrepareSession: {
                    auto parsed = prepare_session_request_from_json(request->payload);
                    if (!parsed) {
                        server_error = parsed.error();
                        return;
                    }

                    OfxRuntimeSessionSnapshot snapshot;
                    snapshot.session_id = "alpha-only-session";
                    snapshot.model_path = parsed->model_path;
                    snapshot.artifact_name = parsed->artifact_name;
                    snapshot.requested_device = parsed->requested_device;
                    snapshot.effective_device = parsed->requested_device;
                    snapshot.requested_quality_mode = parsed->requested_quality_mode;
                    snapshot.requested_resolution = parsed->requested_resolution;
                    snapshot.effective_resolution = parsed->effective_resolution;
                    snapshot.recommended_resolution = parsed->effective_resolution;
                    snapshot.ref_count = 1;

                    OfxRuntimePrepareSessionResponse response;
                    response.session = snapshot;
                    (void)(*client)->write_json(to_json(ok_response(to_json(response))));
                    break;
                }
                case OfxRuntimeCommand::RenderFrame: {
                    auto parsed = render_frame_request_from_json(request->payload);
                    if (!parsed) {
                        server_error = parsed.error();
                        return;
                    }
                    if (!parsed->params.output_alpha_only) {
                        server_error = Error{ErrorCode::InvalidParameters,
                                             "Expected output_alpha_only render request."};
                        return;
                    }

                    auto transport = SharedFrameTransport::open(parsed->shared_frame_path);
                    if (!transport) {
                        server_error = transport.error();
                        return;
                    }
                    fill_transport_result(*transport, 0.6F, 0.2F);

                    OfxRuntimeSessionSnapshot snapshot;
                    snapshot.session_id = "alpha-only-session";
                    snapshot.requested_device = DeviceInfo{"RTX Test", 16384, Backend::TensorRT};
                    snapshot.effective_device = snapshot.requested_device;
                    snapshot.requested_resolution = parsed->params.target_resolution;
                    snapshot.effective_resolution = parsed->params.target_resolution;
                    snapshot.recommended_resolution = parsed->params.target_resolution;
                    snapshot.ref_count = 1;

                    OfxRuntimeRenderFrameResponse response;
                    response.session = snapshot;
                    (void)(*client)->write_json(to_json(ok_response(to_json(response))));
                    break;
                }
                case OfxRuntimeCommand::ReleaseSession: {
                    (void)(*client)->write_json(to_json(ok_response(nlohmann::json::object())));
                    break;
                }
                case OfxRuntimeCommand::Shutdown: {
                    stop_server = true;
                    (void)(*client)->write_json(to_json(ok_response(nlohmann::json::object())));
                    break;
                }
            }
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
    options.request_timeout_ms = 2000;
    auto client = OfxRuntimeClient::create(options);
    REQUIRE(client.has_value());

    OfxRuntimePrepareSessionRequest prepare_request;
    prepare_request.client_instance_id = "client-alpha-only";
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

    ImageBuffer rgb_buffer(4, 2, 3);
    ImageBuffer hint_buffer(4, 2, 1);
    std::fill(rgb_buffer.view().data.begin(), rgb_buffer.view().data.end(), 0.5F);
    std::fill(hint_buffer.view().data.begin(), hint_buffer.view().data.end(), 1.0F);

    InferenceParams params;
    params.target_resolution = 512;
    params.batch_size = 1;
    params.output_alpha_only = true;

    auto frame = (*client)->process_frame(rgb_buffer.view(), hint_buffer.view(), params, 0);
    REQUIRE(frame.has_value());
    CHECK(frame->alpha.const_view().data.front() == Catch::Approx(0.6F));
    CHECK(frame->foreground.const_view().empty());

    auto released = (*client)->release_session();
    REQUIRE(released.has_value());

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

TEST_CASE("ofx runtime client re-prepares when quality metadata changes for the same model",
          "[integration][ofx][runtime][regression]") {
    const auto port = reserve_local_port();
    const LocalJsonEndpoint endpoint{"127.0.0.1", port};

    std::atomic<int> prepare_count = 0;
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
                    (void)(*client)->write_json(to_json(ok_response(to_json(health))));
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
                    snapshot.session_id = "session-" + std::to_string(current_prepare);
                    snapshot.model_path = parsed->model_path;
                    snapshot.artifact_name = parsed->artifact_name;
                    snapshot.requested_device = parsed->requested_device;
                    snapshot.effective_device = parsed->requested_device;
                    snapshot.requested_quality_mode = parsed->requested_quality_mode;
                    snapshot.requested_resolution = parsed->requested_resolution;
                    snapshot.effective_resolution = parsed->effective_resolution;
                    snapshot.recommended_resolution = parsed->effective_resolution;
                    snapshot.ref_count = 1;

                    OfxRuntimePrepareSessionResponse response;
                    response.session = snapshot;
                    (void)(*client)->write_json(to_json(ok_response(to_json(response))));
                    break;
                }
                case OfxRuntimeCommand::ReleaseSession: {
                    ++release_count;
                    (void)(*client)->write_json(to_json(ok_response(nlohmann::json::object())));
                    break;
                }
                case OfxRuntimeCommand::Shutdown: {
                    stop_server = true;
                    (void)(*client)->write_json(to_json(ok_response(nlohmann::json::object())));
                    break;
                }
                case OfxRuntimeCommand::RenderFrame: {
                    server_error = Error{ErrorCode::InvalidParameters,
                                         "RenderFrame is not expected in the prepare-session cache "
                                         "regression test."};
                    return;
                }
            }
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
    options.request_timeout_ms = 2000;
    auto client = OfxRuntimeClient::create(options);
    REQUIRE(client.has_value());

    OfxRuntimePrepareSessionRequest first_request;
    first_request.client_instance_id = "client-a";
    first_request.model_path = "models/corridorkey_fp16_1024.onnx";
    first_request.artifact_name = "corridorkey_fp16_1024.onnx";
    first_request.requested_device = DeviceInfo{"RTX Test", 16384, Backend::TensorRT};
    first_request.requested_quality_mode = 2;
    first_request.requested_resolution = 1024;
    first_request.effective_resolution = 1024;
    first_request.engine_options.allow_cpu_fallback = false;
    first_request.engine_options.disable_cpu_ep_fallback = true;

    auto first_prepare = (*client)->prepare_session(first_request);
    REQUIRE(first_prepare.has_value());
    CHECK(prepare_count.load() == 1);
    CHECK(release_count.load() == 0);

    auto second_request = first_request;
    second_request.requested_quality_mode = 3;
    second_request.requested_resolution = 1536;
    second_request.effective_resolution = 1024;

    auto second_prepare = (*client)->prepare_session(second_request);
    REQUIRE(second_prepare.has_value());
    CHECK(prepare_count.load() == 2);
    CHECK(release_count.load() == 1);
    CHECK(second_prepare->session.session_id == "session-2");
    CHECK(second_prepare->session.requested_quality_mode == second_request.requested_quality_mode);
    CHECK(second_prepare->session.requested_resolution == second_request.requested_resolution);
    CHECK(second_prepare->session.effective_resolution == second_request.effective_resolution);

    auto released = (*client)->release_session();
    REQUIRE(released.has_value());
    CHECK(release_count.load() == 2);

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

TEST_CASE("ofx runtime client invalidates structural render failures until re-prepare",
          "[integration][ofx][runtime][regression]") {
    const auto port = reserve_local_port();
    const LocalJsonEndpoint endpoint{"127.0.0.1", port};

    std::atomic<int> prepare_count = 0;
    std::atomic<int> render_count = 0;
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
                    health.server_pid = 5252;
                    health.session_count = 1;
                    health.active_session_count = 1;
                    (void)(*client)->write_json(to_json(ok_response(to_json(health))));
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
                    snapshot.session_id = "session-" + std::to_string(current_prepare);
                    snapshot.model_path = parsed->model_path;
                    snapshot.artifact_name = parsed->artifact_name;
                    snapshot.requested_device = parsed->requested_device;
                    snapshot.effective_device = parsed->requested_device;
                    snapshot.requested_quality_mode = parsed->requested_quality_mode;
                    snapshot.requested_resolution = parsed->requested_resolution;
                    snapshot.effective_resolution = parsed->effective_resolution;
                    snapshot.recommended_resolution = parsed->effective_resolution;
                    snapshot.ref_count = 1;

                    OfxRuntimePrepareSessionResponse response;
                    response.session = snapshot;
                    (void)(*client)->write_json(to_json(ok_response(to_json(response))));
                    break;
                }
                case OfxRuntimeCommand::RenderFrame: {
                    auto parsed = render_frame_request_from_json(request->payload);
                    if (!parsed) {
                        server_error = parsed.error();
                        return;
                    }

                    ++render_count;
                    if (prepare_count.load() == 1) {
                        (void)(*client)->write_json(
                            to_json(error_response("Model output contains non-finite values.")));
                        break;
                    }

                    auto transport = SharedFrameTransport::open(parsed->shared_frame_path);
                    if (!transport) {
                        server_error = transport.error();
                        return;
                    }
                    fill_transport_result(*transport, 0.5F, 0.9F);

                    OfxRuntimeSessionSnapshot snapshot;
                    snapshot.session_id = "session-2";
                    snapshot.requested_device = DeviceInfo{"RTX Test", 16384, Backend::TensorRT};
                    snapshot.effective_device = snapshot.requested_device;
                    snapshot.requested_resolution = parsed->params.target_resolution;
                    snapshot.effective_resolution = parsed->params.target_resolution;
                    snapshot.recommended_resolution = parsed->params.target_resolution;
                    snapshot.ref_count = 1;

                    OfxRuntimeRenderFrameResponse response;
                    response.session = snapshot;
                    (void)(*client)->write_json(to_json(ok_response(to_json(response))));
                    break;
                }
                case OfxRuntimeCommand::ReleaseSession: {
                    (void)(*client)->write_json(to_json(ok_response(nlohmann::json::object())));
                    break;
                }
                case OfxRuntimeCommand::Shutdown: {
                    stop_server = true;
                    (void)(*client)->write_json(to_json(ok_response(nlohmann::json::object())));
                    break;
                }
            }
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
    options.request_timeout_ms = 2000;
    auto client = OfxRuntimeClient::create(options);
    REQUIRE(client.has_value());

    OfxRuntimePrepareSessionRequest prepare_request;
    prepare_request.client_instance_id = "client-a";
    prepare_request.model_path = "models/corridorkey_fp16_1024.onnx";
    prepare_request.artifact_name = "corridorkey_fp16_1024.onnx";
    prepare_request.requested_device = DeviceInfo{"RTX Test", 16384, Backend::TensorRT};
    prepare_request.requested_quality_mode = 2;
    prepare_request.requested_resolution = 1024;
    prepare_request.effective_resolution = 1024;
    prepare_request.engine_options.allow_cpu_fallback = false;
    prepare_request.engine_options.disable_cpu_ep_fallback = true;

    auto first_prepare = (*client)->prepare_session(prepare_request);
    REQUIRE(first_prepare.has_value());
    CHECK((*client)->has_session());

    ImageBuffer rgb_buffer(4, 2, 3);
    ImageBuffer hint_buffer(4, 2, 1);
    std::fill(rgb_buffer.view().data.begin(), rgb_buffer.view().data.end(), 0.5F);
    std::fill(hint_buffer.view().data.begin(), hint_buffer.view().data.end(), 1.0F);

    InferenceParams params;
    params.target_resolution = 1024;
    params.batch_size = 1;

    auto failed_frame = (*client)->process_frame(rgb_buffer.view(), hint_buffer.view(), params, 0);
    REQUIRE_FALSE(failed_frame.has_value());
    CHECK(failed_frame.error().message.find("non-finite") != std::string::npos);
    CHECK_FALSE((*client)->has_session());
    CHECK(render_count.load() == 1);

    auto blocked_frame = (*client)->process_frame(rgb_buffer.view(), hint_buffer.view(), params, 1);
    REQUIRE_FALSE(blocked_frame.has_value());
    CHECK(blocked_frame.error().message.find("not prepared") != std::string::npos);
    CHECK(render_count.load() == 1);

    auto recovered_prepare = (*client)->prepare_session(prepare_request);
    REQUIRE(recovered_prepare.has_value());
    CHECK(prepare_count.load() == 2);
    CHECK((*client)->has_session());

    auto recovered_frame =
        (*client)->process_frame(rgb_buffer.view(), hint_buffer.view(), params, 2);
    REQUIRE(recovered_frame.has_value());
    CHECK(render_count.load() == 2);
    CHECK(recovered_frame->alpha.const_view().data.front() == Catch::Approx(0.5F));
    CHECK(recovered_frame->foreground.const_view().data.front() == Catch::Approx(0.9F));

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
            (void)(*client)->write_json(to_json(response));
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

// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,readability-identifier-length,bugprone-easily-swappable-parameters,readability-function-cognitive-complexity,readability-function-size,cppcoreguidelines-avoid-magic-numbers,modernize-use-designated-initializers,readability-uppercase-literal-suffix,readability-math-missing-parentheses,modernize-use-ranges,modernize-use-starts-ends-with,modernize-use-emplace,modernize-use-auto,modernize-loop-convert,modernize-avoid-c-style-cast,modernize-return-braced-init-list,readability-implicit-bool-conversion,readability-container-contains,readability-redundant-member-init,readability-redundant-string-init,bugprone-narrowing-conversions,cppcoreguidelines-narrowing-conversions,readability-avoid-nested-conditional-operator,modernize-use-nodiscard,readability-make-member-function-const,cppcoreguidelines-pro-type-reinterpret-cast,bugprone-implicit-widening-of-multiplication-result,readability-redundant-inline-specifier,cppcoreguidelines-prefer-member-initializer,performance-unnecessary-value-param,readability-use-concise-preprocessor-directives,readability-else-after-return,readability-string-compare,bugprone-exception-escape,cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays,bugprone-branch-clone,cert-err33-c,readability-redundant-declaration,readability-qualified-auto,modernize-use-scoped-lock,modernize-use-bool-literals,cppcoreguidelines-init-variables,cppcoreguidelines-special-member-functions,cppcoreguidelines-owning-memory,cppcoreguidelines-no-malloc,performance-enum-size,performance-avoid-endl,bugprone-unchecked-optional-access,bugprone-unchecked-string-to-number-conversion,cppcoreguidelines-pro-type-cstyle-cast,modernize-use-using,modernize-use-integer-sign-comparison,cert-dcl50-cpp,cppcoreguidelines-pro-type-const-cast,readability-identifier-naming,modernize-raw-string-literal,readability-container-size-empty,bugprone-command-processor,readability-use-std-min-max,cppcoreguidelines-avoid-non-const-global-variables,bugprone-misplaced-widening-cast,readability-misleading-indentation,cert-env33-c,performance-unnecessary-copy-initialization,readability-named-parameter,readability-isolate-declaration,cert-err34-c,modernize-avoid-variadic-functions,cppcoreguidelines-pro-bounds-constant-array-index)
