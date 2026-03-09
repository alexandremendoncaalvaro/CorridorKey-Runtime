#include "inference_session.hpp"
#include "common/srgb_lut.hpp"
#include "post_process/color_utils.hpp"
#include "post_process/despill.hpp"
#include "post_process/despeckle.hpp"

namespace corridorkey {

InferenceSession::InferenceSession(DeviceInfo device)
    : m_device(std::move(device)) {
    
    // Hardware Tier Logic
    // HIGH: 10+ GB dedicated -> 1024
    // MEDIUM: 12-16 GB unified -> 768
    // LOW: < 12 GB -> 512
    // CPU: -> 512

    if (m_device.backend == Backend::CPU) {
        m_recommended_resolution = 512;
    } else if (m_device.backend == Backend::CoreML || m_device.backend == Backend::DirectML) {
        // Unified or generic GPU
        if (m_device.available_memory_mb >= 16000) {
            m_recommended_resolution = 768;
        } else {
            m_recommended_resolution = 512;
        }
    } else {
        // Dedicated GPUs (CUDA, TensorRT)
        if (m_device.available_memory_mb >= 10000) {
            m_recommended_resolution = 1024;
        } else if (m_device.available_memory_mb >= 8000) {
            m_recommended_resolution = 768;
        } else {
            m_recommended_resolution = 512;
        }
    }
}

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
            break;
        }
        default:
            break;
    }
}

void InferenceSession::extract_metadata() {
    Ort::AllocatorWithDefaultOptions allocator;

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

Result<FrameResult> InferenceSession::run_tiled(
    const Image& rgb,
    const Image& alpha_hint,
    const InferenceParams& params,
    int model_res
) {
    int w = rgb.width;
    int h = rgb.height;
    int tile_size = model_res;
    int padding = params.tile_padding;
    int stride = tile_size - 2 * padding;

    // Allocate accumulators
    ImageBuffer acc_alpha(w, h, 1);
    ImageBuffer acc_fg(w, h, 3);
    ImageBuffer acc_weight(w, h, 1);

    std::fill(acc_alpha.view().data.begin(), acc_alpha.view().data.end(), 0.0f);
    std::fill(acc_fg.view().data.begin(), acc_fg.view().data.end(), 0.0f);
    std::fill(acc_weight.view().data.begin(), acc_weight.view().data.end(), 0.0f);

    // Pre-calculate weight mask (linear falloff at edges)
    ImageBuffer weight_mask_buf(tile_size, tile_size, 1);
    Image mask = weight_mask_buf.view();
    for (int y = 0; y < tile_size; ++y) {
        float wy = 1.0f;
        if (y < padding) wy = static_cast<float>(y) / padding;
        else if (y >= tile_size - padding) wy = static_cast<float>(tile_size - 1 - y) / padding;
        
        for (int x = 0; x < tile_size; ++x) {
            float wx = 1.0f;
            if (x < padding) wx = static_cast<float>(x) / padding;
            else if (x >= tile_size - padding) wx = static_cast<float>(tile_size - 1 - x) / padding;
            mask(y, x) = std::min(wx, wy);
        }
    }

    // Iterate tiles
    int nx = (w + stride - 1) / stride;
    int ny = (h + stride - 1) / stride;

    for (int ty = 0; ty < ny; ++ty) {
        for (int tx = 0; tx < nx; ++tx) {
            int x_start = tx * stride;
            int y_start = ty * stride;
            
            // Center crop if last tile goes over edge
            if (x_start + tile_size > w) x_start = std::max(0, w - tile_size);
            if (y_start + tile_size > h) y_start = std::max(0, h - tile_size);

            // Extract tile
            ImageBuffer rgb_tile(tile_size, tile_size, 3);
            ImageBuffer hint_tile(tile_size, tile_size, 1);
            
            for (int y = 0; y < tile_size; ++y) {
                for (int x = 0; x < tile_size; ++x) {
                    for (int c = 0; c < 3; ++c) rgb_tile.view()(y, x, c) = rgb(y_start + y, x_start + x, c);
                    hint_tile.view()(y, x) = alpha_hint(y_start + y, x_start + x);
                }
            }

            // Run inference on tile (recursive call but non-tiled params)
            InferenceParams sub_params = params;
            sub_params.enable_tiling = false; 
            sub_params.target_resolution = tile_size; 
            auto res = run(rgb_tile.view(), hint_tile.view(), sub_params);
            if (!res) return unexpected(res.error());

            // Accumulate
            for (int y = 0; y < tile_size; ++y) {
                for (int x = 0; x < tile_size; ++x) {
                    int global_y = y_start + y;
                    int global_x = x_start + x;
                    if (global_y >= h || global_x >= w) continue;

                    float w_val = mask(y, x);
                    acc_weight.view()(global_y, global_x) += w_val;
                    acc_alpha.view()(global_y, global_x) += res->alpha.view()(y, x) * w_val;
                    for (int c = 0; c < 3; ++c) {
                        acc_fg.view()(global_y, global_x, c) += res->foreground.view()(y, x, c) * w_val;
                    }
                }
            }
        }
    }

    // Normalize
    for (int i = 0; i < w * h; ++i) {
        float w_val = acc_weight.view().data[i];
        if (w_val > 0.0001f) {
            acc_alpha.view().data[i] /= w_val;
            acc_fg.view().data[i*3] /= w_val;
            acc_fg.view().data[i*3+1] /= w_val;
            acc_fg.view().data[i*3+2] /= w_val;
        }
    }

    // Final Post-Process on full image
    FrameResult result;
    result.alpha = std::move(acc_alpha);
    result.foreground = std::move(acc_fg);

    // Run standard post-process pipeline on full image
    if (!result.alpha.view().empty() && !result.foreground.view().empty()) {
        int w = result.foreground.view().width;
        int h = result.foreground.view().height;

        if (params.auto_despeckle) {
            despeckle(result.alpha.view(), params.despeckle_size);
        }
        despill(result.foreground.view(), params.despill_strength);

        const auto& lut = SrgbLut::instance();
        Image fg = result.foreground.const_view();
        Image alpha_view = result.alpha.const_view();

        result.processed = ImageBuffer(w, h, 4);
        Image proc = result.processed.view();

        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                float a = alpha_view(y, x);
                proc(y, x, 0) = lut.to_linear(fg(y, x, 0)) * a;
                proc(y, x, 1) = lut.to_linear(fg(y, x, 1)) * a;
                proc(y, x, 2) = lut.to_linear(fg(y, x, 2)) * a;
                proc(y, x, 3) = a;
            }
        }

        result.composite = ImageBuffer(w, h, 4);
        Image comp = result.composite.view();
        std::copy(proc.data.begin(), proc.data.end(), comp.data.begin());

        ColorUtils::composite_over_checker(comp);
        ColorUtils::linear_to_srgb(comp);
    }

    return result;
}

Result<FrameResult> InferenceSession::run(
    const Image& rgb,
    const Image& alpha_hint,
    const InferenceParams& params
) {
    // Check for tiling request
    int target_res = params.target_resolution > 0 ? params.target_resolution : m_recommended_resolution;
    
    // Only tile if requested and input is significantly larger than model
    if (params.enable_tiling && (rgb.width > target_res || rgb.height > target_res)) {
        return run_tiled(rgb, alpha_hint, params, target_res);
    }

    try {
        Ort::MemoryInfo memory_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        int target_res = params.target_resolution > 0 ? params.target_resolution : m_recommended_resolution;

        // The Python export outputs a single tensor with 4 channels (RGB + Hint)
        // Check if the model expects 1 input (concatenated) or 2 inputs
        bool is_concatenated = (m_input_node_dims.size() == 1 && m_input_node_dims[0][1] == 4);

        std::vector<Ort::Value> input_tensors;
        m_planar_pool.resize(is_concatenated ? 1 : m_input_node_dims.size());

        if (is_concatenated) {
            auto shape = m_input_node_dims[0];
            int64_t batch_size = shape[0] < 0 ? 1 : shape[0];
            int64_t model_h = shape[2] < 0 ? target_res : shape[2];
            int64_t model_w = shape[3] < 0 ? target_res : shape[3];

            Image current_rgb = rgb;
            Image current_hint = alpha_hint;

            if (rgb.width != (int)model_w || rgb.height != (int)model_h) {
                m_resize_pool.resize(2);
                m_resize_pool[0] = ColorUtils::resize(rgb, (int)model_w, (int)model_h);
                m_resize_pool[1] = ColorUtils::resize(alpha_hint, (int)model_w, (int)model_h);
                current_rgb = m_resize_pool[0].view();
                current_hint = m_resize_pool[1].view();
            }

            size_t planar_size = 4 * model_h * model_w;
            if (m_planar_pool[0].view().data.size() != planar_size) {
                m_planar_pool[0] = ImageBuffer(static_cast<int>(planar_size), 1, 1);
            }

            // Manually interleave to planar and apply ImageNet normalization to RGB
            // RGB mean: [0.485, 0.456, 0.406], std: [0.229, 0.224, 0.225]
            float* dst = m_planar_pool[0].view().data.data();
            size_t channel_stride = model_h * model_w;
            
            for (int y = 0; y < model_h; ++y) {
                for (int x = 0; x < model_w; ++x) {
                    size_t idx = y * model_w + x;
                    dst[0 * channel_stride + idx] = (current_rgb(y, x, 0) - 0.485f) / 0.229f; // R
                    dst[1 * channel_stride + idx] = (current_rgb(y, x, 1) - 0.456f) / 0.224f; // G
                    dst[2 * channel_stride + idx] = (current_rgb(y, x, 2) - 0.406f) / 0.225f; // B
                    dst[3 * channel_stride + idx] = current_hint(y, x, 0); // Hint
                }
            }

            std::vector<int64_t> effective_shape = {batch_size, 4, model_h, model_w};
            input_tensors.push_back(Ort::Value::CreateTensor<float>(
                memory_info,
                dst, planar_size,
                effective_shape.data(), effective_shape.size()
            ));
        } else {
            m_resize_pool.resize(m_input_node_dims.size());
            for (size_t i = 0; i < m_input_node_dims.size(); ++i) {
                auto shape = m_input_node_dims[i];
                const Image& source = (i == 0) ? rgb : alpha_hint;

                int64_t batch_size = shape[0] < 0 ? 1 : shape[0];
                int64_t model_h = shape[2] < 0 ? target_res : shape[2];
                int64_t model_w = shape[3] < 0 ? target_res : shape[3];

                Image current_view = source;
                if (source.width != (int)model_w || source.height != (int)model_h) {
                    m_resize_pool[i] = ColorUtils::resize(source, (int)model_w, (int)model_h);
                    current_view = m_resize_pool[i].view();
                }

                // Reuse planar buffer if size matches, otherwise reallocate
                size_t planar_size = static_cast<size_t>(shape[1]) * model_h * model_w;
                if (m_planar_pool[i].view().data.size() != planar_size) {
                    m_planar_pool[i] = ImageBuffer(static_cast<int>(planar_size), 1, 1);
                }
                
                // Only RGB (i == 0) gets normalized, Hint (i == 1) does not
                if (i == 0 && shape[1] == 3) {
                    float* dst = m_planar_pool[i].view().data.data();
                    size_t channel_stride = model_h * model_w;
                    for (int y = 0; y < model_h; ++y) {
                        for (int x = 0; x < model_w; ++x) {
                            size_t idx = y * model_w + x;
                            dst[0 * channel_stride + idx] = (current_view(y, x, 0) - 0.485f) / 0.229f; // R
                            dst[1 * channel_stride + idx] = (current_view(y, x, 1) - 0.456f) / 0.224f; // G
                            dst[2 * channel_stride + idx] = (current_view(y, x, 2) - 0.406f) / 0.225f; // B
                        }
                    }
                } else {
                    ColorUtils::to_planar(current_view, m_planar_pool[i].view().data.data());
                }

                std::vector<int64_t> effective_shape = {batch_size, shape[1], model_h, model_w};

                input_tensors.push_back(Ort::Value::CreateTensor<float>(
                    memory_info,
                    m_planar_pool[i].view().data.data(), planar_size,
                    effective_shape.data(), effective_shape.size()
                ));
            }
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

        // Extract alpha from output tensor 0
        if (output_tensors.size() > 0) {
            auto shape = output_tensors[0].GetTensorTypeAndShapeInfo().GetShape();
            result.alpha = ImageBuffer((int)shape[3], (int)shape[2], (int)shape[1]);
            ColorUtils::from_planar(output_tensors[0].GetTensorData<float>(), result.alpha.view());
        }

        // Extract foreground from output tensor 1
        if (output_tensors.size() > 1) {
            auto shape = output_tensors[1].GetTensorTypeAndShapeInfo().GetShape();
            result.foreground = ImageBuffer((int)shape[3], (int)shape[2], (int)shape[1]);
            ColorUtils::from_planar(output_tensors[1].GetTensorData<float>(), result.foreground.view());
        }

        // Post-process pipeline (matches original Python order):
        // 1. Despeckle alpha (space-agnostic, operates on 0-1 mask)
        // 2. Despill FG in sRGB (model output is sRGB)
        // 3. Convert FG to linear, premultiply, pack RGBA
        // 4. Composite on checker in linear, convert to sRGB
        if (!result.alpha.view().empty() && !result.foreground.view().empty()) {
            int w = result.foreground.view().width;
            int h = result.foreground.view().height;

            // 1. Despeckle alpha
            if (params.auto_despeckle) {
                despeckle(result.alpha.view(), params.despeckle_size);
            }

            // 2. Despill FG in sRGB space (no alpha involvement)
            despill(result.foreground.view(), params.despill_strength);

            // 3. Generate processed: sRGB FG -> linear -> premultiply -> RGBA
            const auto& lut = SrgbLut::instance();
            Image fg = result.foreground.const_view();
            Image alpha_view = result.alpha.const_view();

            result.processed = ImageBuffer(w, h, 4);
            Image proc = result.processed.view();

            for (int y = 0; y < h; ++y) {
                for (int x = 0; x < w; ++x) {
                    float a = alpha_view(y, x);
                    proc(y, x, 0) = lut.to_linear(fg(y, x, 0)) * a;
                    proc(y, x, 1) = lut.to_linear(fg(y, x, 1)) * a;
                    proc(y, x, 2) = lut.to_linear(fg(y, x, 2)) * a;
                    proc(y, x, 3) = a;
                }
            }

            // 4. Composite on checker (linear space), then convert to sRGB
            result.composite = ImageBuffer(w, h, 4);
            Image comp = result.composite.view();
            std::copy(proc.data.begin(), proc.data.end(), comp.data.begin());

            ColorUtils::composite_over_checker(comp);
            ColorUtils::linear_to_srgb(comp);
        }

        return result;
    } catch (const Ort::Exception& e) {
        return unexpected(Error{ ErrorCode::InferenceFailed, std::string("ONNX Runtime execution failed: ") + e.what() });
    } catch (const std::exception& e) {
        return unexpected(Error{ ErrorCode::InferenceFailed, std::string("Inference error: ") + e.what() });
    }
}

} // namespace corridorkey
