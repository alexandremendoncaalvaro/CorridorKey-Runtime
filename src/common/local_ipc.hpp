#pragma once

#include <corridorkey/types.hpp>
#include <cstddef>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>

namespace corridorkey::common {

struct LocalJsonEndpoint {
    std::string host = "127.0.0.1";
    std::uint16_t port = 0;
};

class CORRIDORKEY_API LocalJsonConnection {
   public:
    LocalJsonConnection();
    ~LocalJsonConnection();

    LocalJsonConnection(const LocalJsonConnection&) = delete;
    LocalJsonConnection& operator=(const LocalJsonConnection&) = delete;
    LocalJsonConnection(LocalJsonConnection&& other) noexcept;
    LocalJsonConnection& operator=(LocalJsonConnection&& other) noexcept;

    [[nodiscard]] bool valid() const;
    [[nodiscard]] Result<void> write_json(const nlohmann::json& json) const;
    [[nodiscard]] Result<nlohmann::json> read_json(int timeout_ms) const;

   private:
    friend class LocalJsonServer;
    explicit LocalJsonConnection(std::intptr_t socket_handle);
    void close();

    std::intptr_t m_socket = -1;
};

class CORRIDORKEY_API LocalJsonServer {
   public:
    static Result<LocalJsonServer> listen(const LocalJsonEndpoint& endpoint);

    LocalJsonServer();
    ~LocalJsonServer();

    LocalJsonServer(const LocalJsonServer&) = delete;
    LocalJsonServer& operator=(const LocalJsonServer&) = delete;
    LocalJsonServer(LocalJsonServer&& other) noexcept;
    LocalJsonServer& operator=(LocalJsonServer&& other) noexcept;

    [[nodiscard]] Result<std::optional<LocalJsonConnection>> accept_one(int timeout_ms) const;

   private:
    explicit LocalJsonServer(std::intptr_t socket_handle);
    void close();

    std::intptr_t m_socket = -1;
};

CORRIDORKEY_API LocalJsonEndpoint default_ofx_runtime_endpoint();

CORRIDORKEY_API Result<nlohmann::json> send_json_request(const LocalJsonEndpoint& endpoint,
                                                         const nlohmann::json& request,
                                                         int timeout_ms);

}  // namespace corridorkey::common
