#include <algorithm>
#include <chrono>
#include <cmath>
#include <corridorkey/engine.hpp>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>

#include "common/srgb_lut.hpp"
#include "ofx_image_utils.hpp"
#include "ofx_logging.hpp"
#include "ofx_model_selection.hpp"
#include "ofx_runtime_client.hpp"
#include "ofx_shared.hpp"
#include "post_process/alpha_edge.hpp"
#include "post_process/color_utils.hpp"

namespace corridorkey::ofx {

namespace {

std::string backend_label(Backend backend) {
    switch (backend) {
        case Backend::CPU:
            return "cpu";
        case Backend::CoreML:
            return "coreml";
        case Backend::CUDA:
            return "cuda";
        case Backend::TensorRT:
            return "tensorrt";
        case Backend::DirectML:
            return "dml";
        case Backend::WindowsML:
            return "winml";
        case Backend::OpenVINO:
            return "openvino";
        case Backend::MLX:
            return "mlx";
        default:
            return "auto";
    }
}

std::string render_phase_label(std::uint64_t render_count) {
    return render_count == 0 ? "first_frame" : "subsequent_frame";
}

DeviceInfo requested_device_for_render(const InstanceData* data) {
    if (data == nullptr) {
        return {};
    }
    if (data->preferred_device.backend == Backend::Auto) {
        return data->device;
    }
    return data->preferred_device;
}

DeviceInfo effective_device_for_render_log(const InstanceData* data) {
    if (data == nullptr) {
        return {};
    }
    if (data->use_runtime_server) {
        if (data->runtime_client != nullptr && data->runtime_client->has_session()) {
            return data->runtime_client->current_device();
        }
        return data->device;
    }
    if (data->engine != nullptr) {
        return data->engine->current_device();
    }
    return data->device;
}

void log_render_stage(std::string_view phase, const DeviceInfo& requested_device,
                      const std::filesystem::path& artifact_path, int requested_resolution,
                      int effective_resolution, const StageTiming& timing) {
    log_message("render", "event=stage phase=" + std::string(phase) + " stage=" + timing.name +
                              " total_ms=" + std::to_string(timing.total_ms) +
                              " requested_backend=" + backend_label(requested_device.backend) +
                              " artifact=" + artifact_path.filename().string() +
                              " requested_resolution=" + std::to_string(requested_resolution) +
                              " effective_resolution=" + std::to_string(effective_resolution));
}

void log_render_event(std::string_view event, std::string_view phase,
                      const DeviceInfo& requested_device, const DeviceInfo& effective_device,
                      const std::filesystem::path& artifact_path, int requested_resolution,
                      int effective_resolution,
                      const std::optional<BackendFallbackInfo>& fallback = std::nullopt,
                      std::string_view detail = {}) {
    std::string message = "event=" + std::string(event) + " phase=" + std::string(phase) +
                          " requested_backend=" + backend_label(requested_device.backend) +
                          " effective_backend=" + backend_label(effective_device.backend) +
                          " requested_device=" + requested_device.name +
                          " effective_device=" + effective_device.name +
                          " artifact=" + artifact_path.filename().string() +
                          " requested_resolution=" + std::to_string(requested_resolution) +
                          " effective_resolution=" + std::to_string(effective_resolution);
    if (fallback.has_value() && !fallback->reason.empty()) {
        message += " fallback_reason=" + fallback->reason;
    }
    if (!detail.empty()) {
        message += " detail=" + std::string(detail);
    }
    log_message("render", message);
}

void record_frame_timing(InstanceData* data, double elapsed_ms) {
    if (data == nullptr || elapsed_ms <= 0.0) {
        return;
    }
    data->last_frame_ms = elapsed_ms;
    if (data->frame_time_samples == 0 || data->avg_frame_ms <= 0.0) {
        data->avg_frame_ms = elapsed_ms;
    } else {
        constexpr double kSmoothing = 0.2;
        data->avg_frame_ms = (1.0 - kSmoothing) * data->avg_frame_ms + kSmoothing * elapsed_ms;
    }
    ++data->frame_time_samples;
    data->runtime_panel_dirty = true;
}

void set_runtime_error(InstanceData* data, const std::string& message,
                       OfxImageEffectHandle instance) {
    if (data != nullptr) {
        data->last_error = message;
        data->cached_result_valid = false;
        data->runtime_panel_dirty = true;
        update_runtime_panel(data);
    }
    post_message(kOfxMessageError, message.c_str(), instance);
}

bool inference_params_equal(const InferenceParams& lhs, const InferenceParams& rhs) {
    return lhs.target_resolution == rhs.target_resolution &&
           lhs.despill_strength == rhs.despill_strength && lhs.spill_method == rhs.spill_method &&
           lhs.auto_despeckle == rhs.auto_despeckle && lhs.despeckle_size == rhs.despeckle_size &&
           lhs.refiner_scale == rhs.refiner_scale && lhs.input_is_linear == rhs.input_is_linear &&
           lhs.batch_size == rhs.batch_size && lhs.enable_tiling == rhs.enable_tiling &&
           lhs.tile_padding == rhs.tile_padding && lhs.upscale_method == rhs.upscale_method &&
           lhs.source_passthrough == rhs.source_passthrough && lhs.sp_erode_px == rhs.sp_erode_px &&
           lhs.sp_blur_px == rhs.sp_blur_px;
}

std::uint64_t mix_signature(std::uint64_t hash, float value) {
    std::uint32_t bits = 0;
    static_assert(sizeof(float) == sizeof(bits));
    std::memcpy(&bits, &value, sizeof(bits));
    hash ^= static_cast<std::uint64_t>(bits);
    return hash * 1099511628211ULL;
}

std::uint64_t frame_signature(const Image& rgb, const Image& hint) {
    constexpr std::uint64_t kOffsetBasis = 1469598103934665603ULL;
    std::uint64_t hash = kOffsetBasis;
    for (float value : rgb.data) {
        hash = mix_signature(hash, value);
    }
    if (hint.width == rgb.width && hint.height == rgb.height) {
        for (float value : hint.data) {
            hash = mix_signature(hash, value);
        }
    }
    hash = mix_signature(hash, static_cast<float>(rgb.width));
    hash = mix_signature(hash, static_cast<float>(rgb.height));
    hash = mix_signature(hash, static_cast<float>(rgb.channels));
    return hash;
}

class RenderScope {
   public:
    explicit RenderScope(InstanceData* data) : m_data(data) {
        if (m_data != nullptr) {
            m_data->in_render = true;
        }
    }

    ~RenderScope() {
        if (m_data != nullptr) {
            m_data->in_render = false;
            flush_runtime_panel(m_data);
        }
    }

    RenderScope(const RenderScope&) = delete;
    RenderScope& operator=(const RenderScope&) = delete;

   private:
    InstanceData* m_data = nullptr;
};

void swap_green_blue(Image image) {
    if (image.channels < 3) {
        return;
    }
    for (int y = 0; y < image.height; ++y) {
        for (int x = 0; x < image.width; ++x) {
            std::swap(image(y, x, 1), image(y, x, 2));
        }
    }
}

void write_rgba_pixel(float r, float g, float b, float a, unsigned char* dst, bool is_float,
                      bool is_byte, bool apply_srgb, const SrgbLut& lut) {
    if (is_float) {
        float* dst_pixel = reinterpret_cast<float*>(dst);
        if (apply_srgb) {
            if (a > 0.0f) {
                float inv_a = 1.0f / a;
                dst_pixel[0] = lut.to_srgb(r * inv_a) * a;
                dst_pixel[1] = lut.to_srgb(g * inv_a) * a;
                dst_pixel[2] = lut.to_srgb(b * inv_a) * a;
            } else {
                dst_pixel[0] = 0.0f;
                dst_pixel[1] = 0.0f;
                dst_pixel[2] = 0.0f;
            }
        } else {
            dst_pixel[0] = r;
            dst_pixel[1] = g;
            dst_pixel[2] = b;
        }
        dst_pixel[3] = a;
        return;
    }

    if (is_byte) {
        unsigned char* dst_pixel = dst;
        float sr = 0.0f;
        float sg = 0.0f;
        float sb = 0.0f;
        if (a > 0.0f) {
            float inv_a = 1.0f / a;
            sr = lut.to_srgb(r * inv_a) * a;
            sg = lut.to_srgb(g * inv_a) * a;
            sb = lut.to_srgb(b * inv_a) * a;
        }
        dst_pixel[0] = static_cast<unsigned char>(std::clamp(sr * 255.0f + 0.5f, 0.0f, 255.0f));
        dst_pixel[1] = static_cast<unsigned char>(std::clamp(sg * 255.0f + 0.5f, 0.0f, 255.0f));
        dst_pixel[2] = static_cast<unsigned char>(std::clamp(sb * 255.0f + 0.5f, 0.0f, 255.0f));
        dst_pixel[3] = static_cast<unsigned char>(std::clamp(a * 255.0f + 0.5f, 0.0f, 255.0f));
    }
}

void write_matte_output(const Image& alpha, void* dst_data, int row_bytes, const std::string& depth,
                        const SrgbLut& lut) {
    const bool is_float = is_depth(depth, kOfxBitDepthFloat);
    const bool is_byte = is_depth(depth, kOfxBitDepthByte);
    for (int y = 0; y < alpha.height; ++y) {
        auto row = reinterpret_cast<unsigned char*>(dst_data) +
                   static_cast<ptrdiff_t>(y) * static_cast<ptrdiff_t>(row_bytes);
        for (int x = 0; x < alpha.width; ++x) {
            float a = alpha(y, x);
            write_rgba_pixel(a, a, a, 1.0f, row + x * 4 * (is_float ? 4 : 1), is_float, is_byte,
                             false, lut);
        }
    }
}

void write_foreground_output(const Image& fg_linear, void* dst_data, int row_bytes,
                             const std::string& depth, bool apply_srgb, const SrgbLut& lut) {
    const bool is_float = is_depth(depth, kOfxBitDepthFloat);
    const bool is_byte = is_depth(depth, kOfxBitDepthByte);
    for (int y = 0; y < fg_linear.height; ++y) {
        auto row = reinterpret_cast<unsigned char*>(dst_data) +
                   static_cast<ptrdiff_t>(y) * static_cast<ptrdiff_t>(row_bytes);
        for (int x = 0; x < fg_linear.width; ++x) {
            float r = fg_linear(y, x, 0);
            float g = fg_linear(y, x, 1);
            float b = fg_linear(y, x, 2);
            write_rgba_pixel(r, g, b, 1.0f, row + x * 4 * (is_float ? 4 : 1), is_float, is_byte,
                             apply_srgb, lut);
        }
    }
}

void write_processed_output(const Image& fg_linear, const Image& alpha, void* dst_data,
                            int row_bytes, const std::string& depth, bool apply_srgb,
                            const SrgbLut& lut) {
    const bool is_float = is_depth(depth, kOfxBitDepthFloat);
    const bool is_byte = is_depth(depth, kOfxBitDepthByte);
    for (int y = 0; y < fg_linear.height; ++y) {
        auto row = reinterpret_cast<unsigned char*>(dst_data) +
                   static_cast<ptrdiff_t>(y) * static_cast<ptrdiff_t>(row_bytes);
        for (int x = 0; x < fg_linear.width; ++x) {
            float a = alpha(y, x);
            float r = fg_linear(y, x, 0) * a;
            float g = fg_linear(y, x, 1) * a;
            float b = fg_linear(y, x, 2) * a;
            write_rgba_pixel(r, g, b, a, row + x * 4 * (is_float ? 4 : 1), is_float, is_byte,
                             apply_srgb, lut);
        }
    }
}

void write_source_matte_output(const Image& rgb_srgb, const Image& alpha, void* dst_data,
                               int row_bytes, const std::string& depth, bool apply_srgb,
                               const SrgbLut& lut) {
    const bool is_float = is_depth(depth, kOfxBitDepthFloat);
    const bool is_byte = is_depth(depth, kOfxBitDepthByte);
    for (int y = 0; y < rgb_srgb.height; ++y) {
        auto row = reinterpret_cast<unsigned char*>(dst_data) +
                   static_cast<ptrdiff_t>(y) * static_cast<ptrdiff_t>(row_bytes);
        for (int x = 0; x < rgb_srgb.width; ++x) {
            float a = alpha(y, x);
            float r = lut.to_linear(rgb_srgb(y, x, 0)) * a;
            float g = lut.to_linear(rgb_srgb(y, x, 1)) * a;
            float b = lut.to_linear(rgb_srgb(y, x, 2)) * a;
            write_rgba_pixel(r, g, b, a, row + x * 4 * (is_float ? 4 : 1), is_float, is_byte,
                             apply_srgb, lut);
        }
    }
}

void bypass_with_source(const void* source_data, void* output_data, int width, int height,
                        int source_row_bytes, int output_row_bytes,
                        const std::string& source_depth) {
    const int pixel_bytes = is_depth(source_depth, kOfxBitDepthFloat) ? 16 : 4;
    const int copy_bytes = width * pixel_bytes;
    for (int y = 0; y < height; ++y) {
        const auto* src_row = reinterpret_cast<const unsigned char*>(source_data) +
                              static_cast<ptrdiff_t>(y) * static_cast<ptrdiff_t>(source_row_bytes);
        auto* dst_row = reinterpret_cast<unsigned char*>(output_data) +
                        static_cast<ptrdiff_t>(y) * static_cast<ptrdiff_t>(output_row_bytes);
        std::memcpy(dst_row, src_row, static_cast<size_t>(copy_bytes));
    }
}

}  // namespace

OfxStatus render(OfxImageEffectHandle instance, OfxPropertySetHandle in_args,
                 OfxPropertySetHandle /*out_args*/) {
    if (g_suites.image_effect == nullptr || g_suites.property == nullptr ||
        g_suites.parameter == nullptr) {
        log_message("render", "Missing required suites.");
        return kOfxStatErrMissingHostFeature;
    }

    InstanceData* data = get_instance_data(instance);
    if (data == nullptr || (data->use_runtime_server && data->runtime_client == nullptr) ||
        (!data->use_runtime_server && data->engine == nullptr)) {
        log_message("render", "Engine is not ready.");
        set_runtime_error(data, "CorridorKey engine is not ready.", instance);
        return kOfxStatFailed;
    }

    RenderScope render_scope(data);
    const auto render_start = std::chrono::steady_clock::now();

    double time = 0.0;
    if (in_args != nullptr) {
        get_double(in_args, kOfxPropTime, time);
    }

    OfxPropertySetHandle source_props = nullptr;
    if (!fetch_image(data->source_clip, time, source_props)) {
        log_message("render", "Failed to fetch source image.");
        set_runtime_error(data, "Failed to fetch source image.", instance);
        return kOfxStatFailed;
    }
    ImageHandleGuard source_guard{source_props};

    OfxPropertySetHandle output_props = nullptr;
    if (!fetch_image(data->output_clip, time, output_props)) {
        log_message("render", "Failed to fetch output image.");
        set_runtime_error(data, "Failed to fetch output image.", instance);
        return kOfxStatFailed;
    }
    ImageHandleGuard output_guard{output_props};

    void* source_data = nullptr;
    if (g_suites.property->propGetPointer(source_props, kOfxImagePropData, 0, &source_data) !=
            kOfxStatOK ||
        source_data == nullptr) {
        log_message("render", "Source image data unavailable.");
        set_runtime_error(data, "Source image data is unavailable.", instance);
        return kOfxStatFailed;
    }

    void* output_data = nullptr;
    if (g_suites.property->propGetPointer(output_props, kOfxImagePropData, 0, &output_data) !=
            kOfxStatOK ||
        output_data == nullptr) {
        log_message("render", "Output image data unavailable.");
        set_runtime_error(data, "Output image data is unavailable.", instance);
        return kOfxStatFailed;
    }

    OfxRectI source_bounds{};
    if (!get_rect_i(source_props, kOfxImagePropBounds, source_bounds)) {
        log_message("render", "Source bounds unavailable.");
        set_runtime_error(data, "Source bounds are unavailable.", instance);
        return kOfxStatFailed;
    }

    int source_row_bytes = 0;
    if (!get_int(source_props, kOfxImagePropRowBytes, source_row_bytes)) {
        log_message("render", "Source row bytes unavailable.");
        set_runtime_error(data, "Source row bytes are unavailable.", instance);
        return kOfxStatFailed;
    }

    int output_row_bytes = 0;
    if (!get_int(output_props, kOfxImagePropRowBytes, output_row_bytes)) {
        log_message("render", "Output row bytes unavailable.");
        set_runtime_error(data, "Output row bytes are unavailable.", instance);
        return kOfxStatFailed;
    }

    std::string source_depth;
    std::string source_components;
    if (!get_string(source_props, kOfxImageEffectPropPixelDepth, source_depth) ||
        !get_string(source_props, kOfxImageEffectPropComponents, source_components)) {
        log_message("render", "Source format unavailable.");
        set_runtime_error(data, "Source format is unavailable.", instance);
        return kOfxStatFailed;
    }
    if (!is_depth(source_depth, kOfxBitDepthFloat) && !is_depth(source_depth, kOfxBitDepthByte)) {
        log_message("render", "Unsupported source bit depth.");
        set_runtime_error(data, "Unsupported source bit depth.", instance);
        return kOfxStatFailed;
    }
    if (source_components != kOfxImageComponentRGBA) {
        log_message("render", "Unsupported source components.");
        set_runtime_error(data, "Only RGBA source images are supported.", instance);
        return kOfxStatFailed;
    }

    int width = source_bounds.x2 - source_bounds.x1;
    int height = source_bounds.y2 - source_bounds.y1;
    if (width <= 0 || height <= 0) {
        log_message("render", "Invalid source bounds.");
        set_runtime_error(data, "Invalid source bounds.", instance);
        return kOfxStatFailed;
    }

    int quality_mode = kQualityAuto;
    int output_mode = kOutputProcessed;
    int input_color_space = kDefaultInputColorSpace;
    int quantization_mode = kDefaultQuantizationMode;
    int screen_color = kDefaultScreenColor;
    double temporal_smoothing = kDefaultTemporalSmoothing;
    int despeckle_enabled = 0;
    int despeckle_size = 400;
    double despill_strength = 0.5;
    int spill_method = kDefaultSpillMethod;
    double alpha_black_point = 0.0;
    double alpha_white_point = 1.0;
    double alpha_erode = 0.0;
    double alpha_softness = 0.0;
    double alpha_gamma = 1.0;
    int upscale_method = kUpscaleBilinear;
    int enable_tiling = 0;
    int tile_overlap = 64;
    int source_passthrough_enabled = kDefaultSourcePassthroughEnabled;
    int edge_erode = kDefaultEdgeErode;
    int edge_blur = kDefaultEdgeBlur;

    if (data->quality_mode_param) {
        g_suites.parameter->paramGetValueAtTime(data->quality_mode_param, time, &quality_mode);
    }
    if (data->input_color_space_param) {
        g_suites.parameter->paramGetValueAtTime(data->input_color_space_param, time,
                                                &input_color_space);
    }
    if (data->quantization_mode_param) {
        g_suites.parameter->paramGetValueAtTime(data->quantization_mode_param, time,
                                                &quantization_mode);
    }
    if (data->screen_color_param) {
        g_suites.parameter->paramGetValueAtTime(data->screen_color_param, time, &screen_color);
    }
    if (data->temporal_smoothing_param) {
        g_suites.parameter->paramGetValueAtTime(data->temporal_smoothing_param, time,
                                                &temporal_smoothing);
    }

    bool input_is_linear = false;
    switch (input_color_space) {
        case kInputColorLinear:
            input_is_linear = true;
            break;
        case kInputColorSrgb:
        default:
            input_is_linear = false;
            break;
    }

    std::string output_depth;
    if (!get_string(output_props, kOfxImageEffectPropPixelDepth, output_depth)) {
        output_depth = source_depth;
    }
    if (!is_depth(output_depth, kOfxBitDepthFloat) && !is_depth(output_depth, kOfxBitDepthByte)) {
        log_message("render", "Unsupported output bit depth.");
        set_runtime_error(data, "Unsupported output bit depth.", instance);
        return kOfxStatFailed;
    }

    ImageBuffer rgb_buffer(width, height, 3);
    ImageBuffer hint_buffer(width, height, 1);
    Image rgb_view = rgb_buffer.view();
    Image hint_view = hint_buffer.view();

    copy_source_to_linear(rgb_view, source_data, source_row_bytes, source_depth);

    if (input_is_linear) {
        const SrgbLut& lut = SrgbLut::instance();
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                rgb_view(y, x, 0) = lut.to_srgb(rgb_view(y, x, 0));
                rgb_view(y, x, 1) = lut.to_srgb(rgb_view(y, x, 1));
                rgb_view(y, x, 2) = lut.to_srgb(rgb_view(y, x, 2));
            }
        }
    }

    const bool swap_screen = screen_color == kScreenColorBlue;
    if (swap_screen) {
        swap_green_blue(rgb_view);
    }
    if (!ensure_engine_for_quality(data, quality_mode, width, height, quantization_mode)) {
        const std::string quality_error =
            data->last_error.empty() ? "Failed to switch quality mode." : data->last_error;
        if (is_fixed_quality_mode(quality_mode)) {
            log_message("render", quality_error);
            set_runtime_error(data, quality_error, instance);
            bypass_with_source(source_data, output_data, width, height, source_row_bytes,
                               output_row_bytes, source_depth);
            return kOfxStatOK;
        }
        log_message("render", quality_error + " Using current engine.");
    }
    if (data->output_mode_param) {
        g_suites.parameter->paramGetValueAtTime(data->output_mode_param, time, &output_mode);
    }
    if (data->despeckle_param) {
        g_suites.parameter->paramGetValueAtTime(data->despeckle_param, time, &despeckle_enabled);
    }
    if (data->despeckle_size_param) {
        g_suites.parameter->paramGetValueAtTime(data->despeckle_size_param, time, &despeckle_size);
    }
    if (data->despill_param) {
        g_suites.parameter->paramGetValueAtTime(data->despill_param, time, &despill_strength);
    }
    if (data->spill_method_param) {
        g_suites.parameter->paramGetValueAtTime(data->spill_method_param, time, &spill_method);
    }
    if (data->alpha_black_point_param) {
        g_suites.parameter->paramGetValueAtTime(data->alpha_black_point_param, time,
                                                &alpha_black_point);
    }
    if (data->alpha_white_point_param) {
        g_suites.parameter->paramGetValueAtTime(data->alpha_white_point_param, time,
                                                &alpha_white_point);
    }
    if (data->alpha_erode_param) {
        g_suites.parameter->paramGetValueAtTime(data->alpha_erode_param, time, &alpha_erode);
    }
    if (data->alpha_softness_param) {
        g_suites.parameter->paramGetValueAtTime(data->alpha_softness_param, time, &alpha_softness);
    }
    if (data->alpha_gamma_param) {
        g_suites.parameter->paramGetValueAtTime(data->alpha_gamma_param, time, &alpha_gamma);
    }
    if (data->upscale_method_param) {
        g_suites.parameter->paramGetValueAtTime(data->upscale_method_param, time, &upscale_method);
    }
    if (data->enable_tiling_param) {
        g_suites.parameter->paramGetValueAtTime(data->enable_tiling_param, time, &enable_tiling);
    }
    if (data->tile_overlap_param) {
        g_suites.parameter->paramGetValueAtTime(data->tile_overlap_param, time, &tile_overlap);
    }
    if (data->source_passthrough_param) {
        g_suites.parameter->paramGetValueAtTime(data->source_passthrough_param, time,
                                                &source_passthrough_enabled);
    }
    if (data->edge_erode_param) {
        g_suites.parameter->paramGetValueAtTime(data->edge_erode_param, time, &edge_erode);
    }
    if (data->edge_blur_param) {
        g_suites.parameter->paramGetValueAtTime(data->edge_blur_param, time, &edge_blur);
    }

    bool hint_from_clip = false;
    OfxPropertySetHandle hint_props = nullptr;
    ImageHandleGuard hint_guard{};

    if (is_clip_connected(data->alpha_hint_clip)) {
        if (fetch_image(data->alpha_hint_clip, time, hint_props)) {
            hint_guard.handle = hint_props;
            void* hint_data = nullptr;
            int hint_row_bytes = 0;
            std::string hint_depth;
            std::string hint_components;

            if (g_suites.property->propGetPointer(hint_props, kOfxImagePropData, 0, &hint_data) ==
                    kOfxStatOK &&
                hint_data != nullptr &&
                get_int(hint_props, kOfxImagePropRowBytes, hint_row_bytes) &&
                get_string(hint_props, kOfxImageEffectPropPixelDepth, hint_depth) &&
                get_string(hint_props, kOfxImageEffectPropComponents, hint_components)) {
                copy_alpha_hint(hint_view, hint_data, hint_row_bytes, hint_depth, hint_components);
                hint_from_clip = true;
                log_message(
                    "render",
                    "Using external alpha hint from connected clip. components=" + hint_components +
                        " interpretation=" + alpha_hint_interpretation_label(hint_components));
            } else {
                log_message("render",
                            "Connected alpha hint clip could not be read. "
                            "Falling back to rough matte generation.");
            }
        } else {
            log_message("render",
                        "Connected alpha hint clip could not be fetched. "
                        "Falling back to rough matte generation.");
        }
    }

    if (!hint_from_clip) {
        const std::string message = "Waiting for Alpha Hint connection.";
        log_message("render", message);

        if (data != nullptr) {
            data->last_error = message;
            data->cached_result_valid = false;
            data->runtime_panel_dirty = true;
            update_runtime_panel(data);
        }

        ImageBuffer alpha_buf(width, height, 1);
        ImageBuffer fg_buf(width, height, 3);
        auto alpha_view = alpha_buf.view();
        auto fg_linear = fg_buf.view();
        const SrgbLut& lut = SrgbLut::instance();

        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                alpha_view(y, x) = 1.0f;
                if (swap_screen) {
                    fg_linear(y, x, 0) = lut.to_linear(rgb_view(y, x, 0));
                    fg_linear(y, x, 1) = lut.to_linear(rgb_view(y, x, 2));
                    fg_linear(y, x, 2) = lut.to_linear(rgb_view(y, x, 1));
                } else {
                    fg_linear(y, x, 0) = lut.to_linear(rgb_view(y, x, 0));
                    fg_linear(y, x, 1) = lut.to_linear(rgb_view(y, x, 1));
                    fg_linear(y, x, 2) = lut.to_linear(rgb_view(y, x, 2));
                }
            }
        }

        bool apply_srgb = should_apply_srgb_to_output(output_mode, input_is_linear);
        if (output_mode == kOutputMatteOnly) {
            write_matte_output(alpha_view, output_data, output_row_bytes, output_depth,
                               SrgbLut::instance());
        } else if (output_mode == kOutputForegroundOnly) {
            write_foreground_output(fg_linear, output_data, output_row_bytes, output_depth,
                                    apply_srgb, SrgbLut::instance());
        } else if (output_mode == kOutputSourceMatte) {
            write_source_matte_output(rgb_view, alpha_view, output_data, output_row_bytes,
                                      output_depth, apply_srgb, SrgbLut::instance());
        } else if (output_mode_uses_linear_premultiplied_rgba(output_mode)) {
            write_processed_output(fg_linear, alpha_view, output_data, output_row_bytes,
                                   output_depth, false, SrgbLut::instance());
        } else {
            write_processed_output(fg_linear, alpha_view, output_data, output_row_bytes,
                                   output_depth, apply_srgb, SrgbLut::instance());
        }
        return kOfxStatOK;
    }

    const std::uint64_t signature = frame_signature(rgb_view, hint_view);

    InferenceParams params;
    params.target_resolution = data->active_resolution;
    params.despill_strength = static_cast<float>(despill_strength);
    params.spill_method = spill_method;
    params.auto_despeckle = despeckle_enabled != 0;
    params.despeckle_size = despeckle_size;
    params.refiner_scale = 1.0f;
    params.input_is_linear = input_is_linear;
    params.upscale_method =
        upscale_method == kUpscaleBilinear ? UpscaleMethod::Bilinear : UpscaleMethod::Lanczos4;
    params.enable_tiling = enable_tiling != 0;
    params.tile_padding = tile_overlap;
    params.source_passthrough = source_passthrough_enabled != 0;
    params.sp_erode_px = edge_erode;
    params.sp_blur_px = edge_blur;

    const bool signature_matches =
        data->cached_signature_valid && data->cached_signature == signature;
    const bool cache_hit = data->cached_result_valid && signature_matches &&
                           data->cached_width == width && data->cached_height == height &&
                           data->cached_model_path == data->model_path &&
                           inference_params_equal(data->cached_params, params) &&
                           data->cached_screen_color == screen_color &&
                           std::abs(data->cached_alpha_black_point - alpha_black_point) < 1e-6 &&
                           std::abs(data->cached_alpha_white_point - alpha_white_point) < 1e-6 &&
                           std::abs(data->cached_alpha_erode - alpha_erode) < 1e-6 &&
                           std::abs(data->cached_alpha_softness - alpha_softness) < 1e-6 &&
                           std::abs(data->cached_alpha_gamma - alpha_gamma) < 1e-6 &&
                           std::abs(data->cached_temporal_smoothing - temporal_smoothing) < 1e-6;

    const SrgbLut& lut = SrgbLut::instance();
    Image alpha_view;
    Image fg_linear;

    if (cache_hit) {
        log_message("render", "event=cache_hit detail=output_switch");
        alpha_view = data->cached_result.alpha.view();
        fg_linear = data->cached_result.foreground.view();
    } else {
        const DeviceInfo requested_device = requested_device_for_render(data);
        const DeviceInfo effective_device_before = effective_device_for_render_log(data);
        const std::string render_phase = render_phase_label(data->render_count);
        log_render_event("render_begin", render_phase, requested_device, effective_device_before,
                         data->model_path, data->requested_resolution, data->active_resolution,
                         data->use_runtime_server ? data->runtime_client->backend_fallback()
                                                  : data->engine->backend_fallback());

        auto result = data->use_runtime_server
                          ? data->runtime_client->process_frame(
                                rgb_view, hint_view, params, data->render_count,
                                [&](const StageTiming& timing) {
                                    log_render_stage(render_phase, requested_device,
                                                     data->model_path, data->requested_resolution,
                                                     data->active_resolution, timing);
                                })
                          : data->engine->process_frame(
                                rgb_view, hint_view, params, [&](const StageTiming& timing) {
                                    log_render_stage(render_phase, requested_device,
                                                     data->model_path, data->requested_resolution,
                                                     data->active_resolution, timing);
                                });
        ++data->render_count;
        if (!result) {
            log_render_event("render_result", render_phase, requested_device,
                             effective_device_for_render_log(data), data->model_path,
                             data->requested_resolution, data->active_resolution,
                             data->use_runtime_server ? data->runtime_client->backend_fallback()
                                                      : data->engine->backend_fallback(),
                             result.error().message);
            log_message("render",
                        std::string("Engine processing failed: ") + result.error().message);
            set_runtime_error(data, result.error().message, instance);
            bypass_with_source(source_data, output_data, width, height, source_row_bytes,
                               output_row_bytes, source_depth);
            return kOfxStatOK;
        }

        DeviceInfo effective_device = data->use_runtime_server
                                          ? data->runtime_client->current_device()
                                          : data->engine->current_device();
        log_render_event("render_result", render_phase, requested_device, effective_device,
                         data->model_path, data->requested_resolution, data->active_resolution,
                         data->use_runtime_server ? data->runtime_client->backend_fallback()
                                                  : data->engine->backend_fallback());
        if (effective_device.backend != data->device.backend ||
            effective_device.name != data->device.name) {
            data->device = effective_device;
            update_runtime_panel(data);
        }
        if (requested_device.backend != Backend::Auto &&
            effective_device.backend != requested_device.backend) {
            std::string fallback_message =
                "Render switched away from the requested backend while using " +
                data->model_path.filename().string() + ".";
            if (auto fallback = data->use_runtime_server ? data->runtime_client->backend_fallback()
                                                         : data->engine->backend_fallback();
                fallback.has_value() && !fallback->reason.empty()) {
                fallback_message += " Reason: " + fallback->reason;
            }
            data->last_error = fallback_message;
            log_message("render", fallback_message);
            set_runtime_error(data, fallback_message, instance);
            return kOfxStatFailed;
        }

        Image alpha_view_local = result->alpha.view();
        if (alpha_view_local.width != width || alpha_view_local.height != height) {
            log_message("render", "Unexpected output size from engine.");
            set_runtime_error(data, "Unexpected output size from engine.", instance);
            return kOfxStatFailed;
        }

        if (alpha_erode != 0.0) {
            alpha_erode_dilate(alpha_view_local, static_cast<float>(alpha_erode),
                               data->alpha_edge_state);
        }
        if (alpha_softness > 0.0) {
            alpha_blur(alpha_view_local, static_cast<float>(alpha_softness),
                       data->alpha_edge_state);
        }
        if (alpha_black_point > 0.0 || alpha_white_point < 1.0) {
            alpha_levels(alpha_view_local, static_cast<float>(alpha_black_point),
                         static_cast<float>(alpha_white_point));
        }
        if (std::abs(alpha_gamma - 1.0) > 1e-6) {
            alpha_gamma_correct(alpha_view_local, static_cast<float>(alpha_gamma));
        }

        Image fg_srgb_view = result->foreground.view();
        ImageBuffer fg_linear_buf(width, height, 3);
        Image fg_linear_local = fg_linear_buf.view();
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                fg_linear_local(y, x, 0) = lut.to_linear(fg_srgb_view(y, x, 0));
                fg_linear_local(y, x, 1) = lut.to_linear(fg_srgb_view(y, x, 1));
                fg_linear_local(y, x, 2) = lut.to_linear(fg_srgb_view(y, x, 2));
            }
        }

        if (swap_screen) {
            swap_green_blue(fg_linear_local);
        }

        if (temporal_smoothing > 0.0) {
            const float blend = static_cast<float>(std::clamp(temporal_smoothing, 0.0, 1.0));
            const bool size_mismatch = !data->temporal_state_valid ||
                                       data->temporal_width != width ||
                                       data->temporal_height != height;
            if (size_mismatch) {
                data->temporal_alpha = ImageBuffer(width, height, 1);
                data->temporal_foreground = ImageBuffer(width, height, 3);
                data->temporal_width = width;
                data->temporal_height = height;
            }
            Image prev_alpha = data->temporal_alpha.view();
            Image prev_foreground = data->temporal_foreground.view();
            if (size_mismatch || !data->temporal_state_valid) {
                for (int y = 0; y < height; ++y) {
                    for (int x = 0; x < width; ++x) {
                        prev_alpha(y, x) = alpha_view_local(y, x);
                        for (int c = 0; c < 3; ++c) {
                            prev_foreground(y, x, c) = fg_linear_local(y, x, c);
                        }
                    }
                }
                data->temporal_state_valid = true;
            } else {
                for (int y = 0; y < height; ++y) {
                    for (int x = 0; x < width; ++x) {
                        float a_prev = prev_alpha(y, x);
                        float a_cur = alpha_view_local(y, x);
                        float a_out = a_cur * (1.0f - blend) + a_prev * blend;
                        alpha_view_local(y, x) = a_out;
                        prev_alpha(y, x) = a_out;
                        for (int c = 0; c < 3; ++c) {
                            float fg_prev = prev_foreground(y, x, c);
                            float fg_cur = fg_linear_local(y, x, c);
                            float fg_out = fg_cur * (1.0f - blend) + fg_prev * blend;
                            fg_linear_local(y, x, c) = fg_out;
                            prev_foreground(y, x, c) = fg_out;
                        }
                    }
                }
            }
            data->temporal_time = time;
        } else {
            data->temporal_state_valid = false;
        }

        data->cached_result.alpha = std::move(result->alpha);
        data->cached_result.foreground = std::move(fg_linear_buf);
        data->cached_result_valid = true;
        data->cached_time = time;
        data->cached_width = width;
        data->cached_height = height;
        data->cached_params = params;
        data->cached_model_path = data->model_path;
        data->cached_screen_color = screen_color;
        data->cached_alpha_black_point = alpha_black_point;
        data->cached_alpha_white_point = alpha_white_point;
        data->cached_alpha_erode = alpha_erode;
        data->cached_alpha_softness = alpha_softness;
        data->cached_alpha_gamma = alpha_gamma;
        data->cached_temporal_smoothing = temporal_smoothing;
        data->cached_signature = signature;
        data->cached_signature_valid = true;

        alpha_view = data->cached_result.alpha.view();
        fg_linear = data->cached_result.foreground.view();
    }

    if (swap_screen) {
        swap_green_blue(rgb_view);
    }

    bool apply_srgb = should_apply_srgb_to_output(output_mode, input_is_linear);

    if (output_mode == kOutputMatteOnly) {
        write_matte_output(alpha_view, output_data, output_row_bytes, output_depth, lut);
    } else if (output_mode == kOutputForegroundOnly) {
        write_foreground_output(fg_linear, output_data, output_row_bytes, output_depth, apply_srgb,
                                lut);
    } else if (output_mode == kOutputSourceMatte) {
        write_source_matte_output(rgb_view, alpha_view, output_data, output_row_bytes, output_depth,
                                  apply_srgb, lut);
    } else if (output_mode_uses_linear_premultiplied_rgba(output_mode)) {
        write_processed_output(fg_linear, alpha_view, output_data, output_row_bytes, output_depth,
                               false, lut);
    } else {
        write_processed_output(fg_linear, alpha_view, output_data, output_row_bytes, output_depth,
                               apply_srgb, lut);
    }

    const double render_ms =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - render_start)
            .count();
    record_frame_timing(data, render_ms);
    if (data != nullptr) {
        data->last_error.clear();
        data->runtime_panel_dirty = true;
    }
    return kOfxStatOK;
}

}  // namespace corridorkey::ofx
