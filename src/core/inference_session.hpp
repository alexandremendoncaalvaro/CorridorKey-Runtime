#pragma once

#include <corridorkey/types.hpp>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "post_process/alpha_edge.hpp"
#include "post_process/color_utils.hpp"
#include "post_process/despeckle.hpp"

namespace corridorkey {

namespace core {
class MlxSession;
class TorchTrtSession;
}  // namespace core

struct SessionCreateOptions {
    bool disable_cpu_ep_fallback = false;
    ExecutionEngine execution_engine = ExecutionEngine::Auto;
};

/**
 * @brief Private wrapper for packaged inference sessions on the active backend.
 * This class isolates backend-specific runtime types from the rest of the core.
 */
class InferenceSession {
   public:
    static Result<std::unique_ptr<InferenceSession>> create(const std::filesystem::path& model_path,
                                                            DeviceInfo device,
                                                            SessionCreateOptions options = {});

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
    [[nodiscard]] ExecutionEngine execution_engine() const {
        return m_execution_engine;
    }
    [[nodiscard]] int recommended_resolution() const {
        return m_recommended_resolution;
    }

   private:
    explicit InferenceSession(DeviceInfo device);

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

    std::unique_ptr<core::MlxSession> m_mlx_session = nullptr;
    std::unique_ptr<core::TorchTrtSession> m_torch_trt_session = nullptr;
    ExecutionEngine m_execution_engine = ExecutionEngine::Auto;

    // Pre-allocated buffer pools (reused across run() calls)
    std::vector<ImageBuffer> m_tiled_rgb_pool = {};
    std::vector<ImageBuffer> m_tiled_hint_pool = {};
    ImageBuffer m_tiled_weight_mask = {};
    int m_tiled_buffer_size = 0;
    int m_tiled_pool_capacity = 0;
    int m_tiled_weight_padding = -1;

    DespeckleState m_despeckle_state = {};
    ColorUtils::State m_color_utils_state = {};
    AlphaEdgeState m_alpha_edge_state = {};
};

}  // namespace corridorkey
