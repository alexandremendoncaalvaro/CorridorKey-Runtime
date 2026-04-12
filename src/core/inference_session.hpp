#pragma once

#include <corridorkey/types.hpp>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

// Keep the C++ wrapper aligned with the provider header layout for each platform.
// The curated Windows RTX package only ships the core/session layout; falling back
// to the vcpkg top-level wrapper alongside vendor provider headers causes duplicate
// ONNX Runtime type definitions during compilation.
#if defined(_WIN32)
#if __has_include(<onnxruntime/core/session/onnxruntime_cxx_api.h>)
#include <onnxruntime/core/session/onnxruntime_cxx_api.h>
#elif __has_include(<onnxruntime/onnxruntime_cxx_api.h>)
#include <onnxruntime/onnxruntime_cxx_api.h>
#else
#error "ONNX Runtime C++ headers not found"
#endif
#else
#if __has_include(<onnxruntime/onnxruntime_cxx_api.h>)
#include <onnxruntime/onnxruntime_cxx_api.h>
#elif __has_include(<onnxruntime/core/session/onnxruntime_cxx_api.h>)
#include <onnxruntime/core/session/onnxruntime_cxx_api.h>
#else
#error "ONNX Runtime C++ headers not found"
#endif
#endif

#include "post_process/alpha_edge.hpp"
#include "post_process/color_utils.hpp"
#include "post_process/despeckle.hpp"
#include "core/gpu_prep.hpp"
#include "core/gpu_resize.hpp"

#ifdef __APPLE__
#if __has_include(<onnxruntime/coreml_provider_factory.h>)
#include <onnxruntime/coreml_provider_factory.h>
#else
#include <onnxruntime/core/providers/coreml/coreml_provider_factory.h>
#endif
#endif

namespace corridorkey {

namespace core {
class MlxSession;
class OrtProcessContext;
}

struct SessionCreateOptions {
    bool disable_cpu_ep_fallback = false;
    OrtLoggingLevel log_severity = ORT_LOGGING_LEVEL_ERROR;
    std::shared_ptr<core::OrtProcessContext> ort_process_context = nullptr;
};

/**
 * @brief Private wrapper for an ONNX Runtime session.
 * This class isolates Ort types from the rest of the core.
 */
class InferenceSession {
   public:
    static Result<std::unique_ptr<InferenceSession>> create(const std::filesystem::path& model_path,
                                                            DeviceInfo device,
                                                            SessionCreateOptions options = {},
                                                            StageTimingCallback on_stage = nullptr);

    ~InferenceSession();

    // Disable copy, allow move
    InferenceSession(const InferenceSession&) = delete;
    InferenceSession& operator=(const InferenceSession&) = delete;
    InferenceSession(InferenceSession&&) noexcept = default;
    InferenceSession& operator=(InferenceSession&&) noexcept = default;

    /**
     * @brief Run inference on a frame.
     */
    [[nodiscard]] Result<FrameResult> run(const Image& rgb, const Image& alpha_hint,
                                          const InferenceParams& params,
                                          StageTimingCallback on_stage = nullptr);

    /**
     * @brief Run inference on a batch of frames.
     */
    [[nodiscard]] Result<std::vector<FrameResult>> run_batch(
        const std::vector<Image>& rgbs, const std::vector<Image>& alpha_hints,
        const InferenceParams& params, StageTimingCallback on_stage = nullptr);

    [[nodiscard]] DeviceInfo device() const {
        return m_device;
    }
    [[nodiscard]] int recommended_resolution() const {
        return m_recommended_resolution;
    }

   private:
    struct BoundIoState;

    explicit InferenceSession(DeviceInfo device);

    void configure_session_options(bool use_optimized_model_cache,
                                   const SessionCreateOptions& options,
                                   const std::filesystem::path& model_path);
    void extract_metadata(const std::filesystem::path& model_path);
    [[nodiscard]] Result<Ort::Value> create_input_tensor(float* planar_data,
                                                         std::size_t element_count,
                                                         const std::vector<int64_t>& shape);
    [[nodiscard]] Result<BoundIoState*> ensure_bound_io_state(
        const std::vector<int64_t>& input_shape);

    /**
     * @brief Internal raw inference (no post-processing).
     */
    [[nodiscard]] Result<FrameResult> infer_raw(const Image& rgb, const Image& alpha_hint,
                                                const InferenceParams& params,
                                                StageTimingCallback on_stage = nullptr);

    /**
     * @brief Internal raw inference on a batch.
     */
    [[nodiscard]] Result<std::vector<FrameResult>> infer_batch_raw(
        const std::vector<Image>& rgbs, const std::vector<Image>& alpha_hints,
        const InferenceParams& params, StageTimingCallback on_stage = nullptr);

    /**
     * @brief Apply despeckle, despill and composition to raw results.
     */
    void apply_post_process(FrameResult& result, const InferenceParams& params, Image source_rgb,
                            StageTimingCallback on_stage = nullptr);

    [[nodiscard]] Result<FrameResult> run_direct(const Image& rgb, const Image& alpha_hint,
                                                 const InferenceParams& params,
                                                 StageTimingCallback on_stage = nullptr);

    [[nodiscard]] Result<FrameResult> run_coarse_to_fine(const Image& rgb, const Image& alpha_hint,
                                                         const InferenceParams& params,
                                                         StageTimingCallback on_stage = nullptr);

    /**
     * @brief Helper for running tiling inference on large images.
     */
    [[nodiscard]] Result<FrameResult> run_tiled(const Image& rgb, const Image& alpha_hint,
                                                const InferenceParams& params, int model_res,
                                                StageTimingCallback on_stage = nullptr);

    DeviceInfo m_device;
    int m_recommended_resolution = 512;

    Ort::Session m_session{nullptr};
    Ort::SessionOptions m_session_options;

    // Input/Output metadata
    std::vector<std::string> m_input_node_names = {};
    std::vector<std::string> m_output_node_names = {};
    std::vector<const char*> m_input_node_names_ptr = {};
    std::vector<const char*> m_output_node_names_ptr = {};
    std::vector<std::vector<int64_t>> m_input_node_dims = {};
    std::vector<std::vector<int64_t>> m_output_node_dims = {};
    ONNXTensorElementDataType m_input_element_type = ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT;
    std::vector<ONNXTensorElementDataType> m_output_element_types = {};
    std::unique_ptr<core::MlxSession> m_mlx_session = nullptr;
    std::shared_ptr<core::OrtProcessContext> m_ort_process_context = nullptr;
    std::unique_ptr<BoundIoState> m_bound_io_state = nullptr;
    bool m_io_binding_enabled = false;

    // Pre-allocated buffer pools (reused across run() calls)
    std::vector<ImageBuffer> m_resize_pool = {};
    std::vector<ImageBuffer> m_planar_pool = {};
    std::vector<Ort::Float16_t> m_fp16_pool = {};
    std::vector<ImageBuffer> m_tiled_rgb_pool = {};
    std::vector<ImageBuffer> m_tiled_hint_pool = {};
    ImageBuffer m_tiled_weight_mask = {};
    int m_tiled_buffer_size = 0;
    int m_tiled_pool_capacity = 0;
    int m_tiled_weight_padding = -1;

    DespeckleState m_despeckle_state = {};
    ColorUtils::State m_color_utils_state = {};
    AlphaEdgeState m_alpha_edge_state = {};

    core::GpuInputPrep m_gpu_prep;
    core::GpuResizer m_gpu_resizer;
};

}  // namespace corridorkey
