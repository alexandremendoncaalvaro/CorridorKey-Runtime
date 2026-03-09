#include "inference_session.hpp"
#include "post_process/color_utils.hpp"
#include <iostream>

namespace corridorkey {

InferenceSession::InferenceSession(DeviceInfo device)
    : m_device(std::move(device)) {}

InferenceSession::~InferenceSession() = default;

void InferenceSession::configure_session_options() {
    m_session_options.SetIntraOpNumThreads(1);
    m_session_options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

    switch (m_device.backend) {
        case Backend::CUDA: {
            OrtCUDAProviderOptions cuda_options;
            cuda_options.device_id = 0;
            m_session_options.AppendExecutionProvider_CUDA(cuda_options);
            break;
        }
        case Backend::TensorRT: {
            OrtTensorRTProviderOptions trt_options;
            trt_options.device_id = 0;
            m_session_options.AppendExecutionProvider_TensorRT(trt_options);
            break;
        }
        case Backend::DirectML: {
            // DirectML setup if needed
            break;
        }
        default:
            break;
    }
}

void InferenceSession::extract_metadata() {
    Ort::AllocatorWithDefaultOptions allocator;
    
    // Inputs
    size_t num_input_nodes = m_session.GetInputCount();
    for (size_t i = 0; i < num_input_nodes; i++) {
        auto input_name_ptr = m_session.GetInputNameAllocated(i, allocator);
        m_input_node_names.push_back(input_name_ptr.get());

        auto type_info = m_session.GetInputTypeInfo(i);
        auto tensor_info = type_info.GetTensorTypeAndShapeInfo();
        m_input_node_dims.push_back(tensor_info.GetShape());
    }
    for (const auto& name : m_input_node_names) {
        m_input_node_names_ptr.push_back(name.c_str());
    }

    // Outputs
    size_t num_output_nodes = m_session.GetOutputCount();
    for (size_t i = 0; i < num_output_nodes; i++) {
        auto output_name_ptr = m_session.GetOutputNameAllocated(i, allocator);
        m_output_node_names.push_back(output_name_ptr.get());
    }
    for (const auto& name : m_output_node_names) {
        m_output_node_names_ptr.push_back(name.c_str());
    }
}

Result<std::unique_ptr<InferenceSession>> InferenceSession::create(
    const std::filesystem::path& model_path,
    DeviceInfo device
) {
    if (!std::filesystem::exists(model_path)) {
        return unexpected(Error{ ErrorCode::ModelLoadFailed, "Model file not found: " + model_path.string() });
    }

    try {
        auto session_ptr = std::unique_ptr<InferenceSession>(new InferenceSession(std::move(device)));
        session_ptr->m_env = Ort::Env(ORT_LOGGING_LEVEL_WARNING, "CorridorKey");
        session_ptr->configure_session_options();

#ifdef _WIN32
        session_ptr->m_session = Ort::Session(session_ptr->m_env, model_path.wstring().c_str(), session_ptr->m_session_options);
#else
        session_ptr->m_session = Ort::Session(session_ptr->m_env, model_path.c_str(), session_ptr->m_session_options);
#endif

        session_ptr->extract_metadata();
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
        int target_res = params.target_resolution > 0 ? params.target_resolution : m_recommended_resolution;

        std::vector<ImageBuffer> resized_buffers; 
        std::vector<std::vector<float>> planar_buffers; 
        std::vector<Ort::Value> input_tensors;

        for (size_t i = 0; i < m_input_node_dims.size(); ++i) {
            auto shape = m_input_node_dims[i];
            const Image& source = (i == 0) ? rgb : alpha_hint;
            
            int64_t model_h = (shape[2] == -1) ? target_res : shape[2];
            int64_t model_w = (shape[3] == -1) ? target_res : shape[3];

            Image current_view = source;
            if (source.width != (int)model_w || source.height != (int)model_h) {
                resized_buffers.push_back(ColorUtils::resize(source, (int)model_w, (int)model_h));
                current_view = resized_buffers.back().view();
            }

            planar_buffers.emplace_back(shape[1] * model_h * model_w);
            ColorUtils::to_planar(current_view, planar_buffers.back().data());
            
            std::vector<int64_t> effective_shape = shape;
            effective_shape[2] = model_h;
            effective_shape[3] = model_w;

            input_tensors.push_back(Ort::Value::CreateTensor<float>(
                memory_info, 
                planar_buffers.back().data(), planar_buffers.back().size(), 
                effective_shape.data(), effective_shape.size()
            ));
        }

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

        FrameResult result;
        if (output_tensors.size() > 0) {
            auto shape = output_tensors[0].GetTensorTypeAndShapeInfo().GetShape();
            result.alpha = ImageBuffer((int)shape[3], (int)shape[2], (int)shape[1]);
            ColorUtils::from_planar(output_tensors[0].GetTensorData<float>(), result.alpha.view());
        }
        if (output_tensors.size() > 1) {
            auto shape = output_tensors[1].GetTensorTypeAndShapeInfo().GetShape();
            result.foreground = ImageBuffer((int)shape[3], (int)shape[2], (int)shape[1]);
            ColorUtils::from_planar(output_tensors[1].GetTensorData<float>(), result.foreground.view());
        }
        if (output_tensors.size() > 2) {
            auto shape = output_tensors[2].GetTensorTypeAndShapeInfo().GetShape();
            result.composite = ImageBuffer((int)shape[3], (int)shape[2], (int)shape[1]);
            ColorUtils::from_planar(output_tensors[2].GetTensorData<float>(), result.composite.view());
        }

        return result;
    } catch (const Ort::Exception& e) {
        return unexpected(Error{ ErrorCode::InferenceFailed, std::string("ONNX Runtime execution failed: ") + e.what() });
    } catch (const std::exception& e) {
        return unexpected(Error{ ErrorCode::InferenceFailed, std::string("Inference error: ") + e.what() });
    }
}

} // namespace corridorkey
