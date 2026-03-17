#include <corridorkey/engine.hpp>

#include "common/srgb_lut.hpp"
#include "ofx_image_utils.hpp"
#include "ofx_logging.hpp"
#include "ofx_shared.hpp"
#include "post_process/alpha_edge.hpp"
#include "post_process/color_utils.hpp"

namespace corridorkey::ofx {

OfxStatus render(OfxImageEffectHandle instance, OfxPropertySetHandle in_args,
                 OfxPropertySetHandle /*out_args*/) {
    if (g_suites.image_effect == nullptr || g_suites.property == nullptr ||
        g_suites.parameter == nullptr) {
        log_message("render", "Missing required suites.");
        return kOfxStatErrMissingHostFeature;
    }

    InstanceData* data = get_instance_data(instance);
    if (data == nullptr || data->engine == nullptr) {
        log_message("render", "Engine is not ready.");
        post_message(kOfxMessageError, "CorridorKey engine is not ready.", instance);
        return kOfxStatFailed;
    }

    double time = 0.0;
    if (in_args != nullptr) {
        get_double(in_args, kOfxPropTime, time);
    }

    OfxPropertySetHandle source_props = nullptr;
    if (!fetch_image(data->source_clip, time, source_props)) {
        log_message("render", "Failed to fetch source image.");
        post_message(kOfxMessageError, "Failed to fetch source image.", instance);
        return kOfxStatFailed;
    }
    ImageHandleGuard source_guard{source_props};

    OfxPropertySetHandle output_props = nullptr;
    if (!fetch_image(data->output_clip, time, output_props)) {
        log_message("render", "Failed to fetch output image.");
        post_message(kOfxMessageError, "Failed to fetch output image.", instance);
        return kOfxStatFailed;
    }
    ImageHandleGuard output_guard{output_props};

    void* source_data = nullptr;
    if (g_suites.property->propGetPointer(source_props, kOfxImagePropData, 0, &source_data) !=
            kOfxStatOK ||
        source_data == nullptr) {
        log_message("render", "Source image data unavailable.");
        post_message(kOfxMessageError, "Source image data is unavailable.", instance);
        return kOfxStatFailed;
    }

    void* output_data = nullptr;
    if (g_suites.property->propGetPointer(output_props, kOfxImagePropData, 0, &output_data) !=
            kOfxStatOK ||
        output_data == nullptr) {
        log_message("render", "Output image data unavailable.");
        post_message(kOfxMessageError, "Output image data is unavailable.", instance);
        return kOfxStatFailed;
    }

    OfxRectI source_bounds{};
    if (!get_rect_i(source_props, kOfxImagePropBounds, source_bounds)) {
        log_message("render", "Source bounds unavailable.");
        post_message(kOfxMessageError, "Source bounds are unavailable.", instance);
        return kOfxStatFailed;
    }

    int source_row_bytes = 0;
    if (!get_int(source_props, kOfxImagePropRowBytes, source_row_bytes)) {
        log_message("render", "Source row bytes unavailable.");
        post_message(kOfxMessageError, "Source row bytes are unavailable.", instance);
        return kOfxStatFailed;
    }

    int output_row_bytes = 0;
    if (!get_int(output_props, kOfxImagePropRowBytes, output_row_bytes)) {
        log_message("render", "Output row bytes unavailable.");
        post_message(kOfxMessageError, "Output row bytes are unavailable.", instance);
        return kOfxStatFailed;
    }

    std::string source_depth;
    std::string source_components;
    if (!get_string(source_props, kOfxImageEffectPropPixelDepth, source_depth) ||
        !get_string(source_props, kOfxImageEffectPropComponents, source_components)) {
        log_message("render", "Source format unavailable.");
        post_message(kOfxMessageError, "Source format is unavailable.", instance);
        return kOfxStatFailed;
    }
    if (!is_depth(source_depth, kOfxBitDepthFloat) && !is_depth(source_depth, kOfxBitDepthByte)) {
        log_message("render", "Unsupported source bit depth.");
        post_message(kOfxMessageError, "Unsupported source bit depth.", instance);
        return kOfxStatFailed;
    }
    if (source_components != kOfxImageComponentRGBA) {
        log_message("render", "Unsupported source components.");
        post_message(kOfxMessageError, "Only RGBA source images are supported.", instance);
        return kOfxStatFailed;
    }

    std::string output_depth;
    if (!get_string(output_props, kOfxImageEffectPropPixelDepth, output_depth)) {
        output_depth = source_depth;
    }
    if (!is_depth(output_depth, kOfxBitDepthFloat) && !is_depth(output_depth, kOfxBitDepthByte)) {
        log_message("render", "Unsupported output bit depth.");
        post_message(kOfxMessageError, "Unsupported output bit depth.", instance);
        return kOfxStatFailed;
    }

    int width = source_bounds.x2 - source_bounds.x1;
    int height = source_bounds.y2 - source_bounds.y1;
    if (width <= 0 || height <= 0) {
        log_message("render", "Invalid source bounds.");
        post_message(kOfxMessageError, "Invalid source bounds.", instance);
        return kOfxStatFailed;
    }

    ImageBuffer rgb_buffer(width, height, 3);
    ImageBuffer hint_buffer(width, height, 1);
    Image rgb_view = rgb_buffer.view();
    Image hint_view = hint_buffer.view();

    copy_source_to_linear(rgb_view, source_data, source_row_bytes, source_depth);

    int quality_mode = kQualityAuto;
    int output_mode = kOutputProcessed;
    int despeckle_enabled = 0;
    int despeckle_size = 400;
    int input_is_linear = 0;
    double despill_strength = 1.0;
    double refiner_scale = 1.0;
    double alpha_black_point = 0.0;
    double alpha_white_point = 1.0;
    double alpha_erode = 0.0;
    double alpha_softness = 0.0;
    double brightness = 1.0;
    double saturation = 1.0;
    int upscale_method = kUpscaleLanczos4;
    int enable_tiling = 0;
    int tile_overlap = 32;

    if (data->quality_mode_param) {
        g_suites.parameter->paramGetValueAtTime(data->quality_mode_param, time, &quality_mode);
    }
    if (!ensure_engine_for_quality(data, quality_mode, width, height)) {
        log_message("render", "Failed to switch quality mode. Using current engine.");
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
    if (data->input_is_linear_param) {
        g_suites.parameter->paramGetValueAtTime(data->input_is_linear_param, time,
                                                &input_is_linear);
    }
    if (data->despill_param) {
        g_suites.parameter->paramGetValueAtTime(data->despill_param, time, &despill_strength);
    }
    if (data->refiner_param) {
        g_suites.parameter->paramGetValueAtTime(data->refiner_param, time, &refiner_scale);
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
    if (data->brightness_param) {
        g_suites.parameter->paramGetValueAtTime(data->brightness_param, time, &brightness);
    }
    if (data->saturation_param) {
        g_suites.parameter->paramGetValueAtTime(data->saturation_param, time, &saturation);
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

    // Use external alpha hint if connected, otherwise generate from green channel
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
                log_message("render", "Using external alpha hint from connected clip.");
            }
        }
    }

    if (!hint_from_clip) {
        ColorUtils::generate_rough_matte(rgb_view, hint_view);
    }

    // When input is linear, convert to sRGB for model inference.
    // The model was trained on sRGB ImageNet-normalized input.
    // Reference: OG CorridorKey inference_engine.py:170-174
    if (input_is_linear != 0) {
        const SrgbLut& lut = SrgbLut::instance();
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                rgb_view(y, x, 0) = lut.to_srgb(rgb_view(y, x, 0));
                rgb_view(y, x, 1) = lut.to_srgb(rgb_view(y, x, 1));
                rgb_view(y, x, 2) = lut.to_srgb(rgb_view(y, x, 2));
            }
        }
    }

    InferenceParams params;
    params.despill_strength = static_cast<float>(despill_strength);
    params.auto_despeckle = despeckle_enabled != 0;
    params.despeckle_size = despeckle_size;
    params.refiner_scale = static_cast<float>(refiner_scale);
    params.input_is_linear = input_is_linear != 0;
    params.upscale_method =
        upscale_method == kUpscaleBilinear ? UpscaleMethod::Bilinear : UpscaleMethod::Lanczos4;
    params.enable_tiling = enable_tiling != 0;
    params.tile_padding = tile_overlap;

    auto result = data->engine->process_frame(rgb_view, hint_view, params);
    if (!result) {
        log_message("render", std::string("Engine processing failed: ") + result.error().message);
        post_message(kOfxMessageError, result.error().message.c_str(), instance);
        return kOfxStatFailed;
    }

    // Apply alpha edge controls before final output assembly
    Image alpha_view = result->alpha.view();
    if (alpha_view.width != width || alpha_view.height != height) {
        log_message("render", "Unexpected output size from engine.");
        post_message(kOfxMessageError, "Unexpected output size from engine.", instance);
        return kOfxStatFailed;
    }

    if (alpha_erode != 0.0) {
        alpha_erode_dilate(alpha_view, static_cast<float>(alpha_erode));
    }
    if (alpha_softness > 0.0) {
        alpha_blur(alpha_view, static_cast<float>(alpha_softness));
    }
    if (alpha_black_point > 0.0 || alpha_white_point < 1.0) {
        alpha_levels(alpha_view, static_cast<float>(alpha_black_point),
                     static_cast<float>(alpha_white_point));
    }

    // Convert foreground from sRGB to linear for all output assembly.
    // The model outputs sRGB foreground; all premultiplication and color
    // correction must happen in linear space to avoid double-gamma artifacts.
    const SrgbLut& lut = SrgbLut::instance();
    Image fg_srgb_view = result->foreground.view();
    ImageBuffer fg_linear_buf(width, height, 3);
    Image fg_linear = fg_linear_buf.view();
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            fg_linear(y, x, 0) = lut.to_linear(fg_srgb_view(y, x, 0));
            fg_linear(y, x, 1) = lut.to_linear(fg_srgb_view(y, x, 1));
            fg_linear(y, x, 2) = lut.to_linear(fg_srgb_view(y, x, 2));
        }
    }

    // Apply color correction in linear space
    if (brightness != 1.0 || saturation != 1.0) {
        const float bright_f = static_cast<float>(brightness);
        const float sat_f = static_cast<float>(saturation);
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                float r = fg_linear(y, x, 0) * bright_f;
                float g = fg_linear(y, x, 1) * bright_f;
                float b = fg_linear(y, x, 2) * bright_f;
                if (sat_f != 1.0F) {
                    float luma = 0.2126F * r + 0.7152F * g + 0.0722F * b;
                    r = luma + sat_f * (r - luma);
                    g = luma + sat_f * (g - luma);
                    b = luma + sat_f * (b - luma);
                }
                fg_linear(y, x, 0) = r;
                fg_linear(y, x, 1) = g;
                fg_linear(y, x, 2) = b;
            }
        }
    }

    // Build output based on selected mode
    bool apply_srgb = input_is_linear == 0;

    if (output_mode == kOutputMatteOnly) {
        ImageBuffer matte_rgba(width, height, 4);
        Image matte_view = matte_rgba.view();
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                float a = alpha_view(y, x);
                matte_view(y, x, 0) = a;
                matte_view(y, x, 1) = a;
                matte_view(y, x, 2) = a;
                matte_view(y, x, 3) = 1.0f;
            }
        }
        write_output_image(matte_view, output_data, output_row_bytes, output_depth, false);
    } else if (output_mode == kOutputForegroundOnly) {
        // Foreground with alpha = 1, premultiplied linear
        ImageBuffer fg_rgba(width, height, 4);
        Image out_view = fg_rgba.view();
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                out_view(y, x, 0) = fg_linear(y, x, 0);
                out_view(y, x, 1) = fg_linear(y, x, 1);
                out_view(y, x, 2) = fg_linear(y, x, 2);
                out_view(y, x, 3) = 1.0f;
            }
        }
        write_output_image(out_view, output_data, output_row_bytes, output_depth, apply_srgb);
    } else if (output_mode == kOutputSourceMatte) {
        // Source RGB (sRGB) converted to linear, premultiplied with alpha
        ImageBuffer src_rgba(width, height, 4);
        Image out_view = src_rgba.view();
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                float a = alpha_view(y, x);
                out_view(y, x, 0) = lut.to_linear(rgb_view(y, x, 0)) * a;
                out_view(y, x, 1) = lut.to_linear(rgb_view(y, x, 1)) * a;
                out_view(y, x, 2) = lut.to_linear(rgb_view(y, x, 2)) * a;
                out_view(y, x, 3) = a;
            }
        }
        write_output_image(out_view, output_data, output_row_bytes, output_depth, apply_srgb);
    } else {
        // Default: Processed output (premultiplied linear RGBA)
        ImageBuffer reprocessed(width, height, 4);
        Image out_view = reprocessed.view();
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                float a = alpha_view(y, x);
                out_view(y, x, 0) = fg_linear(y, x, 0) * a;
                out_view(y, x, 1) = fg_linear(y, x, 1) * a;
                out_view(y, x, 2) = fg_linear(y, x, 2) * a;
                out_view(y, x, 3) = a;
            }
        }
        write_output_image(out_view, output_data, output_row_bytes, output_depth, apply_srgb);
    }
    return kOfxStatOK;
}

}  // namespace corridorkey::ofx
