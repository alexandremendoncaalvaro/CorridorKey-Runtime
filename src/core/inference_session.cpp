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
        return unexpected(Error{ ErrorCode::ModelLoadFailed, "Model file not found: " + model_path.string() });
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
        return unexpected(Error{ ErrorCode::ModelLoadFailed, std::string("ONNX Runtime session creation failed: ") + e.what() });
    } catch (const std::exception& e) {
        return unexpected(Error{ ErrorCode::ModelLoadFailed, std::string("Failed to initialize session: ") + e.what() });
    }
}

Result<FrameResult> InferenceSession::run(
    const Image& rgb, 
    const Image& alpha_hint,
    const InferenceParams& params
) {
    try {
        Ort::MemoryInfo memory_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);

        // Determine effective resolution
        int target_res = params.target_resolution > 0 ? params.target_resolution : m_recommended_resolution;

        // 1. Prepare Inputs
        std::vector<ImageBuffer> resized_buffers; 
        std::vector<std::vector<float>> planar_buffers; 
        std::vector<Ort::Value> input_tensors;

        for (size_t i = 0; i < m_input_node_dims.size(); ++i) {
            auto shape = m_input_node_dims[i];
            const Image& source = (i == 0) ? rgb : alpha_hint;
            
            // Resolution hierarchy: User Override > Hardware Recommended > Model Native
            int64_t model_h = (shape[2] == -1) ? target_res : shape[2];
            int64_t model_w = (shape[3] == -1) ? target_res : shape[3];

            Image current_view = source;
            if (source.width != (int)model_w || source.height != (int)model_h) {
                resized_buffers.push_back(ColorUtils::resize(source, (int)model_w, (int)model_h));
                current_view = resized_buffers.back().view();
            }

            planar_buffers.emplace_back(shape[1] * model_h * model_w);
            ColorUtils::to_planar(current_view, planar_buffers.back().data());
            
            // Update shape for Dynamic ONNX models if needed
            std::vector<int64_t> effective_shape = shape;
            effective_shape[2] = model_h;
            effective_shape[3] = model_w;

            input_tensors.push_back(Ort::Value::CreateTensor<float>(
                memory_info, 
                planar_buffers.back().data(), planar_buffers.back().size(), 
                effective_shape.data(), effective_shape.size()
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
            return unexpected(Error{ ErrorCode::InferenceFailed, "Model produced no output tensors" });
        }

        // 3. Process Outputs (assume output 0 is Alpha Matte)
        auto& raw_output = output_tensors[0];
        auto shape = raw_output.GetTensorTypeAndShapeInfo().GetShape();
        
        FrameResult result;
        result.alpha = ImageBuffer((int)shape[3], (int)shape[2], (int)shape[1]);
        
        ColorUtils::from_planar(raw_output.GetTensorData<float>(), result.alpha.view());

        // TODO: Map other outputs (FG, Comp) using the same ImageBuffer pattern
        return result;

    } catch (const Ort::Exception& e) {
        return unexpected(Error{ ErrorCode::InferenceFailed, std::string("ONNX Runtime execution failed: ") + e.what() });
    } catch (const std::exception& e) {
        return unexpected(Error{ ErrorCode::InferenceFailed, std::string("Inference error: ") + e.what() });
    }
}

// Remove the redundant prepare_input_tensors as logic is now in run() for better lifetime control
// (Will remove from header next)


} // namespace corridorkey
