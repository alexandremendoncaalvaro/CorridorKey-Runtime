#include "inference_session.hpp"

#include "common/srgb_lut.hpp"
#include "post_process/color_utils.hpp"
#include "post_process/despeckle.hpp"
#include "post_process/despill.hpp"

namespace corridorkey {

InferenceSession::InferenceSession(DeviceInfo device) : m_device(std::move(device)) {
    // Default recommended resolution. High-level layers (App)
    // will typically override this via InferenceParams.
    m_recommended_resolution = 512;
}

InferenceSession::~InferenceSession() = default;

void InferenceSession::configure_session_options() {
    m_session_options.SetIntraOpNumThreads(1);
    m_session_options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

    switch (m_device.backend) {
        case Backend::CoreML: {
#ifdef __APPLE__
            uint32_t coreml_flags = 0;  // Default flags (Enable ANE, allow GPU/CPU fallback)
            Ort::ThrowOnError(
                OrtSessionOptionsAppendExecutionProvider_CoreML(m_session_options, coreml_flags));
#endif
            break;
        }
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
#ifdef _WIN32
        case Backend::DirectML: {
            // DirectML is only available on Windows. We use the generic provider string.
            std::unordered_map<std::string, std::string> dml_options;
            dml_options["device_id"] = "0";
            m_session_options.AppendExecutionProvider("DML", dml_options);
            break;
        }
#endif
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
    const std::filesystem::path& model_path, DeviceInfo device) {
    if (!std::filesystem::exists(model_path)) {
        return Unexpected(
            Error{ErrorCode::ModelLoadFailed, "Model file not found: " + model_path.string()});
    }

    try {
        auto session_ptr =
            std::unique_ptr<InferenceSession>(new InferenceSession(std::move(device)));
        session_ptr->m_env = Ort::Env(ORT_LOGGING_LEVEL_WARNING, "CorridorKey");
        session_ptr->configure_session_options();

#ifdef _WIN32
        session_ptr->m_session = Ort::Session(session_ptr->m_env, model_path.wstring().c_str(),
                                              session_ptr->m_session_options);
#else
        session_ptr->m_session =
            Ort::Session(session_ptr->m_env, model_path.c_str(), session_ptr->m_session_options);
#endif

        session_ptr->extract_metadata();
        return session_ptr;
    } catch (const Ort::Exception& e) {
        return Unexpected(Error{ErrorCode::ModelLoadFailed,
                                std::string("ONNX Runtime session creation failed: ") + e.what()});
    } catch (const std::exception& e) {
        return Unexpected(Error{ErrorCode::ModelLoadFailed,
                                std::string("Failed to initialize session: ") + e.what()});
    }
}

Result<FrameResult> InferenceSession::run_tiled(const Image& rgb, const Image& alpha_hint,
                                                const InferenceParams& params, int model_res) {
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
        if (y < padding)
            wy = static_cast<float>(y) / padding;
        else if (y >= tile_size - padding)
            wy = static_cast<float>(tile_size - 1 - y) / padding;

        for (int x = 0; x < tile_size; ++x) {
            float wx = 1.0f;
            if (x < padding)
                wx = static_cast<float>(x) / padding;
            else if (x >= tile_size - padding)
                wx = static_cast<float>(tile_size - 1 - x) / padding;
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
                    for (int c = 0; c < 3; ++c)
                        rgb_tile.view()(y, x, c) = rgb(y_start + y, x_start + x, c);
                    hint_tile.view()(y, x) = alpha_hint(y_start + y, x_start + x);
                }
            }

            // Run raw inference on tile
            auto res = infer_raw(rgb_tile.view(), hint_tile.view(), params);
            if (!res) return Unexpected(res.error());

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
                        acc_fg.view()(global_y, global_x, c) +=
                            res->foreground.view()(y, x, c) * w_val;
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
            acc_fg.view().data[i * 3] /= w_val;
            acc_fg.view().data[i * 3 + 1] /= w_val;
            acc_fg.view().data[i * 3 + 2] /= w_val;
        }
    }

    FrameResult result;
    result.alpha = std::move(acc_alpha);
    result.foreground = std::move(acc_fg);

    // Apply post-process on the full stitched image
    apply_post_process(result, params);

    return result;
}

void InferenceSession::apply_post_process(FrameResult& result, const InferenceParams& params) {
    if (result.alpha.view().empty() || result.foreground.view().empty()) return;

    int w = result.foreground.view().width;
    int h = result.foreground.view().height;

    // 1. Despeckle alpha
    if (params.auto_despeckle) {
        despeckle(result.alpha.view(), params.despeckle_size);
    }

    // 2. Despill FG in sRGB space
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

Result<std::vector<FrameResult>> InferenceSession::run_batch(const std::vector<Image>& rgbs,
                                                             const std::vector<Image>& alpha_hints,
                                                             const InferenceParams& params) {
    if (rgbs.empty()) return std::vector<FrameResult>{};

    // Tiling logic for batch (simplified: if first image needs tiling, we don't batch but run tiled
    // one by one) Actually, tiling should be handled at a higher level if we want to batch tiles.
    // For now, let's keep it simple: tiling disables batching.
    int target_res =
        params.target_resolution > 0 ? params.target_resolution : m_recommended_resolution;
    if (params.enable_tiling && (rgbs[0].width > target_res || rgbs[0].height > target_res)) {
        std::vector<FrameResult> results;
        for (size_t i = 0; i < rgbs.size(); ++i) {
            auto res = run_tiled(rgbs[i], alpha_hints[i], params, target_res);
            if (!res) return Unexpected(res.error());
            results.push_back(std::move(*res));
        }
        return results;
    }

    auto results_res = infer_batch_raw(rgbs, alpha_hints, params);
    if (!results_res) return results_res;

    for (auto& res : *results_res) {
        apply_post_process(res, params);
    }
    return results_res;
}

Result<std::vector<FrameResult>> InferenceSession::infer_batch_raw(
    const std::vector<Image>& rgbs, const std::vector<Image>& alpha_hints,
    const InferenceParams& params) {
    if (rgbs.empty()) return std::vector<FrameResult>{};

    try {
        Ort::MemoryInfo memory_info =
            Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        int target_res =
            params.target_resolution > 0 ? params.target_resolution : m_recommended_resolution;
        size_t batch_size = rgbs.size();

        bool is_concatenated = (m_input_node_dims.size() == 1 && m_input_node_dims[0][1] == 4);
        std::vector<Ort::Value> input_tensors;

        // Tracking valid areas for each image in batch
        std::vector<Rect> rois(batch_size);

        if (is_concatenated) {
            auto shape = m_input_node_dims[0];
            int64_t model_h = shape[2] < 0 ? target_res : shape[2];
            int64_t model_w = shape[3] < 0 ? target_res : shape[3];

            size_t total_planar_size = batch_size * 4 * model_h * model_w;
            m_planar_pool.resize(1);
            if (m_planar_pool[0].view().data.size() != total_planar_size) {
                m_planar_pool[0] = ImageBuffer(static_cast<int>(total_planar_size), 1, 1);
            }

            float* dst_base = m_planar_pool[0].view().data.data();
            size_t image_stride = 4 * model_h * model_w;
            size_t channel_stride = model_h * model_w;

            for (size_t b = 0; b < batch_size; ++b) {
                auto [padded_rgb, rgb_roi] =
                    ColorUtils::fit_pad(rgbs[b], (int)model_w, (int)model_h);
                auto [padded_hint, hint_roi] =
                    ColorUtils::fit_pad(alpha_hints[b], (int)model_w, (int)model_h);
                rois[b] = rgb_roi;

                Image cur_rgb = padded_rgb.view();
                Image cur_hint = padded_hint.view();
                float* dst = dst_base + (b * image_stride);

                for (int y = 0; y < model_h; ++y) {
                    for (int x = 0; x < model_w; ++x) {
                        size_t idx = y * model_w + x;
                        dst[0 * channel_stride + idx] = (cur_rgb(y, x, 0) - 0.485f) / 0.229f;
                        dst[1 * channel_stride + idx] = (cur_rgb(y, x, 1) - 0.456f) / 0.224f;
                        dst[2 * channel_stride + idx] = (cur_rgb(y, x, 2) - 0.406f) / 0.225f;
                        dst[3 * channel_stride + idx] = cur_hint(y, x, 0);
                    }
                }
            }

            std::vector<int64_t> effective_shape = {(int64_t)batch_size, 4, model_h, model_w};
            input_tensors.push_back(
                Ort::Value::CreateTensor<float>(memory_info, dst_base, total_planar_size,
                                                effective_shape.data(), effective_shape.size()));
        } else {
            return Unexpected(Error{ErrorCode::HardwareNotSupported,
                                    "Non-concatenated models not yet supported with batching"});
        }

        auto output_tensors = m_session.Run(
            Ort::RunOptions{nullptr}, m_input_node_names_ptr.data(), input_tensors.data(),
            input_tensors.size(), m_output_node_names_ptr.data(), m_output_node_names_ptr.size());

        if (output_tensors.empty()) {
            return Unexpected(
                Error{ErrorCode::InferenceFailed, "Model produced no output tensors"});
        }

        std::vector<FrameResult> batch_results(batch_size);

        float* alpha_ptr = output_tensors[0].GetTensorMutableData<float>();
        float* fg_ptr =
            output_tensors.size() > 1 ? output_tensors[1].GetTensorMutableData<float>() : nullptr;

        auto alpha_shape = output_tensors[0].GetTensorTypeAndShapeInfo().GetShape();
        size_t alpha_image_stride = alpha_shape[1] * alpha_shape[2] * alpha_shape[3];

        std::vector<int64_t> fg_shape;
        size_t fg_image_stride = 0;
        if (fg_ptr) {
            fg_shape = output_tensors[1].GetTensorTypeAndShapeInfo().GetShape();
            fg_image_stride = fg_shape[1] * fg_shape[2] * fg_shape[3];
        }

        for (size_t b = 0; b < batch_size; ++b) {
            FrameResult& result = batch_results[b];
            Rect& roi = rois[b];

            // Extract alpha
            ImageBuffer full_alpha((int)alpha_shape[3], (int)alpha_shape[2], (int)alpha_shape[1]);
            ColorUtils::from_planar(alpha_ptr + (b * alpha_image_stride), full_alpha.view());
            ImageBuffer cropped_alpha =
                ColorUtils::crop(full_alpha.view(), roi.x_pos, roi.y_pos, roi.width, roi.height);
            result.alpha = ColorUtils::resize(cropped_alpha.view(), rgbs[b].width, rgbs[b].height);

            // Extract foreground
            if (fg_ptr != nullptr) {
                ImageBuffer full_fg((int)fg_shape[3], (int)fg_shape[2], (int)fg_shape[1]);
                ColorUtils::from_planar(fg_ptr + (b * fg_image_stride), full_fg.view());
                ImageBuffer cropped_fg =
                    ColorUtils::crop(full_fg.view(), roi.x_pos, roi.y_pos, roi.width, roi.height);
                result.foreground =
                    ColorUtils::resize(cropped_fg.view(), rgbs[b].width, rgbs[b].height);
            }
        }

        return batch_results;

    } catch (const Ort::Exception& e) {
        return Unexpected(Error{ErrorCode::InferenceFailed,
                                std::string("ONNX Runtime execution failed: ") + e.what()});
    }
}

Result<FrameResult> InferenceSession::infer_raw(const Image& rgb, const Image& alpha_hint,
                                                const InferenceParams& params) {
    auto batch_res = infer_batch_raw({rgb}, {alpha_hint}, params);
    if (!batch_res) return Unexpected(batch_res.error());
    return std::move((*batch_res)[0]);
}

Result<FrameResult> InferenceSession::run(const Image& rgb, const Image& alpha_hint,
                                          const InferenceParams& params) {
    // Check for tiling request
    int target_res =
        params.target_resolution > 0 ? params.target_resolution : m_recommended_resolution;

    // Only tile if requested and input is significantly larger than model
    if (params.enable_tiling && (rgb.width > target_res || rgb.height > target_res)) {
        return run_tiled(rgb, alpha_hint, params, target_res);
    }

    auto result_res = infer_raw(rgb, alpha_hint, params);
    if (!result_res) return result_res;

    apply_post_process(*result_res, params);
    return result_res;
}

}  // namespace corridorkey
