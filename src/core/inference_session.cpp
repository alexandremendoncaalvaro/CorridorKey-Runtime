#include "inference_session.hpp"

#include <corridorkey/detail/constants.hpp>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <exception>
#include <fstream>
#include <mutex>
#include <new>

#include "coarse_to_fine_policy.hpp"
#include "common/parallel_for.hpp"
#include "common/runtime_paths.hpp"
#include "common/srgb_lut.hpp"
#include "common/stage_profiler.hpp"
#include "mlx_session.hpp"
#include "post_process/color_utils.hpp"
#include "post_process/despeckle.hpp"
#include "post_process/despill.hpp"
#include "post_process/source_passthrough.hpp"
#include "session_policy.hpp"
#include "tile_blend.hpp"
#include "torch_trt_session.hpp"

namespace corridorkey {

namespace {

void debug_log(const std::string& message) {
#ifdef _WIN32
    char* local_app_data = nullptr;
    size_t len = 0;
    if (_dupenv_s(&local_app_data, &len, "LOCALAPPDATA") == 0 && local_app_data != nullptr) {
        std::filesystem::path log_path =
            std::filesystem::path(local_app_data) / "CorridorKey" / "Logs" / "ofx.log";
        static std::mutex log_mutex;
        std::lock_guard<std::mutex> lock(log_mutex);
        std::ofstream log_file(log_path, std::ios::app);
        if (log_file.is_open()) {
            std::time_t now = std::time(nullptr);
            char buf[32];
            ctime_s(buf, sizeof(buf), &now);
            std::string ts(buf);
            if (!ts.empty() && ts.back() == '\n') ts.pop_back();
            log_file << ts << " [InferenceSession] " << message << std::endl;
        }
        free(local_app_data);
    }
#elif defined(__APPLE__)
    auto log_dir = corridorkey::common::default_logs_root();
    std::error_code ec;
    std::filesystem::create_directories(log_dir, ec);
    if (!ec) {
        auto log_path = log_dir / "ofx.log";
        static std::mutex log_mutex;
        std::lock_guard<std::mutex> lock(log_mutex);
        std::ofstream log_file(log_path, std::ios::app);
        if (log_file.is_open()) {
            std::time_t now = std::time(nullptr);
            char buf[32];
            auto* tm = std::localtime(&now);
            std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", tm);
            log_file << buf << " [InferenceSession] " << message << std::endl;
        }
    }
#else
    (void)message;
#endif
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
                          int image_height, int image_width, int overlap, int y_begin, int y_end) {
    Image tile_alpha = tile_result.alpha.const_view();
    Image tile_foreground = tile_result.foreground.const_view();
    const bool touches_left = x_start == 0;
    const bool touches_top = y_start == 0;
    const bool touches_right = x_start + mask.width >= image_width;
    const bool touches_bottom = y_start + mask.height >= image_height;
    const bool needs_edge_aware_weights =
        touches_left || touches_top || touches_right || touches_bottom;

    for (int y = y_begin; y < y_end; ++y) {
        int global_y = y_start + y;
        if (global_y >= image_height) break;
        for (int x = 0; x < mask.width; ++x) {
            int global_x = x_start + x;
            if (global_x >= image_width) break;

            float weight = mask(y, x);
            if (needs_edge_aware_weights) {
                weight = core::edge_aware_tile_weight(x, y, mask.width, overlap, touches_left,
                                                      touches_right, touches_top, touches_bottom);
            }
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
            if (weight <= 0.0001f) continue;
            acc_alpha.data[pixel_index] /= weight;
            size_t fg_index = pixel_index * 3;
            acc_fg.data[fg_index] /= weight;
            acc_fg.data[fg_index + 1] /= weight;
            acc_fg.data[fg_index + 2] /= weight;
        }
    }
}

}  // namespace

InferenceSession::InferenceSession(DeviceInfo device) : m_device(std::move(device)) {}

InferenceSession::~InferenceSession() = default;

Result<std::unique_ptr<InferenceSession>> InferenceSession::create(
    const std::filesystem::path& model_path, DeviceInfo device, SessionCreateOptions options) {
    auto session = std::unique_ptr<InferenceSession>(new InferenceSession(device));
    session->m_execution_engine = options.execution_engine;

#if defined(__APPLE__)
    if (device.backend == Backend::MLX) {
        auto mlx_res = core::MlxSession::create(model_path);
        if (!mlx_res) return Unexpected(mlx_res.error());
        session->m_mlx_session = std::move(*mlx_res);
        session->m_recommended_resolution = session->m_mlx_session->model_resolution();
        return std::move(session);
    }
#endif

#if defined(_WIN32)
    if (device.backend == Backend::TensorRT || device.backend == Backend::CPU ||
        device.backend == Backend::DirectML) {
        auto torch_res = core::TorchTrtSession::create(model_path, device);
        if (!torch_res) return Unexpected(torch_res.error());
        session->m_torch_trt_session = std::move(*torch_res);
        session->m_recommended_resolution = session->m_torch_trt_session->model_resolution();
        return std::move(session);
    }
#endif

    return Unexpected(
        Error{ErrorCode::HardwareNotSupported,
              "No compatible inference backend found for this platform/device combination."});
}

Result<std::vector<FrameResult>> InferenceSession::infer_batch_raw(
    const std::vector<Image>& rgbs, const std::vector<Image>& alpha_hints,
    const InferenceParams& params, StageTimingCallback on_stage) {
    if (rgbs.empty()) return std::vector<FrameResult>{};

    if (m_mlx_session != nullptr) {
        int model_res = m_mlx_session->model_resolution();
        std::vector<FrameResult> results;
        results.reserve(rgbs.size());
        for (size_t index = 0; index < rgbs.size(); ++index) {
            bool is_tile = rgbs[index].width == model_res && rgbs[index].height == model_res;
            if (is_tile) {
                auto result = m_mlx_session->infer_tile(rgbs[index], alpha_hints[index], on_stage);
                if (!result) return Unexpected(result.error());
                results.push_back(std::move(*result));
            } else {
                auto result = m_mlx_session->infer(rgbs[index], alpha_hints[index],
                                                   params.upscale_method, on_stage);
                if (!result) return Unexpected(result.error());
                results.push_back(std::move(*result));
            }
        }
        return results;
    }
    if (m_torch_trt_session != nullptr) {
        std::vector<FrameResult> results;
        results.reserve(rgbs.size());
        for (size_t index = 0; index < rgbs.size(); ++index) {
            auto result =
                m_torch_trt_session->infer(rgbs[index], alpha_hints[index], params, on_stage);
            if (!result) return Unexpected(result.error());
            results.push_back(std::move(*result));
        }
        return results;
    }
    return Unexpected(Error{ErrorCode::HardwareNotSupported, "No backend loaded."});
}

Result<FrameResult> InferenceSession::infer_raw(const Image& rgb, const Image& alpha_hint,
                                                const InferenceParams& params,
                                                StageTimingCallback on_stage) {
    if (m_mlx_session != nullptr) {
        auto batch_res = infer_batch_raw({rgb}, {alpha_hint}, params, on_stage);
        if (!batch_res) return Unexpected(batch_res.error());
        return std::move((*batch_res)[0]);
    }
    if (m_torch_trt_session != nullptr) {
        return m_torch_trt_session->infer(rgb, alpha_hint, params, on_stage);
    }
    return Unexpected(Error{ErrorCode::HardwareNotSupported, "No backend loaded."});
}

Result<FrameResult> InferenceSession::run_tiled(const Image& rgb, const Image& alpha_hint,
                                                const InferenceParams& params, int model_res,
                                                StageTimingCallback on_stage) {
    if (model_res <= 0) {
        return Unexpected(
            Error{ErrorCode::InvalidParameters, "Invalid model resolution for tiled inference."});
    }
    if (rgb.width <= 0 || rgb.height <= 0) {
        return Unexpected(
            Error{ErrorCode::InvalidParameters, "Invalid input size for tiled inference."});
    }

    struct PendingTile {
        int x_start = 0;
        int y_start = 0;
        Image rgb_view = {};
        Image hint_view = {};
    };

    try {
        int w = rgb.width;
        int h = rgb.height;
        int tile_size = model_res;
        int overlap = std::clamp(params.tile_padding, 0, tile_size - 1);
        int stride = core::tile_stride(tile_size, overlap);
        int tile_batch_size = m_device.backend == Backend::CPU ? std::max(1, params.batch_size) : 1;

        auto validate_buffer = [&](const ImageBuffer& buffer, int width, int height, int channels,
                                   const char* label) -> Result<void> {
            const size_t expected = static_cast<size_t>(width) * static_cast<size_t>(height) *
                                    static_cast<size_t>(channels);
            if (expected == 0) {
                return {};
            }
            if (buffer.const_view().data.size() != expected) {
                return Unexpected(
                    Error{ErrorCode::InferenceFailed,
                          std::string("Tiled inference failed to allocate ") + label + " buffer."});
            }
            return {};
        };

        // Allocate accumulators
        ImageBuffer acc_alpha(w, h, 1);
        ImageBuffer acc_fg(w, h, 3);
        ImageBuffer acc_weight(w, h, 1);

        auto acc_alpha_ok = validate_buffer(acc_alpha, w, h, 1, "alpha accumulator");
        if (!acc_alpha_ok) return Unexpected(acc_alpha_ok.error());
        auto acc_fg_ok = validate_buffer(acc_fg, w, h, 3, "foreground accumulator");
        if (!acc_fg_ok) return Unexpected(acc_fg_ok.error());
        auto acc_weight_ok = validate_buffer(acc_weight, w, h, 1, "weight accumulator");
        if (!acc_weight_ok) return Unexpected(acc_weight_ok.error());

        std::fill(acc_alpha.view().data.begin(), acc_alpha.view().data.end(), 0.0f);
        std::fill(acc_fg.view().data.begin(), acc_fg.view().data.end(), 0.0f);
        std::fill(acc_weight.view().data.begin(), acc_weight.view().data.end(), 0.0f);

        if (m_tiled_weight_mask.view().width != tile_size || m_tiled_weight_padding != overlap) {
            m_tiled_weight_mask = ImageBuffer(tile_size, tile_size, 1);
            Image mask_view = m_tiled_weight_mask.view();
            common::measure_stage(
                on_stage, "tile_prepare_weights",
                [&]() {
                    common::parallel_for_rows(tile_size, [&](int y_begin, int y_end) {
                        for (int y = y_begin; y < y_end; ++y) {
                            float wy = 1.0f;
                            if (overlap > 0 && y < overlap) {
                                wy = static_cast<float>(y) / static_cast<float>(overlap);
                            } else if (overlap > 0 && y >= tile_size - overlap) {
                                wy = static_cast<float>(tile_size - 1 - y) /
                                     static_cast<float>(overlap);
                            }

                            for (int x = 0; x < tile_size; ++x) {
                                float wx = 1.0f;
                                if (overlap > 0 && x < overlap) {
                                    wx = static_cast<float>(x) / static_cast<float>(overlap);
                                } else if (overlap > 0 && x >= tile_size - overlap) {
                                    wx = static_cast<float>(tile_size - 1 - x) /
                                         static_cast<float>(overlap);
                                }
                                mask_view(y, x) = std::min(wx, wy);
                            }
                        }
                    });
                },
                1);
            m_tiled_weight_padding = overlap;
        }

        auto mask_ok =
            validate_buffer(m_tiled_weight_mask, tile_size, tile_size, 1, "tile weight mask");
        if (!mask_ok) return Unexpected(mask_ok.error());

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

        for (const auto& tile : m_tiled_rgb_pool) {
            auto pool_ok = validate_buffer(tile, tile_size, tile_size, 3, "tile rgb pool");
            if (!pool_ok) return Unexpected(pool_ok.error());
        }
        for (const auto& tile : m_tiled_hint_pool) {
            auto pool_ok = validate_buffer(tile, tile_size, tile_size, 1, "tile hint pool");
            if (!pool_ok) return Unexpected(pool_ok.error());
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
                            accumulate_tile_rows(mask, (*batch_results)[tile_index],
                                                 acc_alpha.view(), acc_fg.view(), acc_weight.view(),
                                                 tile.y_start, tile.x_start, h, w, overlap, y_begin,
                                                 y_end);
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
                            extract_tile_rows(rgb, alpha_hint, rgb_tile, hint_tile, y_start,
                                              x_start, y_begin, y_end);
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
                    normalize_accumulators(acc_alpha.view(), acc_fg.view(), acc_weight.view(),
                                           y_begin, y_end);
                });
            },
            1);

        FrameResult result;
        result.alpha = std::move(acc_alpha);
        result.foreground = std::move(acc_fg);
        return result;
    } catch (const std::bad_alloc&) {
        return Unexpected(
            Error{ErrorCode::InferenceFailed, "Tiled inference failed: out of memory."});
    } catch (const std::exception& e) {
        return Unexpected(
            Error{ErrorCode::InferenceFailed, std::string("Tiled inference failed: ") + e.what()});
    } catch (...) {
        return Unexpected(
            Error{ErrorCode::InferenceFailed, "Tiled inference failed due to an unknown error."});
    }
}

void InferenceSession::apply_post_process(FrameResult& result, const InferenceParams& params,
                                          Image source_rgb, StageTimingCallback on_stage) {
    if (result.alpha.view().empty() || result.foreground.view().empty()) return;

    int w = result.foreground.view().width;
    int h = result.foreground.view().height;

    // 1. Source passthrough: blend original source into opaque regions (before despill
    //    so that despill can clean green spill from both model and source pixels)
    if (params.source_passthrough && !source_rgb.empty()) {
        common::measure_stage(
            on_stage, "post_source_passthrough",
            [&]() {
                corridorkey::source_passthrough(source_rgb, result.foreground.view(),
                                                result.alpha.view(), params.sp_erode_px,
                                                params.sp_blur_px, m_color_utils_state);
            },
            1);
    }

    // 2. Despeckle alpha
    if (params.auto_despeckle) {
        common::measure_stage(
            on_stage, "post_despeckle",
            [&]() { despeckle(result.alpha.view(), params.despeckle_size, m_despeckle_state); }, 1);
    }

    // 3. Despill foreground (operates on combined fg after source passthrough)
    common::measure_stage(
        on_stage, "post_despill",
        [&]() {
            despill(result.foreground.view(), params.despill_strength,
                    static_cast<SpillMethod>(params.spill_method));
        },
        1);

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

    // 5. Composite on checker (linear space), then convert to sRGB
    result.composite = ImageBuffer(w, h, 4);
    Image comp = result.composite.view();
    const float bg_dark = lut.to_linear(0.15F);
    const float bg_light = lut.to_linear(0.55F);
    common::measure_stage(
        on_stage, "post_composite",
        [&]() {
            common::parallel_for_rows(h, [&](int y_begin, int y_end) {
                for (int y = y_begin; y < y_end; ++y) {
                    for (int x = 0; x < w; ++x) {
                        const float alpha = proc(y, x, 3);
                        const bool use_dark_checker = ((y / 16) + (x / 16)) % 2 == 0;
                        const float bg_value = use_dark_checker ? bg_dark : bg_light;
                        comp(y, x, 0) =
                            lut.to_srgb(proc(y, x, 0) * alpha + bg_value * (1.0F - alpha));
                        comp(y, x, 1) =
                            lut.to_srgb(proc(y, x, 1) * alpha + bg_value * (1.0F - alpha));
                        comp(y, x, 2) =
                            lut.to_srgb(proc(y, x, 2) * alpha + bg_value * (1.0F - alpha));
                        comp(y, x, 3) = 1.0F;
                    }
                }
            });
        },
        1);
}

Result<FrameResult> InferenceSession::run_direct(const Image& rgb, const Image& alpha_hint,
                                                 const InferenceParams& params,
                                                 StageTimingCallback on_stage) {
    const int model_resolution = m_recommended_resolution > 0 ? m_recommended_resolution : 512;

    if (params.enable_tiling && (rgb.width > model_resolution || rgb.height > model_resolution)) {
        auto tiled_result = run_tiled(rgb, alpha_hint, params, model_resolution, on_stage);
        if (!tiled_result) {
            return Unexpected(tiled_result.error());
        }
        apply_post_process(*tiled_result, params, rgb, on_stage);
        return tiled_result;
    }

    auto result = infer_raw(rgb, alpha_hint, params, on_stage);
    if (!result) {
        return Unexpected(result.error());
    }
    apply_post_process(*result, params, rgb, on_stage);
    return result;
}

Result<FrameResult> InferenceSession::run_coarse_to_fine(const Image& rgb, const Image& alpha_hint,
                                                         const InferenceParams& params,
                                                         StageTimingCallback on_stage) {
    const int coarse_resolution = m_recommended_resolution > 0 ? m_recommended_resolution : 512;
    InferenceParams coarse_params = core::coarse_inference_params(params, coarse_resolution);

    debug_log("event=quality_path mode=coarse_to_fine requested_resolution=" +
              std::to_string(core::requested_quality_resolution(params, coarse_resolution)) +
              " coarse_resolution=" + std::to_string(coarse_resolution) +
              " strategy=artifact_fallback_only");

    auto raw_result = infer_raw(rgb, alpha_hint, coarse_params, on_stage);
    if (!raw_result) {
        return Unexpected(raw_result.error());
    }
    apply_post_process(*raw_result, params, rgb, on_stage);
    return raw_result;
}

Result<std::vector<FrameResult>> InferenceSession::run_batch(const std::vector<Image>& rgbs,
                                                             const std::vector<Image>& alpha_hints,
                                                             const InferenceParams& params,
                                                             StageTimingCallback on_stage) {
    if (rgbs.empty()) return std::vector<FrameResult>{};

    if (core::should_use_coarse_to_fine_path(params, m_recommended_resolution)) {
        const int coarse_resolution = m_recommended_resolution > 0 ? m_recommended_resolution : 512;
        InferenceParams coarse_params = core::coarse_inference_params(params, coarse_resolution);
        auto results_res = infer_batch_raw(rgbs, alpha_hints, coarse_params, on_stage);
        if (!results_res) {
            return Unexpected(results_res.error());
        }
        for (size_t index = 0; index < results_res->size(); ++index) {
            apply_post_process((*results_res)[index], params, rgbs[index], on_stage);
        }
        return results_res;
    }

    // Tiling logic for batch (simplified: if first image needs tiling, we don't batch but run tiled
    // one by one) Actually, tiling should be handled at a higher level if we want to batch tiles.
    // For now, let's keep it simple: tiling disables batching.
    const int model_resolution = m_recommended_resolution > 0 ? m_recommended_resolution : 512;
    if (params.enable_tiling &&
        (rgbs[0].width > model_resolution || rgbs[0].height > model_resolution)) {
        std::vector<FrameResult> results;
        for (size_t i = 0; i < rgbs.size(); ++i) {
            auto res = run_tiled(rgbs[i], alpha_hints[i], params, model_resolution, on_stage);
            if (!res) return Unexpected(res.error());
            apply_post_process(*res, params, rgbs[i], on_stage);
            results.push_back(std::move(*res));
        }
        return results;
    }

    auto results_res = infer_batch_raw(rgbs, alpha_hints, params, on_stage);
    if (!results_res) return results_res;

    for (size_t i = 0; i < results_res->size(); ++i) {
        apply_post_process((*results_res)[i], params, rgbs[i], on_stage);
    }
    return results_res;
}

Result<FrameResult> InferenceSession::run(const Image& rgb, const Image& alpha_hint,
                                          const InferenceParams& params,
                                          StageTimingCallback on_stage) {
    if (core::should_use_coarse_to_fine_path(params, m_recommended_resolution)) {
        return run_coarse_to_fine(rgb, alpha_hint, params, on_stage);
    }

    return run_direct(rgb, alpha_hint, params, on_stage);
}

}  // namespace corridorkey
