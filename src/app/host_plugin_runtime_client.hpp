#pragma once

#include <corridorkey/engine.hpp>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string_view>

#include "app/host_plugin_runtime_protocol.hpp"
#include "common/host_plugin_runtime_defaults.hpp"
#include "common/local_ipc.hpp"

namespace corridorkey::app {

using HostPluginRuntimeLogCallback =
    std::function<void(std::string_view scope, std::string_view message)>;

struct HostPluginRuntimeClientOptions {
    static constexpr int kDefaultLaunchTimeoutMs = 10000;

    common::LocalJsonEndpoint endpoint;
    std::filesystem::path server_binary;
    HostPluginRuntimeLogCallback log_callback;
    int request_timeout_ms = common::kDefaultHostPluginRequestTimeoutMs;
    int prepare_timeout_ms = common::kDefaultHostPluginPrepareTimeoutMs;
    int launch_timeout_ms = kDefaultLaunchTimeoutMs;
    int idle_timeout_ms = common::kDefaultHostPluginIdleTimeoutMs;
};

class HostPluginRuntimeClient {
   public:
    static Result<std::unique_ptr<HostPluginRuntimeClient>> create(
        HostPluginRuntimeClientOptions options);

    ~HostPluginRuntimeClient();

    HostPluginRuntimeClient(const HostPluginRuntimeClient&) = delete;
    HostPluginRuntimeClient& operator=(const HostPluginRuntimeClient&) = delete;
    HostPluginRuntimeClient(HostPluginRuntimeClient&&) = delete;
    HostPluginRuntimeClient& operator=(HostPluginRuntimeClient&&) = delete;

    Result<HostPluginRuntimeHealthResponse> health();
    Result<HostPluginRuntimePrepareSessionResponse> prepare_session(
        const HostPluginRuntimePrepareSessionRequest& request,
        StageTimingCallback on_stage = nullptr);
    Result<FrameResult> process_frame(const Image& rgb, const Image& alpha_hint,
                                      const InferenceParams& params, std::uint64_t render_index,
                                      StageTimingCallback on_stage = nullptr);
    Result<void> release_session();

    [[nodiscard]] DeviceInfo current_device() const;
    [[nodiscard]] std::optional<BackendFallbackInfo> backend_fallback() const;
    [[nodiscard]] bool has_session() const;
    [[nodiscard]] std::uint64_t session_ref_count() const;
    void set_request_timeout_ms(int timeout_ms);
    void set_prepare_timeout_ms(int timeout_ms);

   private:
    explicit HostPluginRuntimeClient(HostPluginRuntimeClientOptions options);

    Result<void> ensure_server_running();
    Result<nlohmann::json> send_command(HostPluginRuntimeCommand command,
                                        const nlohmann::json& payload);
    [[nodiscard]] Result<nlohmann::json> send_command_without_launch(
        HostPluginRuntimeCommand command, const nlohmann::json& payload) const;
    [[nodiscard]] Result<nlohmann::json> send_command_without_launch(
        HostPluginRuntimeCommand command, const nlohmann::json& payload, int timeout_ms) const;
    Result<void> launch_server();
    Result<void> recover_runtime_session(StageTimingCallback on_stage);
    Result<void> restart_server(const std::string& reason);
    [[nodiscard]] bool session_belongs_to_current_server() const;
    void invalidate_session(const std::string& reason);
    void log_message(std::string_view scope, std::string_view message) const;
    void update_session_snapshot(const HostPluginRuntimeSessionSnapshot& snapshot);
    void update_server_health(const HostPluginRuntimeHealthResponse& health);

    HostPluginRuntimeClientOptions m_options;
    HostPluginRuntimeSessionSnapshot m_session;
    std::optional<HostPluginRuntimePrepareSessionRequest> m_last_prepare_request;
    int m_server_pid = 0;
    int m_session_server_pid = 0;
};

std::filesystem::path resolve_host_plugin_runtime_server_binary(
    const std::filesystem::path& plugin_module_path);

}  // namespace corridorkey::app
