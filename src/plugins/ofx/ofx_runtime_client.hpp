#pragma once

#include <corridorkey/engine.hpp>
#include <filesystem>
#include <memory>
#include <optional>

#include "app/ofx_runtime_protocol.hpp"
#include "common/local_ipc.hpp"
#include "common/ofx_runtime_defaults.hpp"

namespace corridorkey::ofx {

struct OfxRuntimeClientOptions {
    common::LocalJsonEndpoint endpoint = {};
    std::filesystem::path server_binary = {};
    int request_timeout_ms = common::kDefaultOfxRequestTimeoutMs;
    int prepare_timeout_ms = common::kDefaultOfxPrepareTimeoutMs;
    int launch_timeout_ms = 10000;
    int idle_timeout_ms = common::kDefaultOfxIdleTimeoutMs;
};

class OfxRuntimeClient {
   public:
    static Result<std::unique_ptr<OfxRuntimeClient>> create(OfxRuntimeClientOptions options);

    ~OfxRuntimeClient();

    Result<app::OfxRuntimeHealthResponse> health();
    Result<app::OfxRuntimePrepareSessionResponse> prepare_session(
        const app::OfxRuntimePrepareSessionRequest& request,
        StageTimingCallback on_stage = nullptr);
    Result<FrameResult> process_frame(const Image& rgb, const Image& alpha_hint,
                                      const InferenceParams& params, std::uint64_t render_index,
                                      StageTimingCallback on_stage = nullptr);
    Result<void> release_session();

    [[nodiscard]] DeviceInfo current_device() const;
    [[nodiscard]] std::optional<BackendFallbackInfo> backend_fallback() const;
    [[nodiscard]] bool has_session() const;
    [[nodiscard]] std::uint64_t session_ref_count() const;
    void set_request_timeout_ms(int ms);
    void set_prepare_timeout_ms(int ms);

   private:
    explicit OfxRuntimeClient(OfxRuntimeClientOptions options);

    Result<void> ensure_server_running();
    Result<nlohmann::json> send_command(app::OfxRuntimeCommand command,
                                        const nlohmann::json& payload);
    Result<nlohmann::json> send_command_without_launch(app::OfxRuntimeCommand command,
                                                       const nlohmann::json& payload) const;
    Result<nlohmann::json> send_command_without_launch(app::OfxRuntimeCommand command,
                                                       const nlohmann::json& payload,
                                                       int timeout_ms) const;
    Result<void> launch_server();
    Result<void> recover_runtime_session(StageTimingCallback on_stage);
    Result<void> restart_server(const std::string& reason);
    void invalidate_session(const std::string& reason);
    void update_session_snapshot(const app::OfxRuntimeSessionSnapshot& snapshot);
    void update_server_health(const app::OfxRuntimeHealthResponse& health);

    OfxRuntimeClientOptions m_options = {};
    app::OfxRuntimeSessionSnapshot m_session = {};
    std::optional<app::OfxRuntimePrepareSessionRequest> m_last_prepare_request = std::nullopt;
    int m_server_pid = 0;
};

std::filesystem::path resolve_ofx_runtime_server_binary(
    const std::filesystem::path& plugin_module_path);

}  // namespace corridorkey::ofx
