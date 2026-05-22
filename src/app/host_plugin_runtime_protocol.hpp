#pragma once

#include <corridorkey/api_export.hpp>
#include <corridorkey/engine.hpp>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

namespace corridorkey::app {

inline constexpr int kHostPluginRuntimeProtocolVersion = 1;

enum class HostPluginRuntimeCommand : std::uint8_t {
    Health,
    PrepareSession,
    RenderFrame,
    ReleaseSession,
    Shutdown,
};

struct HostPluginRuntimeRequestEnvelope {
    int protocol_version = kHostPluginRuntimeProtocolVersion;
    HostPluginRuntimeCommand command = HostPluginRuntimeCommand::Health;
    nlohmann::json payload = nlohmann::json::object();
};

struct HostPluginRuntimeResponseEnvelope {
    int protocol_version = kHostPluginRuntimeProtocolVersion;
    bool success = false;
    std::string error;
    nlohmann::json payload = nlohmann::json::object();
};

struct HostPluginRuntimePrepareSessionRequest {
    std::string client_instance_id;
    std::filesystem::path model_path = {};
    std::string artifact_name;
    DeviceInfo requested_device = {};
    EngineCreateOptions engine_options = {};
    int requested_quality_mode = 0;
    int requested_resolution = 0;
    int effective_resolution = 0;
    // Upper bound (ms) the server honors for the synchronous prewarm phase
    // inside prepare_session. Mirrors the Prepare Timeout UI parameter so
    // the server aborts a runaway MLX JIT before the client RPC times out.
    // 0 means "no explicit cap; fall back to the client RPC timeout."
    int prepare_timeout_ms = 0;
    // Plugin descriptor identifier (spec 0002 FR-9 / task 0010). Carries
    // `com.corridorkey.resolve` for Green nodes and
    // `com.corridorkey.resolve.blue` for Blue nodes so the session broker
    // can include node identity in the cache key. Empty string accepted
    // for backward-compat with v0 clients; a missing identity matches the
    // legacy single-node behavior.
    std::string node_identity;
};

struct HostPluginRuntimeSessionSnapshot {
    std::string session_id;
    std::filesystem::path model_path = {};
    std::string artifact_name;
    DeviceInfo requested_device = {};
    DeviceInfo effective_device = {};
    std::optional<BackendFallbackInfo> backend_fallback;
    int requested_quality_mode = 0;
    int requested_resolution = 0;
    int effective_resolution = 0;
    int recommended_resolution = 0;
    std::uint64_t ref_count = 0;
    bool reused_existing_session = false;
};

struct HostPluginRuntimePrepareSessionResponse {
    HostPluginRuntimeSessionSnapshot session = {};
    std::vector<StageTiming> timings;
};

struct HostPluginRuntimeRenderFrameRequest {
    std::string session_id;
    std::filesystem::path shared_frame_path = {};
    int width = 0;
    int height = 0;
    InferenceParams params = {};
    std::uint64_t render_index = 0;
};

struct HostPluginRuntimeRenderFrameResponse {
    HostPluginRuntimeSessionSnapshot session = {};
    std::vector<StageTiming> timings;
};

struct HostPluginRuntimeReleaseSessionRequest {
    std::string session_id;
};

struct HostPluginRuntimeHealthResponse {
    int server_pid = 0;
    std::uint64_t session_count = 0;
    std::uint64_t active_session_count = 0;
};

struct HostPluginRuntimeShutdownRequest {
    std::string reason;
};

CORRIDORKEY_API std::string host_plugin_runtime_command_to_string(HostPluginRuntimeCommand command);
CORRIDORKEY_API Result<HostPluginRuntimeCommand> host_plugin_runtime_command_from_string(const std::string& value);

CORRIDORKEY_API nlohmann::json to_json(const DeviceInfo& device);
CORRIDORKEY_API Result<DeviceInfo> device_from_json(const nlohmann::json& json);

CORRIDORKEY_API Result<BackendFallbackInfo> backend_fallback_from_json(const nlohmann::json& json);

CORRIDORKEY_API nlohmann::json to_json(const EngineCreateOptions& options);
CORRIDORKEY_API Result<EngineCreateOptions> engine_create_options_from_json(
    const nlohmann::json& json);

CORRIDORKEY_API nlohmann::json to_json(const InferenceParams& params);
CORRIDORKEY_API Result<InferenceParams> inference_params_from_json(const nlohmann::json& json);

CORRIDORKEY_API Result<StageTiming> stage_timing_from_json(const nlohmann::json& json);

CORRIDORKEY_API nlohmann::json to_json(const HostPluginRuntimeRequestEnvelope& envelope);
CORRIDORKEY_API Result<HostPluginRuntimeRequestEnvelope> host_plugin_runtime_request_from_json(
    const nlohmann::json& json);

CORRIDORKEY_API nlohmann::json to_json(const HostPluginRuntimeResponseEnvelope& envelope);
CORRIDORKEY_API Result<HostPluginRuntimeResponseEnvelope> host_plugin_runtime_response_from_json(
    const nlohmann::json& json);

CORRIDORKEY_API nlohmann::json to_json(const HostPluginRuntimePrepareSessionRequest& request);
CORRIDORKEY_API Result<HostPluginRuntimePrepareSessionRequest> prepare_session_request_from_json(
    const nlohmann::json& json);

CORRIDORKEY_API nlohmann::json to_json(const HostPluginRuntimeSessionSnapshot& snapshot);
CORRIDORKEY_API Result<HostPluginRuntimeSessionSnapshot> session_snapshot_from_json(
    const nlohmann::json& json);

CORRIDORKEY_API nlohmann::json to_json(const HostPluginRuntimePrepareSessionResponse& response);
CORRIDORKEY_API Result<HostPluginRuntimePrepareSessionResponse> prepare_session_response_from_json(
    const nlohmann::json& json);

CORRIDORKEY_API nlohmann::json to_json(const HostPluginRuntimeRenderFrameRequest& request);
CORRIDORKEY_API Result<HostPluginRuntimeRenderFrameRequest> render_frame_request_from_json(
    const nlohmann::json& json);

CORRIDORKEY_API nlohmann::json to_json(const HostPluginRuntimeRenderFrameResponse& response);
CORRIDORKEY_API Result<HostPluginRuntimeRenderFrameResponse> render_frame_response_from_json(
    const nlohmann::json& json);

CORRIDORKEY_API nlohmann::json to_json(const HostPluginRuntimeReleaseSessionRequest& request);
CORRIDORKEY_API Result<HostPluginRuntimeReleaseSessionRequest> release_session_request_from_json(
    const nlohmann::json& json);

CORRIDORKEY_API nlohmann::json to_json(const HostPluginRuntimeHealthResponse& response);
CORRIDORKEY_API Result<HostPluginRuntimeHealthResponse> health_response_from_json(
    const nlohmann::json& json);

CORRIDORKEY_API nlohmann::json to_json(const HostPluginRuntimeShutdownRequest& request);
CORRIDORKEY_API Result<HostPluginRuntimeShutdownRequest> shutdown_request_from_json(
    const nlohmann::json& json);

}  // namespace corridorkey::app
