#include <corridorkey/engine.hpp>

#include "ofx_image_utils.hpp"
#include "ofx_logging.hpp"
#include "ofx_shared.hpp"
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

    int despeckle_enabled = 0;
    int input_is_linear = 0;
    double despill_strength = 1.0;
    double refiner_scale = 1.0;

    if (data->despeckle_param) {
        g_suites.parameter->paramGetValueAtTime(data->despeckle_param, time, &despeckle_enabled);
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

    ColorUtils::generate_rough_matte(rgb_view, hint_view);

    InferenceParams params;
    params.despill_strength = static_cast<float>(despill_strength);
    params.auto_despeckle = despeckle_enabled != 0;
    params.refiner_scale = static_cast<float>(refiner_scale);
    params.input_is_linear = input_is_linear != 0;

    auto result = data->engine->process_frame(rgb_view, hint_view, params);
    if (!result) {
        log_message("render", std::string("Engine processing failed: ") + result.error().message);
        post_message(kOfxMessageError, result.error().message.c_str(), instance);
        return kOfxStatFailed;
    }

    Image output_view = result->processed.view();
    if (output_view.width != width || output_view.height != height) {
        log_message("render", "Unexpected output size from engine.");
        post_message(kOfxMessageError, "Unexpected output size from engine.", instance);
        return kOfxStatFailed;
    }

    write_output_image(output_view, output_data, output_row_bytes, output_depth,
                       input_is_linear == 0);
    return kOfxStatOK;
}

}  // namespace corridorkey::ofx
