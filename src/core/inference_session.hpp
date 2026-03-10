#pragma once

#include <corridorkey/types.hpp>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

// Include ONNX Runtime (only in private headers)
#include <onnxruntime/onnxruntime_cxx_api.h>

#ifdef __APPLE__
#include <onnxruntime/coreml_provider_factory.h>
#endif

namespace corridorkey {

/**
 * @brief Private wrapper for an ONNX Runtime session.
 * This class isolates Ort types from the rest of the core.
 */
class InferenceSession {
   public:
    static Result<std::unique_ptr<InferenceSession>> create(const std::filesystem::path& model_path,
                                                            DeviceInfo device);

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
    explicit InferenceSession(DeviceInfo device);

    void configure_session_options(bool use_optimized_model_cache = false);
    void extract_metadata();

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
    void apply_post_process(FrameResult& result, const InferenceParams& params,
                            StageTimingCallback on_stage = nullptr);

    /**
     * @brief Helper for running tiling inference on large images.
     */
    [[nodiscard]] Result<FrameResult> run_tiled(const Image& rgb, const Image& alpha_hint,
                                                const InferenceParams& params, int model_res,
                                                StageTimingCallback on_stage = nullptr);

    DeviceInfo m_device;
    int m_recommended_resolution = 512;

    // Ort handles (RAII)
    Ort::Env m_env{nullptr};
    Ort::Session m_session{nullptr};
    Ort::SessionOptions m_session_options;

    // Input/Output metadata
    std::vector<std::string> m_input_node_names = {};
    std::vector<std::string> m_output_node_names = {};
    std::vector<const char*> m_input_node_names_ptr = {};
    std::vector<const char*> m_output_node_names_ptr = {};
    std::vector<std::vector<int64_t>> m_input_node_dims = {};

    // Pre-allocated buffer pools (reused across run() calls)
    std::vector<ImageBuffer> m_resize_pool = {};
    std::vector<ImageBuffer> m_planar_pool = {};
    std::vector<ImageBuffer> m_tiled_rgb_pool = {};
    std::vector<ImageBuffer> m_tiled_hint_pool = {};
    ImageBuffer m_tiled_weight_mask = {};
    int m_tiled_buffer_size = 0;
    int m_tiled_pool_capacity = 0;
    int m_tiled_weight_padding = -1;
};

}  // namespace corridorkey
