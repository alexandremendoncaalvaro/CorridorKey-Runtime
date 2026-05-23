#include <algorithm>
#include <cstring>
#include <limits>
#include <optional>
#include <string>

#include "adobe_bridge.hpp"

namespace corridorkey::adobe {
namespace {

constexpr float kAdobeArgb8White = 255.0F;
constexpr float kAdobeArgb16White = 32768.0F;
constexpr int kMaxAdobeFrameLongEdge = 8192;
constexpr std::size_t kMaxAdobeFramePixels = 8192ULL * 4320ULL;
constexpr int kOutputMatteOnly = 1;
constexpr int kOutputForegroundOnly = 2;
constexpr int kOutputSourceMatte = 3;
constexpr int kRgbChannelCount = 3;
constexpr int kAlphaChannelCount = 1;
constexpr float kOpaqueAlpha = 1.0F;
constexpr float kQuantizeBias = 0.5F;

struct Argb8Pixel {
    std::uint8_t alpha = 0;
    std::uint8_t red = 0;
    std::uint8_t green = 0;
    std::uint8_t blue = 0;
};

struct Bgra8Pixel {
    std::uint8_t blue = 0;
    std::uint8_t green = 0;
    std::uint8_t red = 0;
    std::uint8_t alpha = 0;
};

struct Argb16Pixel {
    std::uint16_t alpha = 0;
    std::uint16_t red = 0;
    std::uint16_t green = 0;
    std::uint16_t blue = 0;
};

struct ArgbFloatPixel {
    float alpha = 0.0F;
    float red = 0.0F;
    float green = 0.0F;
    float blue = 0.0F;
};

static_assert(sizeof(Argb8Pixel) == 4);
static_assert(sizeof(Bgra8Pixel) == 4);
static_assert(sizeof(Argb16Pixel) == 8);
static_assert(sizeof(ArgbFloatPixel) == 16);

Error invalid_adobe_output_error(const std::string& message) {
    return Error{ErrorCode::InvalidParameters, message};
}

std::optional<std::size_t> pixel_size_for(AdobePixelFormat format) {
    switch (format) {
        case AdobePixelFormat::Argb32:
            return sizeof(Argb8Pixel);
        case AdobePixelFormat::Argb64:
            return sizeof(Argb16Pixel);
        case AdobePixelFormat::Argb128:
            return sizeof(ArgbFloatPixel);
        case AdobePixelFormat::Bgra32:
            return sizeof(Bgra8Pixel);
    }
    return std::nullopt;
}

Result<void> validate_mutable_frame_view(const AdobeMutableFrameView& frame,
                                         std::size_t pixel_size) {
    if (frame.data == nullptr) {
        return Unexpected<Error>(invalid_adobe_output_error("Adobe output data is null."));
    }
    if (frame.width <= 0 || frame.height <= 0) {
        return Unexpected<Error>(
            invalid_adobe_output_error("Adobe output dimensions must be positive."));
    }
    if (frame.width > kMaxAdobeFrameLongEdge || frame.height > kMaxAdobeFrameLongEdge) {
        return Unexpected<Error>(
            invalid_adobe_output_error("Adobe output dimensions exceed the supported maximum."));
    }
    if (frame.row_bytes <= 0) {
        return Unexpected<Error>(
            invalid_adobe_output_error("Adobe output row bytes must be positive."));
    }

    const auto width = static_cast<std::size_t>(frame.width);
    const auto height = static_cast<std::size_t>(frame.height);
    if (height > 0 && width > (kMaxAdobeFramePixels / height)) {
        return Unexpected<Error>(invalid_adobe_output_error(
            "Adobe output dimensions exceed the supported pixel count."));
    }
    if (width > (std::numeric_limits<std::size_t>::max() / pixel_size)) {
        return Unexpected<Error>(invalid_adobe_output_error("Adobe output width overflows row."));
    }

    const std::size_t required_row_bytes = width * pixel_size;
    const auto row_bytes = static_cast<std::size_t>(frame.row_bytes);
    if (row_bytes < required_row_bytes) {
        return Unexpected<Error>(
            invalid_adobe_output_error("Adobe output row bytes are smaller than one pixel row."));
    }

    if (frame.data_size_bytes == 0) {
        return {};
    }
    if (height - 1 > (std::numeric_limits<std::size_t>::max() / row_bytes)) {
        return Unexpected<Error>(invalid_adobe_output_error("Adobe output height overflows."));
    }
    const std::size_t last_row_offset = (height - 1) * row_bytes;
    if (last_row_offset > std::numeric_limits<std::size_t>::max() - required_row_bytes) {
        return Unexpected<Error>(invalid_adobe_output_error("Adobe output buffer size overflows."));
    }
    if (frame.data_size_bytes < last_row_offset + required_row_bytes) {
        return Unexpected<Error>(
            invalid_adobe_output_error("Adobe output buffer is smaller than the requested view."));
    }
    return {};
}

Result<void> validate_image_view(const Image& image, int width, int height, int channels,
                                 const char* label) {
    if (image.empty()) {
        return Unexpected<Error>(invalid_adobe_output_error(std::string(label) + " is empty."));
    }
    if (image.width != width || image.height != height || image.channels != channels) {
        return Unexpected<Error>(
            invalid_adobe_output_error(std::string(label) + " dimensions do not match output."));
    }
    return {};
}

bool output_mode_requires_foreground(int output_mode) {
    return output_mode != kOutputMatteOnly && output_mode != kOutputSourceMatte;
}

float clamp_unit(float value) {
    return std::clamp(value, 0.0F, 1.0F);
}

std::uint8_t quantize_argb8(float value) {
    return static_cast<std::uint8_t>(
        std::clamp((clamp_unit(value) * kAdobeArgb8White) + kQuantizeBias, 0.0F, kAdobeArgb8White));
}

std::uint16_t quantize_argb16(float value) {
    return static_cast<std::uint16_t>(std::clamp(
        (clamp_unit(value) * kAdobeArgb16White) + kQuantizeBias, 0.0F, kAdobeArgb16White));
}

void store_output_pixel(AdobePixelFormat format, std::byte* destination, float red, float green,
                        float blue, float alpha) {
    switch (format) {
        case AdobePixelFormat::Argb32: {
            const Argb8Pixel pixel{quantize_argb8(alpha), quantize_argb8(red),
                                   quantize_argb8(green), quantize_argb8(blue)};
            std::memcpy(destination, &pixel, sizeof(pixel));
            break;
        }
        case AdobePixelFormat::Argb64: {
            const Argb16Pixel pixel{quantize_argb16(alpha), quantize_argb16(red),
                                    quantize_argb16(green), quantize_argb16(blue)};
            std::memcpy(destination, &pixel, sizeof(pixel));
            break;
        }
        case AdobePixelFormat::Argb128: {
            const ArgbFloatPixel pixel{alpha, red, green, blue};
            std::memcpy(destination, &pixel, sizeof(pixel));
            break;
        }
        case AdobePixelFormat::Bgra32: {
            const Bgra8Pixel pixel{quantize_argb8(blue), quantize_argb8(green), quantize_argb8(red),
                                   quantize_argb8(alpha)};
            std::memcpy(destination, &pixel, sizeof(pixel));
            break;
        }
    }
}

void write_selected_output_pixel(const Image& alpha, const Image& foreground, const Image& source,
                                 int output_mode, int y_pos, int x_pos, AdobePixelFormat format,
                                 std::byte* destination) {
    const float alpha_value = alpha(y_pos, x_pos);
    if (output_mode == kOutputMatteOnly) {
        store_output_pixel(format, destination, alpha_value, alpha_value, alpha_value,
                           kOpaqueAlpha);
        return;
    }
    if (output_mode == kOutputForegroundOnly) {
        store_output_pixel(format, destination, foreground(y_pos, x_pos, 0),
                           foreground(y_pos, x_pos, 1), foreground(y_pos, x_pos, 2), kOpaqueAlpha);
        return;
    }
    if (output_mode == kOutputSourceMatte) {
        store_output_pixel(format, destination, source(y_pos, x_pos, 0) * alpha_value,
                           source(y_pos, x_pos, 1) * alpha_value,
                           source(y_pos, x_pos, 2) * alpha_value, alpha_value);
        return;
    }

    store_output_pixel(format, destination, foreground(y_pos, x_pos, 0) * alpha_value,
                       foreground(y_pos, x_pos, 1) * alpha_value,
                       foreground(y_pos, x_pos, 2) * alpha_value, alpha_value);
}

}  // namespace

Result<void> copy_runtime_result_to_adobe_frame(const FrameResult& result,
                                                const AdobeMutableFrameView& output_frame,
                                                int output_mode,
                                                const AdobeRuntimeFrame* source_frame) {
    const auto pixel_size = pixel_size_for(output_frame.pixel_format);
    if (!pixel_size.has_value()) {
        return Unexpected<Error>(invalid_adobe_output_error("Unsupported Adobe output format."));
    }
    auto validation = validate_mutable_frame_view(output_frame, *pixel_size);
    if (!validation) {
        return Unexpected<Error>(validation.error());
    }

    const Image alpha = result.alpha.const_view();
    validation = validate_image_view(alpha, output_frame.width, output_frame.height,
                                     kAlphaChannelCount, "Adobe output alpha");
    if (!validation) {
        return Unexpected<Error>(validation.error());
    }

    Image foreground;
    if (output_mode_requires_foreground(output_mode) || output_mode == kOutputForegroundOnly) {
        foreground = result.foreground.const_view();
        validation = validate_image_view(foreground, output_frame.width, output_frame.height,
                                         kRgbChannelCount, "Adobe output foreground");
        if (!validation) {
            return Unexpected<Error>(validation.error());
        }
    }

    Image source;
    if (output_mode == kOutputSourceMatte) {
        if (source_frame == nullptr) {
            return Unexpected<Error>(
                invalid_adobe_output_error("Adobe source frame is required for Source+Matte."));
        }
        source = source_frame->rgb.const_view();
        validation = validate_image_view(source, output_frame.width, output_frame.height,
                                         kRgbChannelCount, "Adobe output source");
        if (!validation) {
            return Unexpected<Error>(validation.error());
        }
    }

    auto* base = static_cast<std::byte*>(output_frame.data);
    const auto row_bytes = static_cast<std::size_t>(output_frame.row_bytes);
    for (int y_pos = 0; y_pos < output_frame.height; ++y_pos) {
        auto* row = base + (row_bytes * y_pos);
        for (int x_pos = 0; x_pos < output_frame.width; ++x_pos) {
            write_selected_output_pixel(alpha, foreground, source, output_mode, y_pos, x_pos,
                                        output_frame.pixel_format,
                                        row + (*pixel_size * static_cast<std::size_t>(x_pos)));
        }
    }

    return {};
}

}  // namespace corridorkey::adobe
