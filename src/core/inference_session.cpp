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
        auto session_ptr = std::unique_ptr<InferenceSession>(new InferenceSession(device));

        // Initialize Ort Environment
        session_ptr->m_env = Ort::Env(ORT_LOGGING_LEVEL_WARNING, "CorridorKey");

        // Set Session Options
        session_ptr->m_session_options.SetIntraOpNumThreads(1);
        session_ptr->m_session_options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

        // Mapped Backend to Execution Provider (Skeleton)
        switch (device.backend) {
            case Backend::CPU:
                // CPU is always available
                break;
            default:
                // Fallback to CPU for now
                break;
        }
        
        // Load and create the session
        // Note: Ort::Session expects wchar_t* on Windows
#ifdef _WIN32
        session_ptr->m_session = Ort::Session(session_ptr->m_env, model_path.wstring().c_str(), session_ptr->m_session_options);
#else
        session_ptr->m_session = Ort::Session(session_ptr->m_env, model_path.c_str(), session_ptr->m_session_options);
#endif

        // Get Input and Output metadata
        Ort::AllocatorWithDefaultOptions allocator;
        
        // Inputs
        size_t num_input_nodes = session_ptr->m_session.GetInputCount();
        for (size_t i = 0; i < num_input_nodes; i++) {
            auto input_name_ptr = session_ptr->m_session.GetInputNameAllocated(i, allocator);
            session_ptr->m_input_node_names.push_back(input_name_ptr.get());
        }
        for (const auto& name : session_ptr->m_input_node_names) {
            session_ptr->m_input_node_names_ptr.push_back(name.c_str());
        }

        // Outputs
        size_t num_output_nodes = session_ptr->m_session.GetOutputCount();
        for (size_t i = 0; i < num_output_nodes; i++) {
            auto output_name_ptr = session_ptr->m_session.GetOutputNameAllocated(i, allocator);
            session_ptr->m_output_node_names.push_back(output_name_ptr.get());
        }
        for (const auto& name : session_ptr->m_output_node_names) {
            session_ptr->m_output_node_names_ptr.push_back(name.c_str());
        }

        return session_ptr;
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
