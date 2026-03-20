#include <catch2/catch_all.hpp>

#include <chrono>
#include <filesystem>
#include <optional>
#include <thread>

#include "app/ofx_runtime_protocol.hpp"
#include "app/ofx_runtime_service.hpp"
#include "common/local_ipc.hpp"

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

    sockaddr_in address {};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(0);

    REQUIRE(bind(socket_handle, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0);

    sockaddr_in bound_address {};
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

}  // namespace

TEST_CASE("ofx runtime service responds to health and shutdown commands",
          "[integration][ofx][runtime]") {
    const auto port = reserve_local_port();
    const auto log_path =
        std::filesystem::temp_directory_path() / ("corridorkey_ofx_runtime_" +
                                                  std::to_string(port) + ".log");

    OfxRuntimeServiceOptions options;
    options.endpoint = LocalJsonEndpoint{"127.0.0.1", port};
    options.idle_timeout = std::chrono::seconds(1);
    options.log_path = log_path;

    std::optional<Error> server_error;
    std::thread server_thread([&]() {
        auto result = OfxRuntimeService::run(options);
        if (!result) {
            server_error = result.error();
        }
    });

    auto stop_server = [&]() {
        OfxRuntimeRequestEnvelope shutdown_request;
        shutdown_request.command = OfxRuntimeCommand::Shutdown;
        shutdown_request.payload = to_json(OfxRuntimeShutdownRequest{"test_shutdown"});
        auto shutdown_response = send_json_request(options.endpoint, to_json(shutdown_request), 2000);
        if (shutdown_response) {
            auto parsed = ofx_runtime_response_from_json(*shutdown_response);
            REQUIRE(parsed.has_value());
            REQUIRE(parsed->success);
        }
    };

    bool ready = false;
    for (int attempt = 0; attempt < 20; ++attempt) {
        OfxRuntimeRequestEnvelope health_request;
        health_request.command = OfxRuntimeCommand::Health;
        health_request.payload = nlohmann::json::object();

        auto health_response = send_json_request(options.endpoint, to_json(health_request), 500);
        if (health_response) {
            auto parsed_response = ofx_runtime_response_from_json(*health_response);
            REQUIRE(parsed_response.has_value());
            REQUIRE(parsed_response->success);

            auto health = health_response_from_json(parsed_response->payload);
            REQUIRE(health.has_value());
            CHECK(health->server_pid > 0);
            CHECK(health->session_count == 0);
            CHECK(health->active_session_count == 0);
            ready = true;
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    if (ready) {
        stop_server();
    }
    server_thread.join();
    CAPTURE(port);
    REQUIRE(ready);
    REQUIRE_FALSE(server_error.has_value());
    REQUIRE(std::filesystem::exists(log_path));
}
