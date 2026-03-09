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

            // Get Input Shapes
            auto type_info = session_ptr->m_session.GetInputTypeInfo(i);
            auto tensor_info = type_info.GetTensorTypeAndShapeInfo();
            session_ptr->m_input_node_dims.push_back(tensor_info.GetShape());
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

Result<std::vector<Ort::Value>> InferenceSession::prepare_input_tensors(
    const Image& rgb, 
    const Image& alpha_hint,
    Ort::MemoryInfo& memory_info
) {
    if (m_input_node_dims.empty()) {
        return std::unexpected(Error{ ErrorCode::InvalidParameters, "InferenceSession metadata is empty" });
    }

    try {
        std::vector<Ort::Value> input_tensors;
        
        // Assume RGB is Input 0 and AlphaHint is Input 1 (based on CorridorKey model structure)
        for (size_t i = 0; i < m_input_node_dims.size(); ++i) {
            auto shape = m_input_node_dims[i];
            int64_t model_h = shape[2];
            int64_t model_w = shape[3];
            int64_t model_c = shape[1];

            const Image& source = (i == 0) ? rgb : alpha_hint;
            
            // 1. Resize if needed
            Image resized = (source.width != (int)model_w || source.height != (int)model_h) 
                           ? ColorUtils::resize(source, (int)model_w, (int)model_h)
                           : source;

            // 2. Convert HWC to NCHW and Normalize [0, 1]
            std::vector<float> planar_data(model_c * model_h * model_w);
            for (int c = 0; c < model_c; ++c) {
                for (int y = 0; y < model_h; ++y) {
                    for (int x = 0; x < model_w; ++x) {
                        int src_idx = (y * (int)model_w + x) * (int)source.channels + (c < (int)source.channels ? c : 0);
                        int dst_idx = c * (int)model_h * (int)model_w + y * (int)model_w + x;
                        planar_data[dst_idx] = resized.data[src_idx]; // Image is already float
                    }
                }
            }

            // 3. Create Tensor
            input_tensors.push_back(Ort::Value::CreateTensor<float>(
                memory_info, 
                planar_data.data(), planar_data.size(), 
                shape.data(), shape.size()
            ));

            // IMPORTANT: planar_data is local! Ort::Value::CreateTensor with pointers 
            // is non-owning by default. For a real production run, we need to manage 
            // the buffer lifetime.
            // TODO: Refactor to maintain tensor buffer lifetime.
        }

        return input_tensors;
    } catch (const std::exception& e) {
        return std::unexpected(Error{ ErrorCode::InferenceFailed, std::string("Tensor preparation failed: ") + e.what() });
    }
}

} // namespace corridorkey
