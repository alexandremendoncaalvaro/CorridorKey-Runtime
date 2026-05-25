#pragma once

#include <corridorkey/engine.hpp>
#include <corridorkey/types.hpp>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

#include "adobe_matte_params.hpp"
#include "app/host_plugin_runtime_client.hpp"
#include "app/host_plugin_runtime_protocol.hpp"
#include "post_process/alpha_edge.hpp"
#include "post_process/screen_color.hpp"

namespace corridorkey::adobe {

enum class AdobePixelFormat : std::uint8_t {
    Argb32,
    Argb64,
    Argb128,
    Bgra32,
};

enum class AdobeAlphaHintSource : std::uint8_t {
    SourceAlpha,
    ExternalLayerAlpha,
    ExternalLayerRed,
    RoughFallback,
};

struct AdobeFrameView {
    const void* data = nullptr;
    std::size_t data_size_bytes = 0;
    int width = 0;
    int height = 0;
    int row_bytes = 0;
    AdobePixelFormat pixel_format = AdobePixelFormat::Argb32;
};

struct AdobeMutableFrameView {
    void* data = nullptr;
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
    std::string node_identity;
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
void apply_adobe_input_color_space(AdobeRuntimeFrame& frame, bool input_is_linear);
ScreenColorTransform canonicalize_adobe_runtime_frame_for_screen_color(AdobeRuntimeFrame& frame,
                                                                       ScreenColorMode mode);
void apply_adobe_matte_params(FrameResult& result, const AdobeMatteParams& params, int width,
                              int height, AlphaEdgeState& state);
Result<AdobeAlphaHintSource> resolve_alpha_hint_source(
    AdobeRuntimeFrame& frame, const AdobeFrameView* external_alpha_hint_frame,
    AlphaHintPolicy alpha_hint_policy, bool input_is_linear = false);
std::string_view adobe_alpha_hint_source_label(AdobeAlphaHintSource source) noexcept;
Result<void> copy_runtime_result_to_adobe_frame(const FrameResult& result,
                                                const AdobeMutableFrameView& output_frame,
                                                int output_mode,
                                                const AdobeRuntimeFrame* source_frame = nullptr);

Result<app::HostPluginRuntimePrepareSessionRequest> build_adobe_prepare_session_request(
    const AdobePrepareSessionOptions& options);

class AdobeRuntimeBridge {
   public:
    explicit AdobeRuntimeBridge(std::unique_ptr<app::HostPluginRuntimeClient> runtime_client);
    explicit AdobeRuntimeBridge(std::shared_ptr<app::HostPluginRuntimeClient> runtime_client);

    Result<app::HostPluginRuntimeHealthResponse> health();
    Result<app::HostPluginRuntimePrepareSessionResponse> prepare_session(
        const AdobePrepareSessionOptions& options, StageTimingCallback on_stage = nullptr);
    Result<FrameResult> process_frame(const AdobeFrameView& frame, const InferenceParams& params,
                                      std::uint64_t render_index,
                                      StageTimingCallback on_stage = nullptr);
    Result<FrameResult> process_frame(const AdobeRuntimeFrame& frame, const InferenceParams& params,
                                      std::uint64_t render_index,
                                      StageTimingCallback on_stage = nullptr);
    Result<void> release_session();

   private:
    std::shared_ptr<app::HostPluginRuntimeClient> m_runtime_client;
};

}  // namespace corridorkey::adobe
