#pragma once

#include <corridorkey/types.hpp>
#include <filesystem>
#include <memory>

// Include ONNX Runtime (only in private headers)
#include <onnxruntime/onnxruntime_cxx_api.h>

namespace corridorkey {

/**
 * @brief Private wrapper for an ONNX Runtime session.
 * This class isolates Ort types from the rest of the core.
 */
class InferenceSession {
public:
    static Result<std::unique_ptr<InferenceSession>> create(
        const std::filesystem::path& model_path,
        DeviceInfo device
    );

    ~InferenceSession();

    // Disable copy, allow move
    InferenceSession(const InferenceSession&) = delete;
    InferenceSession& operator=(const InferenceSession&) = delete;
    InferenceSession(InferenceSession&&) noexcept = default;
    InferenceSession& operator=(InferenceSession&&) noexcept = default;

    /**
     * @brief Run inference on a frame.
     */
    Result<FrameResult> run(
        const Image& rgb,
        const Image& alpha_hint,
        const InferenceParams& params
    );

    DeviceInfo device() const { return m_device; }
    int recommended_resolution() const { return m_recommended_resolution; }

private:
    InferenceSession(DeviceInfo device);

    void configure_session_options();
    void extract_metadata();

    /**
     * @brief Helper for running tiling inference on large images.
     */
    Result<FrameResult> run_tiled(
        const Image& rgb,
        const Image& alpha_hint,
        const InferenceParams& params,
        int model_res
    );

    DeviceInfo m_device;
    int m_recommended_resolution = 512;

    // Ort handles (RAII)
    Ort::Env m_env{nullptr};
    Ort::Session m_session{nullptr};
    Ort::SessionOptions m_session_options;

    // Input/Output metadata
    std::vector<std::string> m_input_node_names;
    std::vector<std::string> m_output_node_names;
    std::vector<const char*> m_input_node_names_ptr;
    std::vector<const char*> m_output_node_names_ptr;
    std::vector<std::vector<int64_t>> m_input_node_dims;

    // Pre-allocated buffer pools (reused across run() calls)
    std::vector<ImageBuffer> m_resize_pool;
    std::vector<ImageBuffer> m_planar_pool;
};

} // namespace corridorkey
