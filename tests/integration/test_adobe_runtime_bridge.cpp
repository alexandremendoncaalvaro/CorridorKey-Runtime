#include <catch2/catch_all.hpp>
#include <charconv>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

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

constexpr int kAdobeBridgeHealthAttempts = 3;

#if defined(_WIN32)
using NativeSocket = SOCKET;
constexpr NativeSocket kInvalidSocket = INVALID_SOCKET;

class WinSockSession {
   public:
    WinSockSession() {
        REQUIRE(WSAStartup(MAKEWORD(2, 2), &m_data) == 0);
    }

    ~WinSockSession() {
        WSACleanup();
    }

    WinSockSession(const WinSockSession&) = delete;
    WinSockSession& operator=(const WinSockSession&) = delete;

   private:
    WSADATA m_data{};
};
#else
using NativeSocket = int;
constexpr NativeSocket kInvalidSocket = -1;
#endif

void close_native_socket(NativeSocket socket_handle) {
    if (socket_handle == kInvalidSocket) {
        return;
    }
#if defined(_WIN32)
    closesocket(socket_handle);
#else
    close(socket_handle);
#endif
}

class ScopedSocket {
   public:
    explicit ScopedSocket(NativeSocket socket_handle) : m_socket(socket_handle) {}

    ~ScopedSocket() {
        close_native_socket(m_socket);
    }

    ScopedSocket(const ScopedSocket&) = delete;
    ScopedSocket& operator=(const ScopedSocket&) = delete;

    [[nodiscard]] NativeSocket get() const {
        return m_socket;
    }

    [[nodiscard]] bool valid() const {
        return m_socket != kInvalidSocket;
    }

   private:
    NativeSocket m_socket = kInvalidSocket;
};

std::uint16_t reserve_adobe_bridge_port() {
#if defined(_WIN32)
    WinSockSession winsock;
    ScopedSocket socket_handle(socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
#else
    ScopedSocket socket_handle(socket(AF_INET, SOCK_STREAM, 0));
#endif
    REQUIRE(socket_handle.valid());

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(0);

    REQUIRE(bind(socket_handle.get(), reinterpret_cast<const sockaddr*>(&address),
                 sizeof(address)) == 0);

    sockaddr_in bound_address{};
#if defined(_WIN32)
    int length = sizeof(bound_address);
    REQUIRE(getsockname(socket_handle.get(), reinterpret_cast<sockaddr*>(&bound_address),
                        &length) == 0);
#else
    socklen_t length = sizeof(bound_address);
    REQUIRE(getsockname(socket_handle.get(), reinterpret_cast<sockaddr*>(&bound_address),
                        &length) == 0);
#endif

    return ntohs(bound_address.sin_port);
}

bool shutdown_runtime_server(const LocalJsonEndpoint& endpoint) {
    auto shutdown_response = send_json_request(
        endpoint,
        to_json(HostPluginRuntimeRequestEnvelope{
            .command = HostPluginRuntimeCommand::Shutdown,
            .payload = to_json(HostPluginRuntimeShutdownRequest{"adobe bridge smoke test"}),
        }),
        2000);
    return shutdown_response.has_value();
}

void capture_spawned_pid(std::optional<int>& launched_pid, std::string_view message) {
    constexpr std::string_view kPrefix = "event=launch_server_spawned pid=";
    if (!message.starts_with(kPrefix)) {
        return;
    }

    const std::string_view pid_text = message.substr(kPrefix.size());
    int pid = 0;
    const auto* begin = pid_text.data();
    const auto* end = pid_text.data() + pid_text.size();
    const auto result = std::from_chars(begin, end, pid);
    if (result.ec == std::errc{} && result.ptr == end) {
        launched_pid = pid;
    }
}

}  // namespace

TEST_CASE("adobe runtime bridge reaches staged runtime service health endpoint",
          "[integration][adobe][runtime]") {
#if !defined(HOST_PLUGIN_RUNTIME_SERVER_PATH)
    SUCCEED("The host plugin runtime server binary is not built on this platform.");
#else
    const std::filesystem::path server_binary = HOST_PLUGIN_RUNTIME_SERVER_PATH;
    REQUIRE(std::filesystem::exists(server_binary));

    std::optional<corridorkey::Error> last_error;
    for (int attempt = 0; attempt < kAdobeBridgeHealthAttempts; ++attempt) {
        const LocalJsonEndpoint endpoint{"127.0.0.1", reserve_adobe_bridge_port()};
        std::optional<int> launched_pid;
        HostPluginRuntimeClientOptions client_options;
        client_options.endpoint = endpoint;
        client_options.server_binary = server_binary;
        client_options.log_callback = [&launched_pid](std::string_view /*scope*/,
                                                      std::string_view message) {
            capture_spawned_pid(launched_pid, message);
        };
        client_options.request_timeout_ms = 2000;
        client_options.prepare_timeout_ms = 2000;
        client_options.launch_timeout_ms = 10000;

        auto client = HostPluginRuntimeClient::create(client_options);
        REQUIRE(client.has_value());

        AdobeRuntimeBridge bridge(std::move(*client));
        auto health = bridge.health();
        const bool stopped = shutdown_runtime_server(endpoint);
        if (health.has_value()) {
            REQUIRE(launched_pid.has_value());
            CHECK(health->server_pid == *launched_pid);
            CHECK(stopped);
            CHECK(health->server_pid > 0);
            return;
        }
        last_error = health.error();
    }

    INFO("Last Adobe runtime health error: " << (last_error.has_value() ? last_error->message
                                                                        : std::string("none")));
    FAIL("Adobe runtime bridge health endpoint did not respond.");
#endif
}
