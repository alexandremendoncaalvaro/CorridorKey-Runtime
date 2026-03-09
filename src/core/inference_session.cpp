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
    (void)params; // Use params for post-processing later

    try {
        Ort::MemoryInfo memory_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);

        // 1. Prepare Inputs
        // We need to keep the converted NCHW data alive during the Run() call.
        // These vectors hold the actual float data.
        std::vector<std::vector<float>> input_buffers;
        std::vector<Ort::Value> input_tensors;

        for (size_t i = 0; i < m_input_node_dims.size(); ++i) {
            auto shape = m_input_node_dims[i];
            int64_t model_c = shape[1];
            int64_t model_h = shape[2];
            int64_t model_w = shape[3];

            const Image& source = (i == 0) ? rgb : alpha_hint;
            
            // Resize if needed
            Image resized = (source.width != (int)model_w || source.height != (int)model_h) 
                           ? ColorUtils::resize(source, (int)model_w, (int)model_h)
                           : source;

            // Convert HWC to NCHW
            std::vector<float> planar_data(model_c * model_h * model_w);
            for (int c = 0; c < model_c; ++c) {
                for (int y = 0; y < model_h; ++y) {
                    for (int x = 0; x < model_w; ++x) {
                        int src_idx = (y * (int)model_w + x) * (int)source.channels + (c < (int)source.channels ? c : 0);
                        int dst_idx = c * (int)model_h * (int)model_w + y * (int)model_w + x;
                        planar_data[dst_idx] = resized.data[src_idx];
                    }
                }
            }
            
            // Move the data to the persistent container for this scope
            input_buffers.push_back(std::move(planar_data));
            
            // Create the non-owning Ort::Value pointing to our managed buffer
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

        // 3. Process Outputs (NCHW to HWC)
        // For now, we assume output 0 is the Alpha Matte (1-channel)
        // CorridorKey model outputs vary, we'll refine this as we know the exact ONNX version.
        auto& raw_output = output_tensors[0];
        auto output_info = raw_output.GetTensorTypeAndShapeInfo();
        auto output_shape = output_info.GetShape();
        
        int64_t out_c = output_shape[1];
        int64_t out_h = output_shape[2];
        int64_t out_w = output_shape[3];
        float* out_data = raw_output.GetTensorMutableData<float>();

        FrameResult result;
        result.alpha.width = (int)out_w;
        result.alpha.height = (int)out_h;
        result.alpha.channels = (int)out_c;
        result.alpha.data.resize(out_c * out_h * out_w);

        // Convert NCHW to HWC for the output Image
        for (int c = 0; c < out_c; ++c) {
            for (int y = 0; y < out_h; ++y) {
                for (int x = 0; x < out_w; ++x) {
                    int src_idx = c * (int)out_h * (int)out_w + y * (int)out_w + x;
                    int dst_idx = (y * (int)out_w + x) * (int)out_c + c;
                    result.alpha.data[dst_idx] = out_data[src_idx];
                }
            }
        }

        // TODO: Map other outputs to foreground and composite
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
