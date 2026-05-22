#include "adobe_bridge.hpp"

#include <cstring>
#include <limits>
#include <optional>
#include <string>
#include <utility>

namespace corridorkey::adobe {
namespace {

constexpr float kAdobeArgb8White = 255.0F;
constexpr float kAdobeArgb16White = 32768.0F;

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

Error invalid_adobe_frame_error(const std::string& message) {
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

Result<void> validate_frame_view(const AdobeFrameView& frame, std::size_t pixel_size) {
    if (frame.data == nullptr) {
        return Unexpected<Error>(invalid_adobe_frame_error("Adobe frame data is null."));
    }
    if (frame.width <= 0 || frame.height <= 0) {
        return Unexpected<Error>(
            invalid_adobe_frame_error("Adobe frame dimensions must be positive."));
    }
    if (frame.row_bytes <= 0) {
        return Unexpected<Error>(
            invalid_adobe_frame_error("Adobe frame row bytes must be positive."));
    }

    const auto width = static_cast<std::size_t>(frame.width);
    const auto height = static_cast<std::size_t>(frame.height);
    if (width > (std::numeric_limits<std::size_t>::max() / pixel_size)) {
        return Unexpected<Error>(
            invalid_adobe_frame_error("Adobe frame width overflows row size."));
    }

    const std::size_t required_row_bytes = width * pixel_size;
    const auto row_bytes = static_cast<std::size_t>(frame.row_bytes);
    if (row_bytes < required_row_bytes) {
        return Unexpected<Error>(
            invalid_adobe_frame_error("Adobe frame row bytes are smaller than one pixel row."));
    }

    if (frame.data_size_bytes == 0) {
        return {};
    }
    if (height - 1 > (std::numeric_limits<std::size_t>::max() / row_bytes)) {
        return Unexpected<Error>(
            invalid_adobe_frame_error("Adobe frame height overflows buffer size."));
    }
    const std::size_t last_row_offset = (height - 1) * row_bytes;
    if (last_row_offset > std::numeric_limits<std::size_t>::max() - required_row_bytes) {
        return Unexpected<Error>(
            invalid_adobe_frame_error("Adobe frame buffer size overflows."));
    }
    if (frame.data_size_bytes < last_row_offset + required_row_bytes) {
        return Unexpected<Error>(
            invalid_adobe_frame_error("Adobe frame buffer is smaller than the requested view."));
    }
    return {};
}

Result<AdobeRuntimeFrame> make_runtime_frame(int width, int height) {
    AdobeRuntimeFrame frame;
    frame.rgb = ImageBuffer(width, height, 3);
    frame.alpha_hint = ImageBuffer(width, height, 1);
    const auto rgb = frame.rgb.view();
    const auto alpha_hint = frame.alpha_hint.view();
    const auto expected_rgb_size = static_cast<std::size_t>(width) *
                                   static_cast<std::size_t>(height) * 3U;
    const auto expected_alpha_size =
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    if (rgb.data.size() != expected_rgb_size || alpha_hint.data.size() != expected_alpha_size) {
        return Unexpected<Error>(
            Error{ErrorCode::IoError, "Failed to allocate Adobe runtime frame buffers."});
    }
    return frame;
}

float normalize_argb8(std::uint8_t value) {
    return static_cast<float>(value) / kAdobeArgb8White;
}

float normalize_argb16(std::uint16_t value) {
    return static_cast<float>(value) / kAdobeArgb16White;
}

float normalize_argb_float(float value) {
    return value;
}

template <typename Pixel, typename Normalizer>
void copy_pixel_rows(const AdobeFrameView& frame, AdobeRuntimeFrame& output,
                     Normalizer normalize) {
    const auto* base = static_cast<const std::byte*>(frame.data);
    auto rgb = output.rgb.view();
    auto alpha_hint = output.alpha_hint.view();
    const auto row_bytes = static_cast<std::size_t>(frame.row_bytes);
    for (int y_pos = 0; y_pos < frame.height; ++y_pos) {
        const auto* row = base + (row_bytes * y_pos);
        for (int x_pos = 0; x_pos < frame.width; ++x_pos) {
            Pixel pixel;
            std::memcpy(&pixel, row + (sizeof(Pixel) * x_pos), sizeof(Pixel));
            rgb(y_pos, x_pos, 0) = normalize(pixel.red);
            rgb(y_pos, x_pos, 1) = normalize(pixel.green);
            rgb(y_pos, x_pos, 2) = normalize(pixel.blue);
            alpha_hint(y_pos, x_pos) = normalize(pixel.alpha);
        }
    }
}

}  // namespace

Result<AdobeRuntimeFrame> copy_adobe_frame_to_runtime(const AdobeFrameView& frame) {
    const auto pixel_size = pixel_size_for(frame.pixel_format);
    if (!pixel_size.has_value()) {
        return Unexpected<Error>(invalid_adobe_frame_error("Unsupported Adobe pixel format."));
    }

    auto validation = validate_frame_view(frame, *pixel_size);
    if (!validation) {
        return Unexpected<Error>(validation.error());
    }

    auto output = make_runtime_frame(frame.width, frame.height);
    if (!output) {
        return Unexpected<Error>(output.error());
    }

    switch (frame.pixel_format) {
        case AdobePixelFormat::Argb32:
            copy_pixel_rows<Argb8Pixel>(frame, *output, normalize_argb8);
            break;
        case AdobePixelFormat::Argb64:
            copy_pixel_rows<Argb16Pixel>(frame, *output, normalize_argb16);
            break;
        case AdobePixelFormat::Argb128:
            copy_pixel_rows<ArgbFloatPixel>(frame, *output, normalize_argb_float);
            break;
        case AdobePixelFormat::Bgra32:
            copy_pixel_rows<Bgra8Pixel>(frame, *output, normalize_argb8);
            break;
    }
    return std::move(*output);
}

}  // namespace corridorkey::adobe
