#include <string>
#include <string_view>
#include <utility>

#include "adobe_bridge.hpp"

namespace corridorkey::adobe {
namespace {

constexpr int kMinAdobeQualityMode = 0;
constexpr int kMaxAdobeQualityMode = 4;
constexpr int kMaxAdobePrepareResolution = 2048;
constexpr int kMaxAdobePrepareTimeoutMs = 600000;

std::string encode_identity_component(std::string_view value) {
    std::string encoded;
    encoded.reserve(value.size());
    for (const char character : value) {
        if (character == '%') {
            encoded += "%25";
        } else if (character == ':') {
            encoded += "%3A";
        } else {
            encoded.push_back(character);
        }
    }
    return encoded;
}

std::string adobe_session_identity(const AdobePrepareSessionOptions& options) {
    return "adobe:" + encode_identity_component(options.host_surface) + ":" +
           encode_identity_component(options.effect_identity);
}

Result<void> validate_range(int value, int minimum, int maximum, std::string_view label) {
    if (value < minimum || value > maximum) {
        return Unexpected<Error>(
            Error{.code = ErrorCode::InvalidParameters,
                  .message = "Adobe " + std::string(label) + " is out of range."});
    }
    return {};
}

Result<void> validate_prepare_options(const AdobePrepareSessionOptions& options) {
    if (options.host_surface.empty()) {
        return Unexpected<Error>(
            Error{ErrorCode::InvalidParameters, "Adobe host surface is required."});
    }
    if (options.effect_identity.empty()) {
        return Unexpected<Error>(
            Error{ErrorCode::InvalidParameters, "Adobe effect identity is required."});
    }
    if (options.model_path.empty()) {
        return Unexpected<Error>(
            Error{ErrorCode::InvalidParameters, "Adobe model path is required."});
    }
    auto quality = validate_range(options.requested_quality_mode, kMinAdobeQualityMode,
                                  kMaxAdobeQualityMode, "quality mode");
    if (!quality) {
        return Unexpected<Error>(quality.error());
    }
    auto requested_resolution = validate_range(options.requested_resolution, 0,
                                               kMaxAdobePrepareResolution, "requested resolution");
    if (!requested_resolution) {
        return Unexpected<Error>(requested_resolution.error());
    }
    auto effective_resolution = validate_range(options.effective_resolution, 0,
                                               kMaxAdobePrepareResolution, "effective resolution");
    if (!effective_resolution) {
        return Unexpected<Error>(effective_resolution.error());
    }
    auto prepare_timeout =
        validate_range(options.prepare_timeout_ms, 0, kMaxAdobePrepareTimeoutMs, "prepare timeout");
    if (!prepare_timeout) {
        return Unexpected<Error>(prepare_timeout.error());
    }
    return {};
}

template <typename T>
Result<T> missing_runtime_client_result() {
    return Unexpected<Error>(
        Error{ErrorCode::InvalidParameters, "Adobe runtime bridge has no runtime client."});
}

}  // namespace

Result<app::HostPluginRuntimePrepareSessionRequest> build_adobe_prepare_session_request(
    const AdobePrepareSessionOptions& options) {
    auto validation = validate_prepare_options(options);
    if (!validation) {
        return Unexpected<Error>(validation.error());
    }

    const std::string identity = adobe_session_identity(options);
    app::HostPluginRuntimePrepareSessionRequest request;
    request.client_instance_id = identity;
    if (!options.client_instance_id.empty()) {
        request.client_instance_id += ":" + encode_identity_component(options.client_instance_id);
    }
    request.model_path = options.model_path;
    request.artifact_name = options.model_path.filename().string();
    request.requested_device = options.requested_device;
    request.engine_options = options.engine_options;
    request.requested_quality_mode = options.requested_quality_mode;
    request.requested_resolution = options.requested_resolution;
    request.effective_resolution = options.effective_resolution;
    request.prepare_timeout_ms = options.prepare_timeout_ms;
    request.node_identity = identity;
    return request;
}

AdobeRuntimeBridge::AdobeRuntimeBridge(std::unique_ptr<app::HostPluginRuntimeClient> runtime_client)
    : m_runtime_client(std::move(runtime_client)) {}

Result<app::HostPluginRuntimeHealthResponse> AdobeRuntimeBridge::health() {
    if (m_runtime_client == nullptr) {
        return missing_runtime_client_result<app::HostPluginRuntimeHealthResponse>();
    }
    return m_runtime_client->health();
}

Result<app::HostPluginRuntimePrepareSessionResponse> AdobeRuntimeBridge::prepare_session(
    const AdobePrepareSessionOptions& options, StageTimingCallback on_stage) {
    if (m_runtime_client == nullptr) {
        return missing_runtime_client_result<app::HostPluginRuntimePrepareSessionResponse>();
    }
    auto request = build_adobe_prepare_session_request(options);
    if (!request) {
        return Unexpected<Error>(request.error());
    }
    return m_runtime_client->prepare_session(*request, std::move(on_stage));
}

Result<FrameResult> AdobeRuntimeBridge::process_frame(const AdobeFrameView& frame,
                                                      const InferenceParams& params,
                                                      std::uint64_t render_index,
                                                      StageTimingCallback on_stage) {
    if (m_runtime_client == nullptr) {
        return missing_runtime_client_result<FrameResult>();
    }
    auto runtime_frame = copy_adobe_frame_to_runtime(frame);
    if (!runtime_frame) {
        return Unexpected<Error>(runtime_frame.error());
    }
    auto rgb = runtime_frame->rgb.view();
    auto alpha_hint = runtime_frame->alpha_hint.view();
    return m_runtime_client->process_frame(rgb, alpha_hint, params, render_index,
                                           std::move(on_stage));
}

Result<void> AdobeRuntimeBridge::release_session() {
    if (m_runtime_client == nullptr) {
        return Unexpected<Error>(
            Error{ErrorCode::InvalidParameters, "Adobe runtime bridge has no runtime client."});
    }
    return m_runtime_client->release_session();
}

}  // namespace corridorkey::adobe
