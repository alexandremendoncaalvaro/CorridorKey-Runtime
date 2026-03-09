#include <src/core/inference_session.hpp>
#include <iostream>

namespace corridorkey {

InferenceSession::InferenceSession(DeviceInfo device)
    : m_device(std::move(device)) {}

InferenceSession::~InferenceSession() = default;

Result<std::unique_ptr<InferenceSession>> InferenceSession::create(
    const std::filesystem::path& model_path,
    DeviceInfo device
) {
    if (!std::filesystem::exists(model_path)) {
        return std::unexpected(Error{ ErrorCode::ModelLoadFailed, "Model file not found: " + model_path.string() });
    }

    try {
        auto session = std::unique_ptr<InferenceSession>(new InferenceSession(device));

        // Initialize Ort Environment
        session->m_env = Ort::Env(ORT_LOGGING_LEVEL_WARNING, "CorridorKey");

        // Set Session Options (Execution Providers)
        // TODO: Map Backend enum to Ort Execution Providers (CoreML, TensorRT, etc.)
        
        // Load and create the session
#ifdef _WIN32
        session->m_session = Ort::Session(session->m_env, model_path.c_str(), session->m_session_options);
#else
        session->m_session = Ort::Session(session->m_env, model_path.c_str(), session->m_session_options);
#endif

        return session;
    } catch (const Ort::Exception& e) {
        return std::unexpected(Error{ ErrorCode::ModelLoadFailed, std::string("ONNX Runtime session creation failed: ") + e.what() });
    } catch (const std::exception& e) {
        return std::unexpected(Error{ ErrorCode::ModelLoadFailed, std::string("Failed to initialize session: ") + e.what() });
    }
}

Result<FrameResult> InferenceSession::run(
    const Image& rgb, 
    const Image& alpha_hint,
    const InferenceParams& params
) {
    (void)rgb;
    (void)alpha_hint;
    (void)params;

    // TODO: Implement actual inference logic (preprocessing, run, postprocessing)
    return std::unexpected(Error{ ErrorCode::InferenceFailed, "Inference run not yet implemented" });
}

} // namespace corridorkey
