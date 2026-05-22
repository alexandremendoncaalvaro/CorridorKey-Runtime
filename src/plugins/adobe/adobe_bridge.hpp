#pragma once

#include <corridorkey/engine.hpp>
#include <corridorkey/types.hpp>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

#include "app/host_plugin_runtime_client.hpp"
#include "app/host_plugin_runtime_protocol.hpp"

namespace corridorkey::adobe {

enum class AdobePixelFormat : std::uint8_t {
    Argb32,
    Argb64,
    Argb128,
    Bgra32,
};

struct AdobeFrameView {
    const void* data = nullptr;
    std::size_t data_size_bytes = 0;
    int width = 0;
    int height = 0;
    int row_bytes = 0;
    AdobePixelFormat pixel_format = AdobePixelFormat::Argb32;
};

struct AdobeRuntimeFrame {
    ImageBuffer rgb;
    ImageBuffer alpha_hint;
};

struct AdobePrepareSessionOptions {
    std::string host_surface;
    std::string effect_identity;
    std::string client_instance_id;
    std::filesystem::path model_path = {};
    DeviceInfo requested_device = {};
    EngineCreateOptions engine_options = {};
    int requested_quality_mode = 0;
    int requested_resolution = 0;
    int effective_resolution = 0;
    int prepare_timeout_ms = 0;
};

Result<AdobeRuntimeFrame> copy_adobe_frame_to_runtime(const AdobeFrameView& frame);

Result<app::HostPluginRuntimePrepareSessionRequest> build_adobe_prepare_session_request(
    const AdobePrepareSessionOptions& options);

class AdobeRuntimeBridge {
   public:
    explicit AdobeRuntimeBridge(std::unique_ptr<app::HostPluginRuntimeClient> runtime_client);

    Result<app::HostPluginRuntimeHealthResponse> health();
    Result<app::HostPluginRuntimePrepareSessionResponse> prepare_session(
        const AdobePrepareSessionOptions& options, StageTimingCallback on_stage = nullptr);
    Result<FrameResult> process_frame(const AdobeFrameView& frame, const InferenceParams& params,
                                      std::uint64_t render_index,
                                      StageTimingCallback on_stage = nullptr);
    Result<void> release_session();

   private:
    std::unique_ptr<app::HostPluginRuntimeClient> m_runtime_client;
};

}  // namespace corridorkey::adobe
