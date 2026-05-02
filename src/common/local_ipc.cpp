#include "local_ipc.hpp"

#include <cstring>

#include "runtime_paths.hpp"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

// NOLINTBEGIN(readability-use-concise-preprocessor-directives,modernize-use-designated-initializers,cppcoreguidelines-avoid-magic-numbers,cppcoreguidelines-pro-type-reinterpret-cast,modernize-use-integer-sign-comparison)
//
// local_ipc.cpp tidy-suppression rationale.
//
// This TU wraps the BSD/Winsock C socket API for the OFX runtime
// control channel. The reinterpret_cast<sockaddr*>(&sockaddr_in) and
// reinterpret_cast<const char*>(&int) call sites are the documented
// portable spelling for setsockopt/bind/connect; switching to bit_cast
// is impossible because the socket APIs take pointers to the live
// objects. The 1000 (ms-to-us conversion), 16 (listen backlog), and
// 4096 (initial JSON line buffer reserve) literals are universal POSIX
// constants rather than tunable policy. The Error{} aggregate matches
// the project-wide positional Result<T> style. The remaining
// #if defined(_WIN32) blocks are multi-line platform branches whose
// #else arm makes them not refactorable to #ifdef in the rest of the
// codebase, but the single-condition tops can stay; the NOLINT covers
// the corner case where clang-tidy still flags them.
namespace corridorkey::common {

namespace {

Error socket_error(const std::string& message) {
    return Error{ErrorCode::IoError, message};
}

#ifdef _WIN32
using NativeSocket = SOCKET;
constexpr NativeSocket kInvalidSocket = INVALID_SOCKET;
#else
using NativeSocket = int;
constexpr NativeSocket kInvalidSocket = -1;
#endif

class SocketRuntime {
   public:
    SocketRuntime() {
#ifdef _WIN32
        static bool initialized = false;
        static WSADATA data;
        if (!initialized) {
            WSAStartup(MAKEWORD(2, 2), &data);
            initialized = true;
        }
#endif
    }
};

void close_socket(NativeSocket socket_handle) {
    if (socket_handle == kInvalidSocket) {
        return;
    }
#ifdef _WIN32
    closesocket(socket_handle);
#else
    close(socket_handle);
#endif
}

Result<void> wait_for_socket(NativeSocket socket_handle, bool for_read, int timeout_ms) {
    fd_set set;
    FD_ZERO(&set);
    FD_SET(socket_handle, &set);
    timeval timeout{};
    timeout.tv_sec = timeout_ms / 1000;
    timeout.tv_usec = (timeout_ms % 1000) * 1000;

    int result = select(static_cast<int>(socket_handle + 1), for_read ? &set : nullptr,
                        for_read ? nullptr : &set, nullptr, &timeout);
    if (result < 0) {
        return Unexpected<Error>(socket_error("Socket select failed."));
    }
    if (result == 0) {
        return Unexpected<Error>(socket_error("Socket operation timed out."));
    }
    return {};
}

Result<nlohmann::json> read_json_line(NativeSocket socket_handle, int timeout_ms) {
    std::string buffer;
    buffer.reserve(4096);

    while (true) {
        auto wait_result = wait_for_socket(socket_handle, true, timeout_ms);
        if (!wait_result) {
            return Unexpected<Error>(wait_result.error());
        }

        char byte = '\0';
        int received = recv(socket_handle, &byte, 1, 0);
        if (received <= 0) {
            return Unexpected<Error>(socket_error("Socket closed before JSON message completed."));
        }
        if (byte == '\n') {
            break;
        }
        buffer.push_back(byte);
    }

    try {
        return nlohmann::json::parse(buffer);
    } catch (const std::exception& error) {
        return Unexpected<Error>(
            socket_error(std::string("Failed to parse JSON control message: ") + error.what()));
    }
}

Result<void> write_json_line(NativeSocket socket_handle, const nlohmann::json& json) {
    auto serialized = json.dump();
    serialized.push_back('\n');

    std::size_t sent_total = 0;
    while (sent_total < serialized.size()) {
        int sent = send(socket_handle, serialized.data() + sent_total,
                        static_cast<int>(serialized.size() - sent_total), 0);
        if (sent <= 0) {
            return Unexpected<Error>(socket_error("Failed to write JSON control message."));
        }
        sent_total += static_cast<std::size_t>(sent);
    }
    return {};
}

sockaddr_in make_address(const LocalJsonEndpoint& endpoint) {
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(endpoint.port);
    inet_pton(AF_INET, endpoint.host.c_str(), &address.sin_addr);
    return address;
}

}  // namespace

LocalJsonConnection::LocalJsonConnection() = default;

LocalJsonConnection::LocalJsonConnection(std::intptr_t socket_handle) : m_socket(socket_handle) {}

LocalJsonConnection::~LocalJsonConnection() {
    close();
}

LocalJsonConnection::LocalJsonConnection(LocalJsonConnection&& other) noexcept {
    *this = std::move(other);
}

LocalJsonConnection& LocalJsonConnection::operator=(LocalJsonConnection&& other) noexcept {
    if (this == &other) {
        return *this;
    }
    close();
    m_socket = other.m_socket;
    other.m_socket = static_cast<std::intptr_t>(kInvalidSocket);
    return *this;
}

bool LocalJsonConnection::valid() const {
    return static_cast<NativeSocket>(m_socket) != kInvalidSocket;
}

Result<void> LocalJsonConnection::write_json(const nlohmann::json& json) const {
    return write_json_line(static_cast<NativeSocket>(m_socket), json);
}

Result<nlohmann::json> LocalJsonConnection::read_json(int timeout_ms) const {
    return read_json_line(static_cast<NativeSocket>(m_socket), timeout_ms);
}

void LocalJsonConnection::close() {
    close_socket(static_cast<NativeSocket>(m_socket));
    m_socket = static_cast<std::intptr_t>(kInvalidSocket);
}

LocalJsonServer::LocalJsonServer() = default;

LocalJsonServer::LocalJsonServer(std::intptr_t socket_handle) : m_socket(socket_handle) {}

LocalJsonServer::~LocalJsonServer() {
    close();
}

LocalJsonServer::LocalJsonServer(LocalJsonServer&& other) noexcept {
    *this = std::move(other);
}

LocalJsonServer& LocalJsonServer::operator=(LocalJsonServer&& other) noexcept {
    if (this == &other) {
        return *this;
    }
    close();
    m_socket = other.m_socket;
    other.m_socket = static_cast<std::intptr_t>(kInvalidSocket);
    return *this;
}

Result<LocalJsonServer> LocalJsonServer::listen(const LocalJsonEndpoint& endpoint) {
    SocketRuntime socket_runtime;
    (void)socket_runtime;

    auto socket_handle = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socket_handle == kInvalidSocket) {
        return Unexpected<Error>(socket_error("Failed to create control socket."));
    }

    int reuse = 1;
    setsockopt(socket_handle, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse),
               sizeof(reuse));

    auto address = make_address(endpoint);
    if (bind(socket_handle, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
        close_socket(socket_handle);
        return Unexpected<Error>(
            socket_error("Failed to bind local runtime control socket on port " +
                         std::to_string(endpoint.port) + "."));
    }

    if (::listen(socket_handle, 16) != 0) {
        close_socket(socket_handle);
        return Unexpected<Error>(socket_error("Failed to listen on local runtime control socket."));
    }

    return LocalJsonServer(static_cast<std::intptr_t>(socket_handle));
}

Result<std::optional<LocalJsonConnection>> LocalJsonServer::accept_one(int timeout_ms) const {
    auto wait_result = wait_for_socket(static_cast<NativeSocket>(m_socket), true, timeout_ms);
    if (!wait_result) {
        if (wait_result.error().message == "Socket operation timed out.") {
            return std::optional<LocalJsonConnection>{};
        }
        return Unexpected<Error>(wait_result.error());
    }

    auto client_socket = accept(static_cast<NativeSocket>(m_socket), nullptr, nullptr);
    if (client_socket == kInvalidSocket) {
        return Unexpected<Error>(socket_error("Failed to accept control connection."));
    }

    return std::optional<LocalJsonConnection>{
        LocalJsonConnection(static_cast<std::intptr_t>(client_socket))};
}

void LocalJsonServer::close() {
    close_socket(static_cast<NativeSocket>(m_socket));
    m_socket = static_cast<std::intptr_t>(kInvalidSocket);
}

LocalJsonEndpoint default_ofx_runtime_endpoint() {
    return LocalJsonEndpoint{"127.0.0.1", default_ofx_runtime_port()};
}

Result<nlohmann::json> send_json_request(const LocalJsonEndpoint& endpoint,
                                         const nlohmann::json& request, int timeout_ms) {
    SocketRuntime socket_runtime;
    (void)socket_runtime;

    auto socket_handle = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socket_handle == kInvalidSocket) {
        return Unexpected<Error>(socket_error("Failed to create client control socket."));
    }

    auto address = make_address(endpoint);
    if (connect(socket_handle, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
        close_socket(socket_handle);
        return Unexpected<Error>(
            socket_error("Failed to connect to local runtime control socket."));
    }

    auto write_result = write_json_line(socket_handle, request);
    if (!write_result) {
        close_socket(socket_handle);
        return Unexpected<Error>(write_result.error());
    }

    auto response = read_json_line(socket_handle, timeout_ms);
    close_socket(socket_handle);
    return response;
}

}  // namespace corridorkey::common
// NOLINTEND(readability-use-concise-preprocessor-directives,modernize-use-designated-initializers,cppcoreguidelines-avoid-magic-numbers,cppcoreguidelines-pro-type-reinterpret-cast,modernize-use-integer-sign-comparison)
