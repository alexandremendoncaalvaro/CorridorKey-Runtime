#include <catch2/catch_all.hpp>
#include <cstdint>
#include <filesystem>

#include "app/host_plugin_runtime_client.hpp"
#include "app/host_plugin_runtime_protocol.hpp"
#include "common/local_ipc.hpp"
#include "plugins/adobe/adobe_bridge.hpp"

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

using namespace corridorkey::adobe;
using namespace corridorkey::app;
using namespace corridorkey::common;

namespace {

std::uint16_t reserve_adobe_bridge_port() {
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

void shutdown_runtime_server(const LocalJsonEndpoint& endpoint) {
    auto shutdown_response = send_json_request(
        endpoint,
        to_json(HostPluginRuntimeRequestEnvelope{
            .command = HostPluginRuntimeCommand::Shutdown,
            .payload = to_json(HostPluginRuntimeShutdownRequest{"adobe bridge smoke test"}),
        }),
        2000);
    CHECK(shutdown_response.has_value());
}

}  // namespace

TEST_CASE("adobe runtime bridge reaches staged runtime service health endpoint",
          "[integration][adobe][runtime]") {
#if !defined(HOST_PLUGIN_RUNTIME_SERVER_PATH)
    SUCCEED("The host plugin runtime server binary is not built on this platform.");
#else
    const std::filesystem::path server_binary = HOST_PLUGIN_RUNTIME_SERVER_PATH;
    REQUIRE(std::filesystem::exists(server_binary));

    const LocalJsonEndpoint endpoint{"127.0.0.1", reserve_adobe_bridge_port()};
    HostPluginRuntimeClientOptions client_options;
    client_options.endpoint = endpoint;
    client_options.server_binary = server_binary;
    client_options.request_timeout_ms = 2000;
    client_options.prepare_timeout_ms = 2000;
    client_options.launch_timeout_ms = 10000;

    auto client = HostPluginRuntimeClient::create(client_options);
    REQUIRE(client.has_value());

    AdobeRuntimeBridge bridge(std::move(*client));
    auto health = bridge.health();
    shutdown_runtime_server(endpoint);

    REQUIRE(health.has_value());
    CHECK(health->server_pid > 0);
#endif
}
