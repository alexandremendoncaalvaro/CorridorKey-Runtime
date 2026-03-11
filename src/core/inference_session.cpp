#include "inference_session.hpp"

#include <unordered_map>

#include "common/parallel_for.hpp"
#include "common/runtime_paths.hpp"
#include "common/srgb_lut.hpp"
#include "common/stage_profiler.hpp"
#include "mlx_session.hpp"
#include "post_process/color_utils.hpp"
#include "post_process/despeckle.hpp"
#include "post_process/despill.hpp"
#include "session_cache_policy.hpp"
#include "session_policy.hpp"

namespace corridorkey {

namespace {

constexpr const char* kDisableCpuEpFallbackConfig = "session.disable_cpu_ep_fallback";

#ifdef __APPLE__
void append_coreml_execution_provider(Ort::SessionOptions& session_options) {
#if ORT_API_VERSION >= 24
    std::unordered_map<std::string, std::string> provider_options = {
        {kCoremlProviderOption_MLComputeUnits, "ALL"},
        {kCoremlProviderOption_RequireStaticInputShapes, "1"},
    };

    if (auto cache_root = common::coreml_model_cache_root(); cache_root.has_value()) {
        std::error_code error;
        std::filesystem::create_directories(*cache_root, error);
        if (!error) {
            provider_options.emplace(kCoremlProviderOption_ModelCacheDirectory,
                                     cache_root->string());
        }
    }

    session_options.AppendExecutionProvider("CoreML", provider_options);
#else
    uint32_t coreml_flags = COREML_FLAG_ONLY_ALLOW_STATIC_INPUT_SHAPES;
    Ort::ThrowOnError(
        OrtSessionOptionsAppendExecutionProvider_CoreML(session_options, coreml_flags));
#endif
}
#endif

void remove_cached_model(const std::filesystem::path& cache_path) {
    std::error_code error;
    std::filesystem::remove(cache_path, error);
}

void extract_tile_rows(const Image& source_rgb, const Image& source_hint, Image rgb_tile,
                       Image hint_tile, int y_start, int x_start, int y_begin, int y_end) {
    for (int y = y_begin; y < y_end; ++y) {
        for (int x = 0; x < rgb_tile.width; ++x) {
            for (int channel = 0; channel < rgb_tile.channels; ++channel) {
                rgb_tile(y, x, channel) = source_rgb(y_start + y, x_start + x, channel);
            }
            hint_tile(y, x) = source_hint(y_start + y, x_start + x);
        }
    }
}

void accumulate_tile_rows(const Image& mask, const FrameResult& tile_result, Image acc_alpha,
                          Image acc_fg, Image acc_weight, int y_start, int x_start,
                          int image_height, int image_width, int y_begin, int y_end) {
    Image tile_alpha = tile_result.alpha.const_view();
    Image tile_foreground = tile_result.foreground.const_view();

    for (int y = y_begin; y < y_end; ++y) {
        int global_y = y_start + y;
        if (global_y >= image_height) {
            break;
        }

        for (int x = 0; x < mask.width; ++x) {
            int global_x = x_start + x;
            if (global_x >= image_width) {
                break;
            }

            float weight = mask(y, x);
            acc_weight(global_y, global_x) += weight;
            acc_alpha(global_y, global_x) += tile_alpha(y, x) * weight;
            for (int channel = 0; channel < 3; ++channel) {
                acc_fg(global_y, global_x, channel) += tile_foreground(y, x, channel) * weight;
            }
        }
    }
}

void normalize_accumulators(Image acc_alpha, Image acc_fg, const Image& acc_weight, int y_begin,
                            int y_end) {
    for (int y = y_begin; y < y_end; ++y) {
        size_t row_offset = static_cast<size_t>(y) * static_cast<size_t>(acc_alpha.width);
        for (int x = 0; x < acc_alpha.width; ++x) {
            size_t pixel_index = row_offset + static_cast<size_t>(x);
            float weight = acc_weight.data[pixel_index];
            if (weight <= 0.0001f) {
                continue;
            }

            acc_alpha.data[pixel_index] /= weight;
            size_t fg_index = pixel_index * 3;
            acc_fg.data[fg_index] /= weight;
            acc_fg.data[fg_index + 1] /= weight;
            acc_fg.data[fg_index + 2] /= weight;
        }
    }
}

}  // namespace

InferenceSession::InferenceSession(DeviceInfo device) : m_device(std::move(device)) {
    // Default recommended resolution. High-level layers (App)
    // will typically override this via InferenceParams.
    m_recommended_resolution = 512;
}

InferenceSession::~InferenceSession() = default;

void InferenceSession::configure_session_options(bool use_optimized_model_cache,
                                                 const SessionCreateOptions& options) {
    m_session_options.SetIntraOpNumThreads(core::intra_op_threads_for_backend(m_device.backend));
    m_session_options.SetGraphOptimizationLevel(use_optimized_model_cache
                                                    ? GraphOptimizationLevel::ORT_DISABLE_ALL
                                                    : GraphOptimizationLevel::ORT_ENABLE_ALL);
    m_session_options.SetLogSeverityLevel(options.log_severity);

    if (options.disable_cpu_ep_fallback && m_device.backend != Backend::CPU) {
        m_session_options.AddConfigEntry(kDisableCpuEpFallbackConfig, "1");
    }

    switch (m_device.backend) {
        case Backend::CoreML: {
#ifdef __APPLE__
            append_coreml_execution_provider(m_session_options);
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
        case Backend::MLX:
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
    const std::filesystem::path& model_path, DeviceInfo device, SessionCreateOptions options) {
    if (!std::filesystem::exists(model_path)) {
        return Unexpected(
            Error{ErrorCode::ModelLoadFailed, "Model file not found: " + model_path.string()});
    }

    DeviceInfo requested_device = device;

    try {
        auto session_ptr =
            std::unique_ptr<InferenceSession>(new InferenceSession(std::move(device)));
        if (requested_device.backend == Backend::MLX) {
            auto mlx_session_res = core::MlxSession::create(model_path, requested_device);
            if (!mlx_session_res) {
                return Unexpected(mlx_session_res.error());
            }
            session_ptr->m_recommended_resolution = (*mlx_session_res)->model_resolution();
            session_ptr->m_mlx_session = std::move(*mlx_session_res);
            return session_ptr;
        }

        session_ptr->m_env = Ort::Env(options.log_severity, "CorridorKey");
        std::filesystem::path session_model_path = model_path;
        std::optional<std::filesystem::path> optimized_model_path;
        bool using_optimized_model_cache = false;

        if (core::use_optimized_model_cache_for_backend(session_ptr->m_device.backend)) {
            optimized_model_path =
                common::optimized_model_cache_path(model_path, session_ptr->m_device.backend);
            if (optimized_model_path.has_value()) {
                std::error_code error;
                std::filesystem::create_directories(optimized_model_path->parent_path(), error);
                if (!error && std::filesystem::exists(*optimized_model_path, error)) {
                    session_model_path = *optimized_model_path;
                    using_optimized_model_cache = true;
                }
            }
        }

        session_ptr->configure_session_options(using_optimized_model_cache, options);
        if (!using_optimized_model_cache && optimized_model_path.has_value()) {
#ifdef _WIN32
            session_ptr->m_session_options.SetOptimizedModelFilePath(
                optimized_model_path->wstring().c_str());
#else
            session_ptr->m_session_options.SetOptimizedModelFilePath(optimized_model_path->c_str());
#endif
        }

#ifdef _WIN32
        session_ptr->m_session =
            Ort::Session(session_ptr->m_env, session_model_path.wstring().c_str(),
                         session_ptr->m_session_options);
#else
        session_ptr->m_session = Ort::Session(session_ptr->m_env, session_model_path.c_str(),
                                              session_ptr->m_session_options);
#endif

        session_ptr->extract_metadata();
        return session_ptr;
    } catch (const Ort::Exception& e) {
        if (core::use_optimized_model_cache_for_backend(requested_device.backend)) {
            auto optimized_model_path =
                common::optimized_model_cache_path(model_path, requested_device.backend);
            if (optimized_model_path.has_value() &&
                std::filesystem::exists(*optimized_model_path)) {
                remove_cached_model(*optimized_model_path);
                try {
                    auto session_ptr =
                        std::unique_ptr<InferenceSession>(new InferenceSession(requested_device));
                    session_ptr->m_env = Ort::Env(options.log_severity, "CorridorKey");
                    session_ptr->configure_session_options(false, options);
#ifdef _WIN32
                    session_ptr->m_session_options.SetOptimizedModelFilePath(
                        optimized_model_path->wstring().c_str());
#else
                    session_ptr->m_session_options.SetOptimizedModelFilePath(
                        optimized_model_path->c_str());
#endif
#ifdef _WIN32
                    session_ptr->m_session =
                        Ort::Session(session_ptr->m_env, model_path.wstring().c_str(),
                                     session_ptr->m_session_options);
#else
                    session_ptr->m_session = Ort::Session(session_ptr->m_env, model_path.c_str(),
                                                          session_ptr->m_session_options);
#endif
                    session_ptr->extract_metadata();
                    return session_ptr;
                } catch (const Ort::Exception&) {
                    remove_cached_model(*optimized_model_path);
                }
            }
        }
        return Unexpected(Error{ErrorCode::ModelLoadFailed,
                                std::string("ONNX Runtime session creation failed: ") + e.what()});
    } catch (const std::exception& e) {
        return Unexpected(Error{ErrorCode::ModelLoadFailed,
                                std::string("Failed to initialize session: ") + e.what()});
    }
}

Result<FrameResult> InferenceSession::run_tiled(const Image& rgb, const Image& alpha_hint,
                                                const InferenceParams& params, int model_res,
                                                StageTimingCallback on_stage) {
    struct PendingTile {
        int x_start = 0;
        int y_start = 0;
        Image rgb_view = {};
        Image hint_view = {};
    };

    int w = rgb.width;
    int h = rgb.height;
    int tile_size = model_res;
    int padding = params.tile_padding;
    int stride = tile_size - 2 * padding;
    int tile_batch_size = m_device.backend == Backend::CPU ? std::max(1, params.batch_size) : 1;

    // Allocate accumulators
    ImageBuffer acc_alpha(w, h, 1);
    ImageBuffer acc_fg(w, h, 3);
    ImageBuffer acc_weight(w, h, 1);

    std::fill(acc_alpha.view().data.begin(), acc_alpha.view().data.end(), 0.0f);
    std::fill(acc_fg.view().data.begin(), acc_fg.view().data.end(), 0.0f);
    std::fill(acc_weight.view().data.begin(), acc_weight.view().data.end(), 0.0f);

    if (m_tiled_weight_mask.view().width != tile_size || m_tiled_weight_padding != padding) {
        m_tiled_weight_mask = ImageBuffer(tile_size, tile_size, 1);
        Image mask_view = m_tiled_weight_mask.view();
        common::measure_stage(
            on_stage, "tile_prepare_weights",
            [&]() {
                common::parallel_for_rows(tile_size, [&](int y_begin, int y_end) {
                    for (int y = y_begin; y < y_end; ++y) {
                        float wy = 1.0f;
                        if (padding > 0 && y < padding) {
                            wy = static_cast<float>(y) / static_cast<float>(padding);
                        } else if (padding > 0 && y >= tile_size - padding) {
                            wy =
                                static_cast<float>(tile_size - 1 - y) / static_cast<float>(padding);
                        }

                        for (int x = 0; x < tile_size; ++x) {
                            float wx = 1.0f;
                            if (padding > 0 && x < padding) {
                                wx = static_cast<float>(x) / static_cast<float>(padding);
                            } else if (padding > 0 && x >= tile_size - padding) {
                                wx = static_cast<float>(tile_size - 1 - x) /
                                     static_cast<float>(padding);
                            }
                            mask_view(y, x) = std::min(wx, wy);
                        }
                    }
                });
            },
            1);
        m_tiled_weight_padding = padding;
    }

    if (m_tiled_buffer_size != tile_size || m_tiled_pool_capacity != tile_batch_size) {
        m_tiled_rgb_pool.clear();
        m_tiled_hint_pool.clear();
        m_tiled_rgb_pool.reserve(static_cast<size_t>(tile_batch_size));
        m_tiled_hint_pool.reserve(static_cast<size_t>(tile_batch_size));
        for (int i = 0; i < tile_batch_size; ++i) {
            m_tiled_rgb_pool.emplace_back(tile_size, tile_size, 3);
            m_tiled_hint_pool.emplace_back(tile_size, tile_size, 1);
        }
        m_tiled_buffer_size = tile_size;
        m_tiled_pool_capacity = tile_batch_size;
    }
    Image mask = m_tiled_weight_mask.view();

    // Iterate tiles
    int nx = (w + stride - 1) / stride;
    int ny = (h + stride - 1) / stride;
    std::vector<PendingTile> pending_tiles;
    pending_tiles.reserve(static_cast<size_t>(tile_batch_size));

    auto flush_pending_tiles = [&]() -> Result<void> {
        if (pending_tiles.empty()) {
            return {};
        }

        std::vector<Image> rgb_tiles;
        std::vector<Image> hint_tiles;
        rgb_tiles.reserve(pending_tiles.size());
        hint_tiles.reserve(pending_tiles.size());
        for (const auto& tile : pending_tiles) {
            rgb_tiles.push_back(tile.rgb_view);
            hint_tiles.push_back(tile.hint_view);
        }

        auto batch_results = common::measure_stage(
            on_stage, "tile_infer",
            [&]() { return infer_batch_raw(rgb_tiles, hint_tiles, params, on_stage); },
            pending_tiles.size());
        if (!batch_results) {
            return Unexpected(batch_results.error());
        }

        for (size_t tile_index = 0; tile_index < pending_tiles.size(); ++tile_index) {
            auto& tile = pending_tiles[tile_index];
            common::measure_stage(
                on_stage, "tile_accumulate",
                [&]() {
                    common::parallel_for_rows(tile_size, [&](int y_begin, int y_end) {
                        accumulate_tile_rows(mask, (*batch_results)[tile_index], acc_alpha.view(),
                                             acc_fg.view(), acc_weight.view(), tile.y_start,
                                             tile.x_start, h, w, y_begin, y_end);
                    });
                },
                1);
        }

        pending_tiles.clear();
        return {};
    };

    for (int ty = 0; ty < ny; ++ty) {
        for (int tx = 0; tx < nx; ++tx) {
            int x_start = tx * stride;
            int y_start = ty * stride;

            // Center crop if last tile goes over edge
            if (x_start + tile_size > w) x_start = std::max(0, w - tile_size);
            if (y_start + tile_size > h) y_start = std::max(0, h - tile_size);

            size_t tile_slot = pending_tiles.size();
            Image rgb_tile = m_tiled_rgb_pool[tile_slot].view();
            Image hint_tile = m_tiled_hint_pool[tile_slot].view();

            common::measure_stage(
                on_stage, "tile_extract",
                [&]() {
                    common::parallel_for_rows(tile_size, [&](int y_begin, int y_end) {
                        extract_tile_rows(rgb, alpha_hint, rgb_tile, hint_tile, y_start, x_start,
                                          y_begin, y_end);
                    });
                },
                1);

            pending_tiles.push_back(PendingTile{x_start, y_start, rgb_tile, hint_tile});
            if (pending_tiles.size() == static_cast<size_t>(tile_batch_size)) {
                auto flush_res = flush_pending_tiles();
                if (!flush_res) {
                    return Unexpected(flush_res.error());
                }
            }
        }
    }

    auto flush_res = flush_pending_tiles();
    if (!flush_res) {
        return Unexpected(flush_res.error());
    }

    common::measure_stage(
        on_stage, "tile_normalize",
        [&]() {
            common::parallel_for_rows(h, [&](int y_begin, int y_end) {
                normalize_accumulators(acc_alpha.view(), acc_fg.view(), acc_weight.view(), y_begin,
                                       y_end);
            });
        },
        1);

    FrameResult result;
    result.alpha = std::move(acc_alpha);
    result.foreground = std::move(acc_fg);

    common::measure_stage(
        on_stage, "tile_post_process", [&]() { apply_post_process(result, params, on_stage); }, 1);

    return result;
}

void InferenceSession::apply_post_process(FrameResult& result, const InferenceParams& params,
                                          StageTimingCallback on_stage) {
    if (result.alpha.view().empty() || result.foreground.view().empty()) return;

    int w = result.foreground.view().width;
    int h = result.foreground.view().height;

    // 1. Despeckle alpha
    if (params.auto_despeckle) {
        common::measure_stage(
            on_stage, "post_despeckle",
            [&]() { despeckle(result.alpha.view(), params.despeckle_size); }, 1);
    }

    common::measure_stage(
        on_stage, "post_despill",
        [&]() { despill(result.foreground.view(), params.despill_strength); }, 1);

    // 3. Generate processed: sRGB FG -> linear -> premultiply -> RGBA
    const auto& lut = SrgbLut::instance();
    Image fg = result.foreground.const_view();
    Image alpha_view = result.alpha.const_view();

    result.processed = ImageBuffer(w, h, 4);
    Image proc = result.processed.view();

    common::measure_stage(
        on_stage, "post_premultiply",
        [&]() {
            common::parallel_for_rows(h, [&](int y_begin, int y_end) {
                for (int y = y_begin; y < y_end; ++y) {
                    for (int x = 0; x < w; ++x) {
                        float a = alpha_view(y, x);
                        proc(y, x, 0) = lut.to_linear(fg(y, x, 0)) * a;
                        proc(y, x, 1) = lut.to_linear(fg(y, x, 1)) * a;
                        proc(y, x, 2) = lut.to_linear(fg(y, x, 2)) * a;
                        proc(y, x, 3) = a;
                    }
                }
            });
        },
        1);

    // 4. Composite on checker (linear space), then convert to sRGB
    result.composite = ImageBuffer(w, h, 4);
    Image comp = result.composite.view();
    common::measure_stage(
        on_stage, "post_composite",
        [&]() {
            std::copy(proc.data.begin(), proc.data.end(), comp.data.begin());
            ColorUtils::composite_over_checker(comp);
            ColorUtils::linear_to_srgb(comp);
        },
        1);
}

Result<std::vector<FrameResult>> InferenceSession::run_batch(const std::vector<Image>& rgbs,
                                                             const std::vector<Image>& alpha_hints,
                                                             const InferenceParams& params,
                                                             StageTimingCallback on_stage) {
    if (rgbs.empty()) return std::vector<FrameResult>{};

    // Tiling logic for batch (simplified: if first image needs tiling, we don't batch but run tiled
    // one by one) Actually, tiling should be handled at a higher level if we want to batch tiles.
    // For now, let's keep it simple: tiling disables batching.
    int target_res =
        params.target_resolution > 0 ? params.target_resolution : m_recommended_resolution;
    if (params.enable_tiling && (rgbs[0].width > target_res || rgbs[0].height > target_res)) {
        std::vector<FrameResult> results;
        for (size_t i = 0; i < rgbs.size(); ++i) {
            auto res = run_tiled(rgbs[i], alpha_hints[i], params, target_res, on_stage);
            if (!res) return Unexpected(res.error());
            results.push_back(std::move(*res));
        }
        return results;
    }

    auto results_res = infer_batch_raw(rgbs, alpha_hints, params, on_stage);
    if (!results_res) return results_res;

    for (auto& res : *results_res) {
        apply_post_process(res, params, on_stage);
    }
    return results_res;
}

Result<std::vector<FrameResult>> InferenceSession::infer_batch_raw(
    const std::vector<Image>& rgbs, const std::vector<Image>& alpha_hints,
    const InferenceParams& params, StageTimingCallback on_stage) {
    if (rgbs.empty()) return std::vector<FrameResult>{};
    if (m_mlx_session != nullptr) {
        if (params.target_resolution > 0 && params.target_resolution != m_recommended_resolution) {
            return Unexpected<Error>{Error{
                ErrorCode::InvalidParameters,
                "The current MLX bridge has a fixed resolution. Use --resolution 0 or prepare a "
                "matching bridge artifact."}};
        }

        std::vector<FrameResult> results;
        results.reserve(rgbs.size());
        for (size_t index = 0; index < rgbs.size(); ++index) {
            auto result = m_mlx_session->infer(rgbs[index], alpha_hints[index], on_stage);
            if (!result) {
                return Unexpected(result.error());
            }
            results.push_back(std::move(*result));
        }
        return results;
    }

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

            common::measure_stage(
                on_stage, "batch_prepare_inputs",
                [&]() {
                    for (size_t b = 0; b < batch_size; ++b) {
                        auto [padded_rgb, rgb_roi] =
                            ColorUtils::fit_pad(rgbs[b], (int)model_w, (int)model_h);
                        auto [padded_hint, hint_roi] =
                            ColorUtils::fit_pad(alpha_hints[b], (int)model_w, (int)model_h);
                        (void)hint_roi;
                        rois[b] = rgb_roi;

                        Image cur_rgb = padded_rgb.view();
                        Image cur_hint = padded_hint.view();
                        float* dst = dst_base + (b * image_stride);

                        for (int y = 0; y < model_h; ++y) {
                            for (int x = 0; x < model_w; ++x) {
                                size_t idx = y * model_w + x;
                                dst[0 * channel_stride + idx] =
                                    (cur_rgb(y, x, 0) - 0.485f) / 0.229f;
                                dst[1 * channel_stride + idx] =
                                    (cur_rgb(y, x, 1) - 0.456f) / 0.224f;
                                dst[2 * channel_stride + idx] =
                                    (cur_rgb(y, x, 2) - 0.406f) / 0.225f;
                                dst[3 * channel_stride + idx] = cur_hint(y, x, 0);
                            }
                        }
                    }
                },
                batch_size);

            std::vector<int64_t> effective_shape = {(int64_t)batch_size, 4, model_h, model_w};
            input_tensors.push_back(
                Ort::Value::CreateTensor<float>(memory_info, dst_base, total_planar_size,
                                                effective_shape.data(), effective_shape.size()));
        } else {
            return Unexpected(Error{ErrorCode::HardwareNotSupported,
                                    "Non-concatenated models not yet supported with batching"});
        }

        auto output_tensors = common::measure_stage(
            on_stage, "ort_run",
            [&]() {
                return m_session.Run(Ort::RunOptions{nullptr}, m_input_node_names_ptr.data(),
                                     input_tensors.data(), input_tensors.size(),
                                     m_output_node_names_ptr.data(),
                                     m_output_node_names_ptr.size());
            },
            batch_size);

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

        common::measure_stage(
            on_stage, "batch_extract_outputs",
            [&]() {
                for (size_t b = 0; b < batch_size; ++b) {
                    FrameResult& result = batch_results[b];
                    Rect& roi = rois[b];

                    ImageBuffer full_alpha((int)alpha_shape[3], (int)alpha_shape[2],
                                           (int)alpha_shape[1]);
                    ColorUtils::from_planar(alpha_ptr + (b * alpha_image_stride),
                                            full_alpha.view());
                    ImageBuffer cropped_alpha = ColorUtils::crop(full_alpha.view(), roi.x_pos,
                                                                 roi.y_pos, roi.width, roi.height);
                    result.alpha =
                        ColorUtils::resize(cropped_alpha.view(), rgbs[b].width, rgbs[b].height);

                    if (fg_ptr != nullptr) {
                        ImageBuffer full_fg((int)fg_shape[3], (int)fg_shape[2], (int)fg_shape[1]);
                        ColorUtils::from_planar(fg_ptr + (b * fg_image_stride), full_fg.view());
                        ImageBuffer cropped_fg = ColorUtils::crop(full_fg.view(), roi.x_pos,
                                                                  roi.y_pos, roi.width, roi.height);
                        result.foreground =
                            ColorUtils::resize(cropped_fg.view(), rgbs[b].width, rgbs[b].height);
                    }
                }
            },
            batch_size);

        return batch_results;

    } catch (const Ort::Exception& e) {
        return Unexpected(Error{ErrorCode::InferenceFailed,
                                std::string("ONNX Runtime execution failed: ") + e.what()});
    }
}

Result<FrameResult> InferenceSession::infer_raw(const Image& rgb, const Image& alpha_hint,
                                                const InferenceParams& params,
                                                StageTimingCallback on_stage) {
    auto batch_res = infer_batch_raw({rgb}, {alpha_hint}, params, on_stage);
    if (!batch_res) return Unexpected(batch_res.error());
    return std::move((*batch_res)[0]);
}

Result<FrameResult> InferenceSession::run(const Image& rgb, const Image& alpha_hint,
                                          const InferenceParams& params,
                                          StageTimingCallback on_stage) {
    // Check for tiling request
    int target_res =
        params.target_resolution > 0 ? params.target_resolution : m_recommended_resolution;

    // Only tile if requested and input is significantly larger than model
    if (params.enable_tiling && (rgb.width > target_res || rgb.height > target_res)) {
        return run_tiled(rgb, alpha_hint, params, target_res, on_stage);
    }

    auto result_res = infer_raw(rgb, alpha_hint, params, on_stage);
    if (!result_res) return result_res;

    apply_post_process(*result_res, params, on_stage);
    return result_res;
}

}  // namespace corridorkey
