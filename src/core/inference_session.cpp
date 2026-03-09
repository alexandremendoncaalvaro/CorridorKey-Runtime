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
    (void)params;

    try {
        Ort::MemoryInfo memory_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);

        // 1. Prepare Inputs
        std::vector<std::vector<float>> input_buffers;
        std::vector<Ort::Value> input_tensors;

        for (size_t i = 0; i < m_input_node_dims.size(); ++i) {
            auto shape = m_input_node_dims[i];
            const Image& source = (i == 0) ? rgb : alpha_hint;
            
            // Resize if model expects different resolution
            Image resized = (source.width != (int)shape[3] || source.height != (int)shape[2]) 
                           ? ColorUtils::resize(source, (int)shape[3], (int)shape[2])
                           : source;

            // Convert HWC interleaved to NCHW planar
            input_buffers.emplace_back(shape[1] * shape[2] * shape[3]);
            ColorUtils::to_planar(resized, input_buffers.back().data());
            
            input_tensors.push_back(Ort::Value::CreateTensor<float>(
                memory_info, 
                input_buffers.back().data(), input_buffers.back().size(), 
                shape.data(), shape.size()
            ));
        }

        // 2. Run Inference
        auto output_tensors = m_session.Run(
            Ort::RunOptions{nullptr},
            m_input_node_names_ptr.data(),
            input_tensors.data(),
            input_tensors.size(),
            m_output_node_names_ptr.data(),
            m_output_node_names_ptr.size()
        );

        if (output_tensors.empty()) {
            return std::unexpected(Error{ ErrorCode::InferenceFailed, "Model produced no output tensors" });
        }

        // 3. Process Outputs (assume output 0 is Alpha Matte)
        auto& raw_output = output_tensors[0];
        auto shape = raw_output.GetTensorTypeAndShapeInfo().GetShape();
        
        FrameResult result;
        result.alpha.width = (int)shape[3];
        result.alpha.height = (int)shape[2];
        result.alpha.channels = (int)shape[1];
        result.alpha.data.resize(result.alpha.width * result.alpha.height * result.alpha.channels);

        ColorUtils::from_planar(raw_output.GetTensorData<float>(), result.alpha);

        return result;

    } catch (const Ort::Exception& e) {
        return std::unexpected(Error{ ErrorCode::InferenceFailed, std::string("ONNX Runtime execution failed: ") + e.what() });
    } catch (const std::exception& e) {
        return std::unexpected(Error{ ErrorCode::InferenceFailed, std::string("Inference error: ") + e.what() });
    }
}

// Remove the redundant prepare_input_tensors as logic is now in run() for better lifetime control
// (Will remove from header next)


} // namespace corridorkey
