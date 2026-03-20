#pragma once

#include <corridorkey/engine.hpp>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

namespace corridorkey::app {

inline constexpr int kOfxRuntimeProtocolVersion = 1;

enum class OfxRuntimeCommand : std::uint8_t {
    Health,
    PrepareSession,
    RenderFrame,
    ReleaseSession,
    Shutdown,
};

struct OfxRuntimeRequestEnvelope {
    int protocol_version = kOfxRuntimeProtocolVersion;
    OfxRuntimeCommand command = OfxRuntimeCommand::Health;
    nlohmann::json payload = nlohmann::json::object();
};

struct OfxRuntimeResponseEnvelope {
    int protocol_version = kOfxRuntimeProtocolVersion;
    bool success = false;
    std::string error = "";
    nlohmann::json payload = nlohmann::json::object();
};

struct OfxRuntimePrepareSessionRequest {
    std::string client_instance_id = "";
    std::filesystem::path model_path = {};
    std::string artifact_name = "";
    DeviceInfo requested_device = {};
    EngineCreateOptions engine_options = {};
    int requested_quality_mode = 0;
    int requested_resolution = 0;
    int effective_resolution = 0;
};

struct OfxRuntimeSessionSnapshot {
    std::string session_id = "";
    std::filesystem::path model_path = {};
    std::string artifact_name = "";
    DeviceInfo requested_device = {};
    DeviceInfo effective_device = {};
    std::optional<BackendFallbackInfo> backend_fallback = std::nullopt;
    int requested_quality_mode = 0;
    int requested_resolution = 0;
    int effective_resolution = 0;
    int recommended_resolution = 0;
    std::uint64_t ref_count = 0;
    bool reused_existing_session = false;
};

struct OfxRuntimePrepareSessionResponse {
    OfxRuntimeSessionSnapshot session = {};
    std::vector<StageTiming> timings = {};
};

struct OfxRuntimeRenderFrameRequest {
    std::string session_id = "";
    std::filesystem::path shared_frame_path = {};
    int width = 0;
    int height = 0;
    InferenceParams params = {};
    std::uint64_t render_index = 0;
};

struct OfxRuntimeRenderFrameResponse {
    OfxRuntimeSessionSnapshot session = {};
    std::vector<StageTiming> timings = {};
};

struct OfxRuntimeReleaseSessionRequest {
    std::string session_id = "";
};

struct OfxRuntimeHealthResponse {
    int server_pid = 0;
    std::uint64_t session_count = 0;
    std::uint64_t active_session_count = 0;
};

struct OfxRuntimeShutdownRequest {
    std::string reason = "";
};

std::string ofx_runtime_command_to_string(OfxRuntimeCommand command);
Result<OfxRuntimeCommand> ofx_runtime_command_from_string(const std::string& value);

nlohmann::json to_json(const DeviceInfo& device);
Result<DeviceInfo> device_from_json(const nlohmann::json& json);

Result<BackendFallbackInfo> backend_fallback_from_json(const nlohmann::json& json);

nlohmann::json to_json(const EngineCreateOptions& options);
Result<EngineCreateOptions> engine_create_options_from_json(const nlohmann::json& json);

nlohmann::json to_json(const InferenceParams& params);
Result<InferenceParams> inference_params_from_json(const nlohmann::json& json);

Result<StageTiming> stage_timing_from_json(const nlohmann::json& json);

nlohmann::json to_json(const OfxRuntimeRequestEnvelope& envelope);
Result<OfxRuntimeRequestEnvelope> ofx_runtime_request_from_json(const nlohmann::json& json);

nlohmann::json to_json(const OfxRuntimeResponseEnvelope& envelope);
Result<OfxRuntimeResponseEnvelope> ofx_runtime_response_from_json(const nlohmann::json& json);

nlohmann::json to_json(const OfxRuntimePrepareSessionRequest& request);
Result<OfxRuntimePrepareSessionRequest> prepare_session_request_from_json(
    const nlohmann::json& json);

nlohmann::json to_json(const OfxRuntimeSessionSnapshot& snapshot);
Result<OfxRuntimeSessionSnapshot> session_snapshot_from_json(const nlohmann::json& json);

nlohmann::json to_json(const OfxRuntimePrepareSessionResponse& response);
Result<OfxRuntimePrepareSessionResponse> prepare_session_response_from_json(
    const nlohmann::json& json);

nlohmann::json to_json(const OfxRuntimeRenderFrameRequest& request);
Result<OfxRuntimeRenderFrameRequest> render_frame_request_from_json(const nlohmann::json& json);

nlohmann::json to_json(const OfxRuntimeRenderFrameResponse& response);
Result<OfxRuntimeRenderFrameResponse> render_frame_response_from_json(const nlohmann::json& json);

nlohmann::json to_json(const OfxRuntimeReleaseSessionRequest& request);
Result<OfxRuntimeReleaseSessionRequest> release_session_request_from_json(
    const nlohmann::json& json);

nlohmann::json to_json(const OfxRuntimeHealthResponse& response);
Result<OfxRuntimeHealthResponse> health_response_from_json(const nlohmann::json& json);

nlohmann::json to_json(const OfxRuntimeShutdownRequest& request);
Result<OfxRuntimeShutdownRequest> shutdown_request_from_json(const nlohmann::json& json);

}  // namespace corridorkey::app
