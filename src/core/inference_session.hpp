#pragma once

#include <corridorkey/types.hpp>
#include <filesystem>
#include <memory>

// Include ONNX Runtime (only in private headers)
#include <onnxruntime_cxx_api.h>

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

    DeviceInfo m_device;
    int m_recommended_resolution = 512;

    // Ort handles (RAII)
    Ort::Env m_env{nullptr};
    Ort::Session m_session{nullptr};
    Ort::SessionOptions m_session_options;
};

} // namespace corridorkey
