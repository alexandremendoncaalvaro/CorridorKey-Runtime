#include "adobe_bridge.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <mutex>
#include <optional>
#include <string>
#include <utility>

#include "common/parallel_for.hpp"
#include "common/srgb_lut.hpp"
#include "post_process/alpha_edge.hpp"
#include "post_process/color_utils.hpp"

namespace corridorkey::adobe {
namespace {

constexpr float kAdobeArgb8White = 255.0F;
constexpr float kAdobeArgb16White = 32768.0F;
constexpr float kOpaqueAlphaHintThreshold = 0.999F;
constexpr float kMeaningfulAlphaHintThreshold = 0.98F;
constexpr float kMeaningfulAlphaHintRange = 0.05F;
constexpr int kMaxAdobeFrameLongEdge = 8192;
constexpr std::size_t kMaxAdobeFramePixels = 8192ULL * 4320ULL;

enum class AdobeFrameChannel : std::uint8_t {
    Alpha,
    Red,
};

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

struct ChannelBounds {
    bool has_samples = false;
    float minimum = 0.0F;
    float maximum = 0.0F;
};

struct ChannelBuffer {
    ImageBuffer buffer;
    ChannelBounds bounds;
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
    if (frame.width > kMaxAdobeFrameLongEdge || frame.height > kMaxAdobeFrameLongEdge) {
        return Unexpected<Error>(
            invalid_adobe_frame_error("Adobe frame dimensions exceed the supported maximum."));
    }
    if (frame.row_bytes <= 0) {
        return Unexpected<Error>(
            invalid_adobe_frame_error("Adobe frame row bytes must be positive."));
    }

    const auto width = static_cast<std::size_t>(frame.width);
    const auto height = static_cast<std::size_t>(frame.height);
    if (height > 0 && width > (kMaxAdobeFramePixels / height)) {
        return Unexpected<Error>(
            invalid_adobe_frame_error("Adobe frame dimensions exceed the supported pixel count."));
    }
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
        return Unexpected<Error>(invalid_adobe_frame_error("Adobe frame buffer size overflows."));
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
    const auto expected_rgb_size =
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 3U;
    const auto expected_alpha_size =
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    if (rgb.data.size() != expected_rgb_size || alpha_hint.data.size() != expected_alpha_size) {
        return Unexpected<Error>(
            Error{ErrorCode::IoError, "Failed to allocate Adobe runtime frame buffers."});
    }
    return frame;
}

Result<void> validate_alpha_hint_frame_view_shape(const AdobeRuntimeFrame& frame,
                                                  const AdobeFrameView& alpha_hint_frame) {
    const auto rgb = frame.rgb.const_view();
    if (alpha_hint_frame.width != rgb.width || alpha_hint_frame.height != rgb.height) {
        return Unexpected<Error>(
            invalid_adobe_frame_error("Alpha Hint Layer dimensions must match the source frame."));
    }
    return {};
}

bool alpha_view_contains_hint(const Image& alpha_hint) noexcept {
    if (alpha_hint.width <= 0 || alpha_hint.height <= 0 || alpha_hint.channels != 1) {
        return false;
    }

    const auto bounds = std::minmax_element(alpha_hint.data.begin(), alpha_hint.data.end());
    if (bounds.first == alpha_hint.data.end() || bounds.second == alpha_hint.data.end()) {
        return false;
    }

    const float minimum_alpha = *bounds.first;
    const float maximum_alpha = *bounds.second;
    if (minimum_alpha >= kOpaqueAlphaHintThreshold) {
        return false;
    }

    return minimum_alpha <= kMeaningfulAlphaHintThreshold &&
           (maximum_alpha - minimum_alpha) >= kMeaningfulAlphaHintRange;
}

bool alpha_bounds_contains_hint(const ChannelBounds& bounds) noexcept {
    if (!bounds.has_samples || bounds.minimum >= kOpaqueAlphaHintThreshold) {
        return false;
    }
    return bounds.minimum <= kMeaningfulAlphaHintThreshold &&
           (bounds.maximum - bounds.minimum) >= kMeaningfulAlphaHintRange;
}

bool source_alpha_contains_hint(const AdobeRuntimeFrame& frame) noexcept {
    return alpha_view_contains_hint(frame.alpha_hint.const_view());
}

void linear_to_srgb_in_place(Image image) {
    if (image.empty()) {
        return;
    }

    const auto& lut = SrgbLut::instance();
    const auto row_value_count =
        static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.channels);
    common::parallel_for_rows(image.height, [&](int y_begin, int y_end) {
        const auto begin_index = static_cast<std::size_t>(y_begin) * row_value_count;
        const auto end_index = static_cast<std::size_t>(y_end) * row_value_count;
        for (std::size_t index = begin_index; index < end_index; ++index) {
            image.data[index] = lut.to_srgb(image.data[index]);
        }
    });
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
void copy_pixel_rows(const AdobeFrameView& frame, AdobeRuntimeFrame& output, Normalizer normalize) {
    const auto* base = static_cast<const std::byte*>(frame.data);
    auto rgb = output.rgb.view();
    auto alpha_hint = output.alpha_hint.view();
    const auto row_bytes = static_cast<std::size_t>(frame.row_bytes);
    common::parallel_for_rows(frame.height, [&](int y_begin, int y_end) {
        for (int y_pos = y_begin; y_pos < y_end; ++y_pos) {
            const auto* row = base + (row_bytes * static_cast<std::size_t>(y_pos));
            for (int x_pos = 0; x_pos < frame.width; ++x_pos) {
                Pixel pixel;
                std::memcpy(&pixel, row + (sizeof(Pixel) * static_cast<std::size_t>(x_pos)),
                            sizeof(Pixel));
                rgb(y_pos, x_pos, 0) = normalize(pixel.red);
                rgb(y_pos, x_pos, 1) = normalize(pixel.green);
                rgb(y_pos, x_pos, 2) = normalize(pixel.blue);
                alpha_hint(y_pos, x_pos) = normalize(pixel.alpha);
            }
        }
    });
}

template <typename Pixel, typename Normalizer>
ChannelBounds copy_channel_rows_with_bounds(const AdobeFrameView& frame, Image output,
                                            AdobeFrameChannel channel, Normalizer normalize,
                                            bool convert_linear_to_srgb) {
    const auto* base = static_cast<const std::byte*>(frame.data);
    const auto row_bytes = static_cast<std::size_t>(frame.row_bytes);
    const auto* srgb_lut = convert_linear_to_srgb ? &SrgbLut::instance() : nullptr;
    std::mutex bounds_mutex;
    ChannelBounds bounds;
    common::parallel_for_rows(frame.height, [&](int y_begin, int y_end) {
        ChannelBounds local_bounds;
        for (int y_pos = y_begin; y_pos < y_end; ++y_pos) {
            const auto* row = base + (row_bytes * static_cast<std::size_t>(y_pos));
            for (int x_pos = 0; x_pos < frame.width; ++x_pos) {
                Pixel pixel;
                std::memcpy(&pixel, row + (sizeof(Pixel) * static_cast<std::size_t>(x_pos)),
                            sizeof(Pixel));
                float value =
                    normalize(channel == AdobeFrameChannel::Alpha ? pixel.alpha : pixel.red);
                if (srgb_lut != nullptr) {
                    value = srgb_lut->to_srgb(value);
                }
                output(y_pos, x_pos) = value;
                if (!local_bounds.has_samples) {
                    local_bounds.has_samples = true;
                    local_bounds.minimum = value;
                    local_bounds.maximum = value;
                    continue;
                }
                local_bounds.minimum = std::min(local_bounds.minimum, value);
                local_bounds.maximum = std::max(local_bounds.maximum, value);
            }
        }
        if (!local_bounds.has_samples) {
            return;
        }

        std::lock_guard lock(bounds_mutex);
        if (!bounds.has_samples) {
            bounds = local_bounds;
            return;
        }
        bounds.minimum = std::min(bounds.minimum, local_bounds.minimum);
        bounds.maximum = std::max(bounds.maximum, local_bounds.maximum);
    });
    return bounds;
}

Result<ChannelBuffer> copy_adobe_channel_to_buffer_with_bounds(const AdobeFrameView& frame,
                                                               AdobeFrameChannel channel,
                                                               bool convert_linear_to_srgb) {
    const auto pixel_size = pixel_size_for(frame.pixel_format);
    if (!pixel_size.has_value()) {
        return Unexpected<Error>(invalid_adobe_frame_error("Unsupported Adobe pixel format."));
    }

    auto validation = validate_frame_view(frame, *pixel_size);
    if (!validation) {
        return Unexpected<Error>(validation.error());
    }

    ImageBuffer output(frame.width, frame.height, 1);
    auto output_view = output.view();
    ChannelBounds bounds;
    switch (frame.pixel_format) {
        case AdobePixelFormat::Argb32:
            bounds = copy_channel_rows_with_bounds<Argb8Pixel>(
                frame, output_view, channel, normalize_argb8, convert_linear_to_srgb);
            break;
        case AdobePixelFormat::Argb64:
            bounds = copy_channel_rows_with_bounds<Argb16Pixel>(
                frame, output_view, channel, normalize_argb16, convert_linear_to_srgb);
            break;
        case AdobePixelFormat::Argb128:
            bounds = copy_channel_rows_with_bounds<ArgbFloatPixel>(
                frame, output_view, channel, normalize_argb_float, convert_linear_to_srgb);
            break;
        case AdobePixelFormat::Bgra32:
            bounds = copy_channel_rows_with_bounds<Bgra8Pixel>(
                frame, output_view, channel, normalize_argb8, convert_linear_to_srgb);
            break;
    }

    return ChannelBuffer{std::move(output), bounds};
}

Result<std::optional<AdobeAlphaHintSource>> copy_external_alpha_hint(
    AdobeRuntimeFrame& frame, const AdobeFrameView& external_alpha_hint_frame,
    bool input_is_linear) {
    auto shape_validation = validate_alpha_hint_frame_view_shape(frame, external_alpha_hint_frame);
    if (!shape_validation) {
        return Unexpected<Error>(shape_validation.error());
    }

    auto source_alpha = copy_adobe_channel_to_buffer_with_bounds(external_alpha_hint_frame,
                                                                 AdobeFrameChannel::Alpha, false);
    if (!source_alpha) {
        return Unexpected<Error>(source_alpha.error());
    }
    if (alpha_bounds_contains_hint(source_alpha->bounds)) {
        frame.alpha_hint = std::move(source_alpha->buffer);
        return std::optional<AdobeAlphaHintSource>{AdobeAlphaHintSource::ExternalLayerAlpha};
    }

    auto source_red = copy_adobe_channel_to_buffer_with_bounds(
        external_alpha_hint_frame, AdobeFrameChannel::Red, input_is_linear);
    if (!source_red) {
        return Unexpected<Error>(source_red.error());
    }

    if (alpha_bounds_contains_hint(source_red->bounds)) {
        frame.alpha_hint = std::move(source_red->buffer);
        return std::optional<AdobeAlphaHintSource>{AdobeAlphaHintSource::ExternalLayerRed};
    }

    return Result<std::optional<AdobeAlphaHintSource>>{std::optional<AdobeAlphaHintSource>{}};
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

void apply_adobe_input_color_space(AdobeRuntimeFrame& frame, bool input_is_linear) {
    if (!input_is_linear) {
        return;
    }
    linear_to_srgb_in_place(frame.rgb.view());
}

void apply_adobe_matte_params(FrameResult& result, const AdobeMatteParams& params, int width,
                              int height, AlphaEdgeState& state) {
    Image alpha = result.alpha.view();
    if (alpha.empty()) {
        return;
    }

    constexpr double kBaselineLongEdge = 1920.0;
    const int long_edge = std::max(width, height);
    const double scale = long_edge > 0 ? static_cast<double>(long_edge) / kBaselineLongEdge : 1.0;
    const double scaled_shrink_grow = params.shrink_grow_pixels * scale;
    const double scaled_edge_blur = params.edge_blur_pixels * scale;

    if (scaled_shrink_grow != 0.0) {
        alpha_erode_dilate(alpha, static_cast<float>(scaled_shrink_grow), state);
    }
    if (scaled_edge_blur > 0.0) {
        alpha_blur(alpha, static_cast<float>(scaled_edge_blur), state);
    }
    if (params.black_point > 0.0 || params.white_point < 1.0) {
        alpha_levels(alpha, static_cast<float>(params.black_point),
                     static_cast<float>(params.white_point));
    }
    if (std::abs(params.gamma - 1.0) > 1e-6) {
        alpha_gamma_correct(alpha, static_cast<float>(params.gamma));
    }
}

ScreenColorTransform canonicalize_adobe_runtime_frame_for_screen_color(AdobeRuntimeFrame& frame,
                                                                       ScreenColorMode mode) {
    auto rgb = frame.rgb.view();
    ScreenColorTransform transform = make_screen_color_transform(rgb, mode);
    canonicalize_to_green_domain(rgb, transform);
    return transform;
}

Result<AdobeAlphaHintSource> resolve_alpha_hint_source(
    AdobeRuntimeFrame& frame, const AdobeFrameView* external_alpha_hint_frame,
    AlphaHintPolicy alpha_hint_policy, bool input_is_linear) {
    if (external_alpha_hint_frame != nullptr) {
        auto copy_status =
            copy_external_alpha_hint(frame, *external_alpha_hint_frame, input_is_linear);
        if (!copy_status) {
            return Unexpected<Error>(copy_status.error());
        }
        if (copy_status->has_value()) {
            return **copy_status;
        }
    }

    if (source_alpha_contains_hint(frame)) {
        return AdobeAlphaHintSource::SourceAlpha;
    }

    if (alpha_hint_policy == AlphaHintPolicy::RequireExternalHint) {
        return Unexpected<Error>(
            Error{ErrorCode::InvalidParameters, "Waiting for Alpha Hint Layer."});
    }

    ColorUtils::generate_rough_matte(frame.rgb.view(), frame.alpha_hint.view());
    return AdobeAlphaHintSource::RoughFallback;
}

std::string_view adobe_alpha_hint_source_label(AdobeAlphaHintSource source) noexcept {
    switch (source) {
        case AdobeAlphaHintSource::SourceAlpha:
            return "source_alpha";
        case AdobeAlphaHintSource::ExternalLayerAlpha:
            return "external_layer_alpha";
        case AdobeAlphaHintSource::ExternalLayerRed:
            return "external_layer_red";
        case AdobeAlphaHintSource::RoughFallback:
            return "rough_fallback";
    }
    return "unknown";
}

}  // namespace corridorkey::adobe
