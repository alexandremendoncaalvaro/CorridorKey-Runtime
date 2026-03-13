#include "video_io.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>
#include <libswscale/swscale.h>
}

namespace corridorkey {

std::string normalize_extension(const std::string& extension);
std::string ffmpeg_error_string(int error_code);
bool encoder_exists(const char* codec_name);
std::vector<std::string> lossless_encoders_for_container(const std::string& extension);
std::vector<std::string> balanced_encoders_for_container(const std::string& extension);
Result<VideoOutputPlan> resolve_plan_for_container(const std::filesystem::path& output_path,
                                                   const std::string& container_extension,
                                                   const VideoOutputOptions& options,
                                                   const VideoFrameFormat& input_format);
Result<VideoOutputPlan> resolve_plan_for_forced_encoder(const std::filesystem::path& output_path,
                                                        const std::string& container_extension,
                                                        const VideoOutputOptions& options,
                                                        const VideoFrameFormat& input_format,
                                                        const std::string& encoder_name);

struct AvDeleter {
    void operator()(AVFormatContext* context) const {
        AVFormatContext* raw = context;
        if (raw != nullptr) {
            avformat_close_input(&raw);
        }
    }
    void operator()(AVCodecContext* context) const {
        AVCodecContext* raw = context;
        avcodec_free_context(&raw);
    }
    void operator()(AVFrame* frame) const {
        AVFrame* raw = frame;
        av_frame_free(&raw);
    }
    void operator()(AVPacket* packet) const {
        AVPacket* raw = packet;
        av_packet_free(&raw);
    }
    void operator()(SwsContext* context) const {
        sws_freeContext(context);
    }
};

bool is_videotoolbox_available() {
#if defined(__APPLE__)
    return encoder_exists("h264_videotoolbox");
#else
    return false;
#endif
}

Result<VideoOutputPlan> resolve_video_output_plan(const std::filesystem::path& output_path,
                                                  const VideoOutputOptions& options,
                                                  const VideoFrameFormat& input_format) {
    std::string container_extension = normalize_extension(output_path.extension().string());
    if (container_extension.empty() && !options.requested_container.empty()) {
        container_extension = normalize_extension(options.requested_container);
    }

    if (!container_extension.empty()) {
        return resolve_plan_for_container(output_path, container_extension, options, input_format);
    }

    if (options.mode == VideoOutputMode::Lossless) {
        std::string last_error;
        for (const auto& extension : {std::string(".mov"), std::string(".mkv")}) {
            auto plan = resolve_plan_for_container(output_path, extension, options, input_format);
            if (plan) {
                return plan;
            }
            last_error = plan.error().message;
        }
        std::string message =
            "Lossless output is not available for this build. Use .mov or .mkv if "
            "available, or choose --video-encode balanced.";
        if (!last_error.empty()) {
            message += " Last error: " + last_error;
        }
        return Unexpected(Error{ErrorCode::InvalidParameters, message});
    }

    std::string last_error;
    for (const auto& extension :
         {std::string(".mp4"), std::string(".mov"), std::string(".mkv"), std::string(".avi")}) {
        auto plan = resolve_plan_for_container(output_path, extension, options, input_format);
        if (plan) {
            return plan;
        }
        last_error = plan.error().message;
    }

    std::string message = "No compatible video encoders found.";
    if (!last_error.empty()) {
        message += " Last error: " + last_error;
    }
    return Unexpected(Error{ErrorCode::InvalidParameters, message});
}

Result<std::filesystem::path> resolve_video_output_path(const std::filesystem::path& output_path,
                                                        const VideoOutputOptions& options,
                                                        const VideoFrameFormat& input_format) {
    auto plan = resolve_video_output_plan(output_path, options, input_format);
    if (!plan) {
        return Unexpected(plan.error());
    }
    return plan->output_path;
}

VideoOutputSupport inspect_video_output_support(const VideoFrameFormat& input_format) {
    VideoOutputSupport support;
    support.default_mode = VideoOutputMode::Lossless;

    VideoOutputOptions options;
    options.mode = VideoOutputMode::Lossless;
    auto plan = resolve_video_output_plan("output", options, input_format);
    if (!plan) {
        support.lossless_available = false;
        support.lossless_unavailable_reason = plan.error().message;
        return support;
    }

    support.lossless_available = plan->lossless;
    support.default_container = plan->container_extension;
    support.default_encoder = plan->encoder_name;
    return support;
}

std::string default_video_encoder_for_path(const std::filesystem::path& path) {
    VideoFrameFormat input_format;
    VideoOutputOptions options;
    options.mode = VideoOutputMode::Lossless;
    auto plan = resolve_video_output_plan(path, options, input_format);
    if (!plan) {
        return "";
    }
    return plan->encoder_name;
}

std::vector<std::string> available_video_encoders_for_path(const std::filesystem::path& path) {
    std::string extension = normalize_extension(path.extension().string());
    if (extension.empty()) {
        extension = ".mov";
    }

    std::vector<std::string> candidates = lossless_encoders_for_container(extension);
    auto balanced = balanced_encoders_for_container(extension);
    for (const auto& entry : balanced) {
        if (std::find(candidates.begin(), candidates.end(), entry) == candidates.end()) {
            candidates.push_back(entry);
        }
    }

    std::vector<std::string> available;
    for (const auto& entry : candidates) {
        if (encoder_exists(entry.c_str())) {
            available.push_back(entry);
        }
    }
    return available;
}

class VideoWriter::Impl {
   public:
    std::unique_ptr<AVFormatContext, AvDeleter> format_context;
    std::unique_ptr<AVCodecContext, AvDeleter> codec_context;
    AVStream* stream = nullptr;
    std::unique_ptr<SwsContext, AvDeleter> scale_context;
    std::unique_ptr<AVFrame, AvDeleter> frame;
    std::unique_ptr<AVPacket, AvDeleter> packet;

    VideoOutputPlan plan;
    VideoFrameFormat input_format;
    AVPixelFormat input_pixel_format = AV_PIX_FMT_RGB24;
    int64_t frame_count = 0;
    int64_t last_pts = std::numeric_limits<int64_t>::min();
    bool finalized = false;

    std::vector<uint8_t> rgb8_temp;
    std::vector<uint16_t> rgb16_temp;
    ImageBuffer rgb_float_temp;

    Impl() : frame(av_frame_alloc()), packet(av_packet_alloc()) {}

    Result<void> ensure_frame_buffer() {
        if (!codec_context) {
            return Unexpected(Error{ErrorCode::IoError, "FFmpeg: Encoder not initialized"});
        }

        if (!frame) {
            return Unexpected(Error{ErrorCode::IoError, "FFmpeg: Frame allocation failed"});
        }

        if (frame->format != codec_context->pix_fmt || frame->width != codec_context->width ||
            frame->height != codec_context->height || frame->data[0] == nullptr) {
            frame->format = codec_context->pix_fmt;
            frame->width = codec_context->width;
            frame->height = codec_context->height;
            int buffer_result = av_frame_get_buffer(frame.get(), 0);
            if (buffer_result < 0) {
                return Unexpected(
                    Error{ErrorCode::IoError, "FFmpeg: Could not allocate frame buffer: " +
                                                  ffmpeg_error_string(buffer_result)});
            }
        }

        int writable_result = av_frame_make_writable(frame.get());
        if (writable_result < 0) {
            return Unexpected(Error{ErrorCode::IoError, "FFmpeg: Frame not writable: " +
                                                            ffmpeg_error_string(writable_result)});
        }

        return {};
    }

    Result<void> drain_packets() {
        if (!codec_context) {
            return Unexpected(Error{ErrorCode::IoError, "FFmpeg: Encoder not initialized"});
        }

        while (true) {
            int receive_result = avcodec_receive_packet(codec_context.get(), packet.get());
            if (receive_result == 0) {
                av_packet_rescale_ts(packet.get(), codec_context->time_base, stream->time_base);
                packet->stream_index = stream->index;
                int write_result = av_interleaved_write_frame(format_context.get(), packet.get());
                av_packet_unref(packet.get());
                if (write_result < 0) {
                    return Unexpected(Error{
                        ErrorCode::IoError,
                        "FFmpeg: Error writing packet: " + ffmpeg_error_string(write_result)});
                }
                continue;
            }
            if (receive_result == AVERROR(EAGAIN) || receive_result == AVERROR_EOF) {
                break;
            }
            return Unexpected(Error{ErrorCode::IoError, "FFmpeg: Error receiving packet: " +
                                                            ffmpeg_error_string(receive_result)});
        }

        return {};
    }

    Result<void> send_frame(AVFrame* input_frame) {
        while (true) {
            int send_result = avcodec_send_frame(codec_context.get(), input_frame);
            if (send_result == AVERROR(EAGAIN)) {
                auto drain_result = drain_packets();
                if (!drain_result) {
                    return Unexpected(drain_result.error());
                }
                continue;
            }
            if (send_result < 0) {
                return Unexpected(Error{ErrorCode::IoError, "FFmpeg: Error sending frame: " +
                                                                ffmpeg_error_string(send_result)});
            }
            break;
        }

        return drain_packets();
    }
};

VideoWriter::VideoWriter() : m_impl(std::make_unique<Impl>()) {}

VideoWriter::~VideoWriter() {
    if (m_impl) {
        finalize();
    }
}

Result<std::unique_ptr<VideoWriter>> VideoWriter::open(const std::filesystem::path& path, int width,
                                                       int height, double fps,
                                                       const VideoFrameFormat& input_format,
                                                       const VideoOutputOptions& options,
                                                       const std::string& codec_name,
                                                       std::optional<VideoTimeBase> time_base) {
    auto writer = std::unique_ptr<VideoWriter>(new VideoWriter());
    auto* impl = writer->m_impl.get();

    if (width <= 0 || height <= 0) {
        return Unexpected(Error{ErrorCode::InvalidParameters, "Invalid video dimensions"});
    }
    if (fps <= 0.0 && (!time_base.has_value() || !time_base->is_valid())) {
        return Unexpected(
            Error{ErrorCode::InvalidParameters, "Invalid fps without a usable time base"});
    }

    VideoOutputPlan plan;
    if (!codec_name.empty()) {
        std::string extension = normalize_extension(path.extension().string());
        auto forced_plan =
            resolve_plan_for_forced_encoder(path, extension, options, input_format, codec_name);
        if (!forced_plan) {
            return Unexpected(forced_plan.error());
        }
        plan = *forced_plan;
    } else {
        auto plan_result = resolve_video_output_plan(path, options, input_format);
        if (!plan_result) {
            return Unexpected(plan_result.error());
        }
        plan = *plan_result;
    }

    AVFormatContext* format_context = nullptr;
    if (avformat_alloc_output_context2(&format_context, nullptr, nullptr,
                                       plan.output_path.string().c_str()) < 0) {
        return Unexpected(Error{ErrorCode::IoError, "FFmpeg: Could not create output context"});
    }
    impl->format_context.reset(format_context);

    const AVCodec* codec = avcodec_find_encoder_by_name(plan.encoder_name.c_str());
    if (codec == nullptr) {
        return Unexpected(
            Error{ErrorCode::IoError, "FFmpeg: Encoder not found: " + plan.encoder_name});
    }

    impl->codec_context.reset(avcodec_alloc_context3(codec));
    if (!impl->codec_context) {
        return Unexpected(Error{ErrorCode::IoError, "FFmpeg: Failed to allocate encoder context"});
    }

    impl->codec_context->width = width;
    impl->codec_context->height = height;
    AVRational resolved_time_base = av_inv_q(av_d2q(std::max(1.0, fps), 60000));
    if (time_base.has_value() && time_base->is_valid()) {
        resolved_time_base = AVRational{time_base->numerator, time_base->denominator};
    }
    impl->codec_context->time_base = resolved_time_base;
    if (fps > 0.0) {
        impl->codec_context->framerate = av_d2q(fps, 60000);
    }
    impl->codec_context->pix_fmt = static_cast<AVPixelFormat>(plan.pixel_format);

    impl->stream = avformat_new_stream(impl->format_context.get(), nullptr);
    if (impl->stream == nullptr) {
        return Unexpected(Error{ErrorCode::IoError, "FFmpeg: Could not create stream"});
    }
    if (impl->format_context->oformat->flags & AVFMT_GLOBALHEADER) {
        impl->codec_context->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }

    std::string encoder_name = plan.encoder_name;
    if (encoder_name.find("libx264") != std::string::npos) {
        av_opt_set(impl->codec_context->priv_data, "preset",
                   options.mode == VideoOutputMode::Lossless ? "veryslow" : "slow", 0);
        if (options.mode == VideoOutputMode::Lossless) {
            av_opt_set(impl->codec_context->priv_data, "crf", "0", 0);
            av_opt_set(impl->codec_context->priv_data, "qp", "0", 0);
            av_opt_set(impl->codec_context->priv_data, "profile", "high444", 0);
        } else {
            av_opt_set(impl->codec_context->priv_data, "crf", "18", 0);
        }
    }

    if (avcodec_open2(impl->codec_context.get(), codec, nullptr) < 0) {
        return Unexpected(
            Error{ErrorCode::IoError, "FFmpeg: Could not open encoder: " + plan.encoder_name});
    }

    impl->stream->time_base = impl->codec_context->time_base;
    if (fps > 0.0) {
        impl->stream->avg_frame_rate = av_d2q(fps, 60000);
        impl->stream->r_frame_rate = impl->stream->avg_frame_rate;
    }

    int param_result =
        avcodec_parameters_from_context(impl->stream->codecpar, impl->codec_context.get());
    if (param_result < 0) {
        return Unexpected(Error{ErrorCode::IoError, "FFmpeg: Failed to copy encoder parameters: " +
                                                        ffmpeg_error_string(param_result)});
    }

    if (!(impl->format_context->oformat->flags & AVFMT_NOFILE)) {
        if (avio_open(&impl->format_context->pb, plan.output_path.string().c_str(),
                      AVIO_FLAG_WRITE) < 0) {
            return Unexpected(Error{ErrorCode::IoError, "FFmpeg: Could not open output file"});
        }
    }

    int header_result = avformat_write_header(impl->format_context.get(), nullptr);
    if (header_result < 0) {
        return Unexpected(Error{ErrorCode::IoError, "FFmpeg: Could not write header: " +
                                                        ffmpeg_error_string(header_result)});
    }

    impl->plan = plan;
    impl->input_format = input_format;
    if (input_format.is_float) {
        impl->input_pixel_format = AV_PIX_FMT_RGBF32LE;
    } else if (input_format.bits_per_component > 8) {
        impl->input_pixel_format = AV_PIX_FMT_RGB48LE;
    } else {
        impl->input_pixel_format = AV_PIX_FMT_RGB24;
    }

    auto buffer_result = impl->ensure_frame_buffer();
    if (!buffer_result) {
        return Unexpected(buffer_result.error());
    }

    return writer;
}

Result<std::unique_ptr<VideoWriter>> VideoWriter::open(const std::filesystem::path& path, int width,
                                                       int height, double fps,
                                                       const std::string& codec_name,
                                                       std::optional<VideoTimeBase> time_base) {
    VideoFrameFormat input_format;
    VideoOutputOptions options;
    return VideoWriter::open(path, width, height, fps, input_format, options, codec_name,
                             time_base);
}

Result<void> VideoWriter::write_frame(const Image& image) {
    return write_frame(image, std::nullopt);
}

Result<void> VideoWriter::write_frame(const Image& image, std::optional<int64_t> pts_us) {
    auto* impl = m_impl.get();
    if (!impl->codec_context) {
        return Unexpected(Error{ErrorCode::IoError, "FFmpeg: Encoder not initialized"});
    }

    if (image.empty()) {
        return Unexpected(Error{ErrorCode::InvalidParameters, "Cannot encode empty frame"});
    }

    auto frame_result = impl->ensure_frame_buffer();
    if (!frame_result) {
        return Unexpected(frame_result.error());
    }

    int width = image.width;
    int height = image.height;
    int input_channels = image.channels;
    int output_channels = 3;

    impl->scale_context.reset(sws_getCachedContext(
        impl->scale_context.release(), width, height, impl->input_pixel_format,
        impl->codec_context->width, impl->codec_context->height, impl->codec_context->pix_fmt,
        SWS_BILINEAR, nullptr, nullptr, nullptr));
    if (!impl->scale_context) {
        return Unexpected(Error{ErrorCode::IoError, "FFmpeg: Failed to allocate scaler"});
    }

    if (impl->input_pixel_format == AV_PIX_FMT_RGB24) {
        size_t required_size = static_cast<size_t>(width) * height * output_channels;
        if (impl->rgb8_temp.size() != required_size) {
            impl->rgb8_temp.assign(required_size, 0);
        }

        for (int y_pos = 0; y_pos < height; ++y_pos) {
            for (int x_pos = 0; x_pos < width; ++x_pos) {
                size_t src_index = static_cast<size_t>(y_pos * width + x_pos) * input_channels;
                size_t dst_index = static_cast<size_t>(y_pos * width + x_pos) * output_channels;
                for (int channel = 0; channel < output_channels; ++channel) {
                    int source_channel = std::min(channel, input_channels - 1);
                    float value = image.data[src_index + source_channel];
                    value = std::clamp(value, 0.0f, 1.0f);
                    impl->rgb8_temp[dst_index + channel] =
                        static_cast<uint8_t>(value * 255.0f + 0.5f);
                }
            }
        }

        const uint8_t* src_data[1] = {impl->rgb8_temp.data()};
        int src_linesize[1] = {width * output_channels};
        sws_scale(impl->scale_context.get(), src_data, src_linesize, 0, height, impl->frame->data,
                  impl->frame->linesize);
    } else if (impl->input_pixel_format == AV_PIX_FMT_RGB48LE) {
        size_t required_size = static_cast<size_t>(width) * height * output_channels;
        if (impl->rgb16_temp.size() != required_size) {
            impl->rgb16_temp.assign(required_size, 0);
        }

        for (int y_pos = 0; y_pos < height; ++y_pos) {
            for (int x_pos = 0; x_pos < width; ++x_pos) {
                size_t src_index = static_cast<size_t>(y_pos * width + x_pos) * input_channels;
                size_t dst_index = static_cast<size_t>(y_pos * width + x_pos) * output_channels;
                for (int channel = 0; channel < output_channels; ++channel) {
                    int source_channel = std::min(channel, input_channels - 1);
                    float value = image.data[src_index + source_channel];
                    value = std::clamp(value, 0.0f, 1.0f);
                    impl->rgb16_temp[dst_index + channel] =
                        static_cast<uint16_t>(value * 65535.0f + 0.5f);
                }
            }
        }

        const uint8_t* src_data[1] = {reinterpret_cast<const uint8_t*>(impl->rgb16_temp.data())};
        int src_linesize[1] = {width * output_channels * static_cast<int>(sizeof(uint16_t))};
        sws_scale(impl->scale_context.get(), src_data, src_linesize, 0, height, impl->frame->data,
                  impl->frame->linesize);
    } else {
        const float* source_data = image.data.data();
        if (input_channels != output_channels) {
            Image temp_view = impl->rgb_float_temp.view();
            if (temp_view.empty() || temp_view.width != width || temp_view.height != height ||
                temp_view.channels != output_channels) {
                impl->rgb_float_temp = ImageBuffer(width, height, output_channels);
                temp_view = impl->rgb_float_temp.view();
            }

            for (int y_pos = 0; y_pos < height; ++y_pos) {
                for (int x_pos = 0; x_pos < width; ++x_pos) {
                    size_t src_index = static_cast<size_t>(y_pos * width + x_pos) * input_channels;
                    size_t dst_index = static_cast<size_t>(y_pos * width + x_pos) * output_channels;
                    for (int channel = 0; channel < output_channels; ++channel) {
                        int source_channel = std::min(channel, input_channels - 1);
                        temp_view.data[dst_index + channel] =
                            image.data[src_index + source_channel];
                    }
                }
            }
            source_data = temp_view.data.data();
        }

        const uint8_t* src_data[1] = {reinterpret_cast<const uint8_t*>(source_data)};
        int src_linesize[1] = {width * output_channels * static_cast<int>(sizeof(float))};
        sws_scale(impl->scale_context.get(), src_data, src_linesize, 0, height, impl->frame->data,
                  impl->frame->linesize);
    }

    int64_t pts = 0;
    if (pts_us) {
        pts = av_rescale_q(*pts_us, AVRational{1, AV_TIME_BASE}, impl->codec_context->time_base);
    } else {
        pts = impl->frame_count++;
    }

    if (pts < 0) {
        pts = 0;
    }
    if (impl->last_pts != std::numeric_limits<int64_t>::min() && pts <= impl->last_pts) {
        pts = impl->last_pts + 1;
    }
    impl->last_pts = pts;
    impl->frame->pts = pts;

    auto send_result = impl->send_frame(impl->frame.get());
    if (!send_result) {
        return Unexpected(send_result.error());
    }

    return {};
}

Result<void> VideoWriter::finalize() {
    auto* impl = m_impl.get();
    if (impl == nullptr || impl->finalized) {
        return {};
    }

    if (impl->codec_context) {
        auto flush_result = impl->send_frame(nullptr);
        if (!flush_result) {
            return Unexpected(flush_result.error());
        }
    }

    if (impl->format_context) {
        int trailer_result = av_write_trailer(impl->format_context.get());
        if (trailer_result < 0) {
            return Unexpected(Error{ErrorCode::IoError, "FFmpeg: Could not write trailer: " +
                                                            ffmpeg_error_string(trailer_result)});
        }
        if (!(impl->format_context->oformat->flags & AVFMT_NOFILE)) {
            avio_closep(&impl->format_context->pb);
        }
    }

    impl->finalized = true;
    return {};
}

std::string to_lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return value;
}

std::string normalize_extension(const std::string& extension) {
    if (extension.empty()) {
        return "";
    }
    if (extension.front() == '.') {
        return to_lower(extension);
    }
    return "." + to_lower(extension);
}

std::string ffmpeg_error_string(int error_code) {
    std::array<char, 256> buffer{};
    av_strerror(error_code, buffer.data(), buffer.size());
    return std::string(buffer.data());
}

bool encoder_exists(const char* codec_name) {
    return codec_name != nullptr && avcodec_find_encoder_by_name(codec_name) != nullptr;
}

int max_bits_for_pixel_format(AVPixelFormat format) {
    const AVPixFmtDescriptor* descriptor = av_pix_fmt_desc_get(format);
    if (descriptor == nullptr) {
        return 8;
    }

    int max_bits = 0;
    for (int index = 0; index < descriptor->nb_components; ++index) {
        max_bits = std::max(max_bits, static_cast<int>(descriptor->comp[index].depth));
    }
    return max_bits > 0 ? max_bits : 8;
}

bool pixel_format_is_float(AVPixelFormat format) {
    const AVPixFmtDescriptor* descriptor = av_pix_fmt_desc_get(format);
    if (descriptor == nullptr) {
        return false;
    }
    return (descriptor->flags & AV_PIX_FMT_FLAG_FLOAT) != 0;
}

bool pixel_format_is_chroma_subsampled(AVPixelFormat format) {
    const AVPixFmtDescriptor* descriptor = av_pix_fmt_desc_get(format);
    if (descriptor == nullptr) {
        return false;
    }
    return descriptor->log2_chroma_w > 0 || descriptor->log2_chroma_h > 0;
}

bool pixel_format_is_rgb(AVPixelFormat format) {
    const AVPixFmtDescriptor* descriptor = av_pix_fmt_desc_get(format);
    if (descriptor == nullptr) {
        return false;
    }
    return (descriptor->flags & AV_PIX_FMT_FLAG_RGB) != 0;
}

std::vector<AVPixelFormat> supported_formats_for_codec(const AVCodec* codec) {
    std::vector<AVPixelFormat> formats;
    if (codec == nullptr) {
        return formats;
    }

    const void* config_values = nullptr;
    int config_count = 0;
    int config_result = avcodec_get_supported_config(nullptr, codec, AV_CODEC_CONFIG_PIX_FORMAT, 0,
                                                     &config_values, &config_count);
    if (config_result >= 0 && config_values != nullptr && config_count > 0) {
        auto pixel_formats = static_cast<const AVPixelFormat*>(config_values);
        formats.assign(pixel_formats, pixel_formats + config_count);
        return formats;
    }

    return formats;
}

struct PixelFormatCandidate {
    AVPixelFormat format = AV_PIX_FMT_NONE;
    int bits_per_component = 8;
    bool is_float = false;
    bool is_rgb = false;
    bool is_subsampled = false;
};

PixelFormatCandidate build_pixel_format_candidate(AVPixelFormat format) {
    PixelFormatCandidate candidate;
    candidate.format = format;
    candidate.bits_per_component = max_bits_for_pixel_format(format);
    candidate.is_float = pixel_format_is_float(format);
    candidate.is_rgb = pixel_format_is_rgb(format);
    candidate.is_subsampled = pixel_format_is_chroma_subsampled(format);
    return candidate;
}

bool preserves_input_precision(const VideoFrameFormat& input_format,
                               const PixelFormatCandidate& candidate) {
    if (input_format.is_float) {
        return candidate.is_float;
    }
    if (candidate.is_float) {
        return true;
    }
    return candidate.bits_per_component >= input_format.bits_per_component;
}

std::string describe_input_format(const VideoFrameFormat& input_format) {
    if (input_format.is_float) {
        return "float";
    }
    return std::to_string(input_format.bits_per_component) + "-bit";
}

std::optional<PixelFormatCandidate> choose_lossless_pixel_format(
    const std::vector<AVPixelFormat>& formats, const VideoFrameFormat& input_format) {
    std::vector<PixelFormatCandidate> viable;
    for (AVPixelFormat format : formats) {
        if (format == AV_PIX_FMT_NONE) {
            continue;
        }
        PixelFormatCandidate candidate = build_pixel_format_candidate(format);
        if (candidate.is_subsampled) {
            continue;
        }
        if (!preserves_input_precision(input_format, candidate)) {
            continue;
        }
        viable.push_back(candidate);
    }

    if (viable.empty()) {
        return std::nullopt;
    }

    std::sort(viable.begin(), viable.end(),
              [&](const PixelFormatCandidate& left, const PixelFormatCandidate& right) {
                  if (left.is_rgb != right.is_rgb) {
                      return left.is_rgb;
                  }
                  if (left.is_float != right.is_float) {
                      return left.is_float;
                  }
                  return left.bits_per_component > right.bits_per_component;
              });

    return viable.front();
}

std::optional<PixelFormatCandidate> choose_balanced_pixel_format(
    const std::vector<AVPixelFormat>& formats, const VideoFrameFormat& input_format) {
    if (formats.empty()) {
        return std::nullopt;
    }

    std::vector<PixelFormatCandidate> candidates;
    candidates.reserve(formats.size());
    for (AVPixelFormat format : formats) {
        if (format == AV_PIX_FMT_NONE) {
            continue;
        }
        candidates.push_back(build_pixel_format_candidate(format));
    }

    if (candidates.empty()) {
        return std::nullopt;
    }

    auto pick_by_preference =
        [&](const std::vector<AVPixelFormat>& preferred) -> std::optional<PixelFormatCandidate> {
        for (AVPixelFormat preferred_format : preferred) {
            auto it = std::find_if(candidates.begin(), candidates.end(),
                                   [&](const PixelFormatCandidate& candidate) {
                                       return candidate.format == preferred_format;
                                   });
            if (it != candidates.end()) {
                return *it;
            }
        }
        return std::nullopt;
    };

    if (input_format.is_float || input_format.bits_per_component > 8) {
        auto preferred =
            pick_by_preference({AV_PIX_FMT_YUV444P10LE, AV_PIX_FMT_YUV422P10LE,
                                AV_PIX_FMT_YUV420P10LE, AV_PIX_FMT_YUV444P, AV_PIX_FMT_YUV422P,
                                AV_PIX_FMT_YUV420P, AV_PIX_FMT_RGB48LE, AV_PIX_FMT_RGB24});
        if (preferred.has_value()) {
            return preferred;
        }
    } else {
        auto preferred =
            pick_by_preference({AV_PIX_FMT_YUV420P, AV_PIX_FMT_NV12, AV_PIX_FMT_YUV422P,
                                AV_PIX_FMT_YUV444P, AV_PIX_FMT_RGB24});
        if (preferred.has_value()) {
            return preferred;
        }
    }

    std::sort(candidates.begin(), candidates.end(),
              [&](const PixelFormatCandidate& left, const PixelFormatCandidate& right) {
                  if (left.is_rgb != right.is_rgb) {
                      return left.is_rgb;
                  }
                  return left.bits_per_component > right.bits_per_component;
              });
    return candidates.front();
}

std::vector<std::string> lossless_encoders_for_container(const std::string& extension) {
    if (extension == ".mov") {
        return {"png", "qtrle"};
    }
    if (extension == ".mkv") {
        return {"ffv1", "utvideo"};
    }
    if (extension == ".avi") {
        return {"ffv1", "huffyuv"};
    }
    if (extension == ".mp4") {
        return {"libx264rgb", "libx264"};
    }
    return {};
}

std::vector<std::string> balanced_encoders_for_container(const std::string& extension) {
    if (extension == ".mp4" || extension == ".mov" || extension == ".mkv" || extension == ".avi") {
        return {"libx264rgb", "libx264", "h264_videotoolbox", "h264", "mpeg4"};
    }
    return {"libx264rgb", "libx264", "h264_videotoolbox", "h264", "mpeg4"};
}

Result<VideoOutputPlan> build_plan_for_candidates(const std::filesystem::path& output_path,
                                                  const std::string& container_extension,
                                                  VideoOutputMode mode,
                                                  const VideoFrameFormat& input_format,
                                                  const std::vector<std::string>& candidates) {
    std::string last_error;

    for (const auto& encoder_name : candidates) {
        if (!encoder_exists(encoder_name.c_str())) {
            last_error = "Encoder not available: " + encoder_name;
            continue;
        }

        const AVCodec* codec = avcodec_find_encoder_by_name(encoder_name.c_str());
        if (codec == nullptr) {
            last_error = "Encoder not found: " + encoder_name;
            continue;
        }

        auto supported_formats = supported_formats_for_codec(codec);
        std::optional<PixelFormatCandidate> pixel_choice;
        if (mode == VideoOutputMode::Lossless) {
            pixel_choice = choose_lossless_pixel_format(supported_formats, input_format);
        } else {
            pixel_choice = choose_balanced_pixel_format(supported_formats, input_format);
        }

        if (!pixel_choice.has_value()) {
            std::string requirement = mode == VideoOutputMode::Lossless
                                          ? "format without chroma subsampling"
                                          : "output format";
            last_error = "Encoder " + encoder_name + " does not support a " +
                         describe_input_format(input_format) + " " + requirement + ".";
            continue;
        }

        VideoOutputPlan plan;
        plan.output_path = output_path;
        plan.container_extension = container_extension;
        plan.encoder_name = encoder_name;
        plan.mode = mode;
        plan.lossless = mode == VideoOutputMode::Lossless;
        plan.bits_per_component = pixel_choice->bits_per_component;
        plan.pixel_format = static_cast<int>(pixel_choice->format);
        return plan;
    }

    if (last_error.empty()) {
        last_error = "No compatible encoders were found.";
    }

    return Unexpected(Error{ErrorCode::InvalidParameters, last_error});
}

Result<VideoOutputPlan> resolve_plan_for_container(const std::filesystem::path& output_path,
                                                   const std::string& container_extension,
                                                   const VideoOutputOptions& options,
                                                   const VideoFrameFormat& input_format) {
    std::filesystem::path resolved_path = output_path;
    if (resolved_path.extension().empty()) {
        resolved_path = std::filesystem::path(output_path.string() + container_extension);
    }

    if (options.mode == VideoOutputMode::Lossless) {
        auto lossless_candidates = lossless_encoders_for_container(container_extension);
        auto lossless_plan =
            build_plan_for_candidates(resolved_path, container_extension, VideoOutputMode::Lossless,
                                      input_format, lossless_candidates);
        if (lossless_plan) {
            return lossless_plan;
        }

        if (!options.allow_lossy_fallback) {
            std::string message = "Lossless output is not available for " + container_extension +
                                  ": " + lossless_plan.error().message;
            if (container_extension == ".mp4") {
                message += " Lossless MP4 requires libx264rgb or libx264 built with 4:4:4 support.";
            }
            return Unexpected(Error{ErrorCode::InvalidParameters, message});
        }
    }

    auto balanced_candidates = balanced_encoders_for_container(container_extension);
    auto balanced_plan =
        build_plan_for_candidates(resolved_path, container_extension, VideoOutputMode::Balanced,
                                  input_format, balanced_candidates);
    if (!balanced_plan) {
        return Unexpected(balanced_plan.error());
    }
    balanced_plan->lossless = false;
    return balanced_plan;
}

Result<VideoOutputPlan> resolve_plan_for_forced_encoder(const std::filesystem::path& output_path,
                                                        const std::string& container_extension,
                                                        const VideoOutputOptions& options,
                                                        const VideoFrameFormat& input_format,
                                                        const std::string& encoder_name) {
    if (!encoder_exists(encoder_name.c_str())) {
        return Unexpected(
            Error{ErrorCode::InvalidParameters, "Encoder not available: " + encoder_name});
    }

    const AVCodec* codec = avcodec_find_encoder_by_name(encoder_name.c_str());
    if (codec == nullptr) {
        return Unexpected(
            Error{ErrorCode::InvalidParameters, "Encoder not found: " + encoder_name});
    }

    auto supported_formats = supported_formats_for_codec(codec);
    std::optional<PixelFormatCandidate> pixel_choice;
    if (options.mode == VideoOutputMode::Lossless) {
        pixel_choice = choose_lossless_pixel_format(supported_formats, input_format);
    } else {
        pixel_choice = choose_balanced_pixel_format(supported_formats, input_format);
    }

    if (!pixel_choice.has_value()) {
        std::string requirement = options.mode == VideoOutputMode::Lossless
                                      ? "format without chroma subsampling"
                                      : "output format";
        return Unexpected(Error{ErrorCode::InvalidParameters,
                                "Encoder " + encoder_name + " does not support a " +
                                    describe_input_format(input_format) + " " + requirement + "."});
    }

    std::filesystem::path resolved_path = output_path;
    if (resolved_path.extension().empty() && !container_extension.empty()) {
        resolved_path = std::filesystem::path(output_path.string() + container_extension);
    }

    VideoOutputPlan plan;
    plan.output_path = resolved_path;
    plan.container_extension = container_extension;
    plan.encoder_name = encoder_name;
    plan.mode = options.mode;
    plan.lossless = options.mode == VideoOutputMode::Lossless;
    plan.bits_per_component = pixel_choice->bits_per_component;
    plan.pixel_format = static_cast<int>(pixel_choice->format);
    return plan;
}

// --- VideoReader Implementation ---

class VideoReader::Impl {
   public:
    std::unique_ptr<AVFormatContext, AvDeleter> format_context;
    std::unique_ptr<AVCodecContext, AvDeleter> codec_context;
    std::unique_ptr<SwsContext, AvDeleter> scale_context;
    std::unique_ptr<AVFrame, AvDeleter> frame;
    std::unique_ptr<AVPacket, AvDeleter> packet;
    int stream_index = -1;

    std::vector<uint8_t> rgb8_temp;
    std::vector<uint16_t> rgb16_temp;
    VideoFrameFormat input_format;
    AVPixelFormat output_pixel_format = AV_PIX_FMT_RGB24;

    bool eof_reached = false;
    bool decoder_flushed = false;
    bool pending_packet = false;
    bool pending_flush = false;

    Impl() : frame(av_frame_alloc()), packet(av_packet_alloc()) {}

    Result<ImageBuffer> convert_frame() {
        if (!codec_context) {
            return Unexpected(Error{ErrorCode::IoError, "FFmpeg: Decoder not initialized"});
        }

        int width = codec_context->width;
        int height = codec_context->height;
        int output_channels = 3;

        auto source_format = static_cast<AVPixelFormat>(frame->format);
        scale_context.reset(sws_getCachedContext(scale_context.release(), width, height,
                                                 source_format, width, height, output_pixel_format,
                                                 SWS_BILINEAR, nullptr, nullptr, nullptr));
        if (!scale_context) {
            return Unexpected(Error{ErrorCode::IoError, "FFmpeg: Failed to allocate scaler"});
        }

        if (output_pixel_format == AV_PIX_FMT_RGBF32LE) {
            ImageBuffer buffer(width, height, output_channels);
            Image view = buffer.view();
            uint8_t* rgb_data[1] = {reinterpret_cast<uint8_t*>(view.data.data())};
            int rgb_linesize[1] = {width * output_channels * static_cast<int>(sizeof(float))};
            sws_scale(scale_context.get(), frame->data, frame->linesize, 0, height, rgb_data,
                      rgb_linesize);
            return buffer;
        }

        if (output_pixel_format == AV_PIX_FMT_RGB24) {
            size_t rgb_size = static_cast<size_t>(width) * height * output_channels;
            if (rgb8_temp.size() != rgb_size) {
                rgb8_temp.assign(rgb_size, 0);
            }

            uint8_t* rgb_data[1] = {rgb8_temp.data()};
            int rgb_linesize[1] = {width * output_channels};
            sws_scale(scale_context.get(), frame->data, frame->linesize, 0, height, rgb_data,
                      rgb_linesize);

            ImageBuffer buffer(width, height, output_channels);
            Image view = buffer.view();
            for (size_t index = 0; index < rgb_size; ++index) {
                view.data[index] = rgb8_temp[index] / 255.0f;
            }
            return buffer;
        }

        size_t rgb_size = static_cast<size_t>(width) * height * output_channels;
        if (rgb16_temp.size() != rgb_size) {
            rgb16_temp.assign(rgb_size, 0);
        }

        uint8_t* rgb_data[1] = {reinterpret_cast<uint8_t*>(rgb16_temp.data())};
        int rgb_linesize[1] = {width * output_channels * static_cast<int>(sizeof(uint16_t))};
        sws_scale(scale_context.get(), frame->data, frame->linesize, 0, height, rgb_data,
                  rgb_linesize);

        ImageBuffer buffer(width, height, output_channels);
        Image view = buffer.view();
        for (size_t index = 0; index < rgb_size; ++index) {
            view.data[index] = rgb16_temp[index] / 65535.0f;
        }
        return buffer;
    }
};

VideoReader::VideoReader() : m_impl(std::make_unique<Impl>()) {}
VideoReader::~VideoReader() = default;

Result<std::unique_ptr<VideoReader>> VideoReader::open(const std::filesystem::path& path) {
    auto reader = std::unique_ptr<VideoReader>(new VideoReader());
    AVFormatContext* format_context = nullptr;

    if (avformat_open_input(&format_context, path.string().c_str(), nullptr, nullptr) < 0) {
        return Unexpected(
            Error{ErrorCode::IoError, "FFmpeg: Could not open file " + path.string()});
    }
    reader->m_impl->format_context.reset(format_context);

    if (avformat_find_stream_info(reader->m_impl->format_context.get(), nullptr) < 0) {
        return Unexpected(Error{ErrorCode::IoError, "FFmpeg: Could not find stream info"});
    }

    const AVCodec* codec = nullptr;
    reader->m_impl->stream_index = av_find_best_stream(reader->m_impl->format_context.get(),
                                                       AVMEDIA_TYPE_VIDEO, -1, -1, &codec, 0);
    if (reader->m_impl->stream_index < 0 || codec == nullptr) {
        return Unexpected(Error{ErrorCode::IoError, "FFmpeg: Could not find video stream"});
    }

    reader->m_impl->codec_context.reset(avcodec_alloc_context3(codec));
    if (!reader->m_impl->codec_context) {
        return Unexpected(Error{ErrorCode::IoError, "FFmpeg: Failed to allocate codec context"});
    }

    int param_result = avcodec_parameters_to_context(
        reader->m_impl->codec_context.get(),
        reader->m_impl->format_context->streams[reader->m_impl->stream_index]->codecpar);
    if (param_result < 0) {
        return Unexpected(Error{ErrorCode::IoError, "FFmpeg: Failed to load codec parameters"});
    }

    reader->m_impl->codec_context->thread_count = 0;

    if (avcodec_open2(reader->m_impl->codec_context.get(), codec, nullptr) < 0) {
        return Unexpected(Error{ErrorCode::IoError, "FFmpeg: Could not open codec"});
    }

    AVPixelFormat input_format = reader->m_impl->codec_context->pix_fmt;
    if (input_format == AV_PIX_FMT_NONE) {
        auto codec_parameters =
            reader->m_impl->format_context->streams[reader->m_impl->stream_index]->codecpar;
        if (codec_parameters->format != AV_PIX_FMT_NONE) {
            input_format = static_cast<AVPixelFormat>(codec_parameters->format);
        }
    }

    reader->m_impl->input_format.bits_per_component = max_bits_for_pixel_format(input_format);
    reader->m_impl->input_format.is_float = pixel_format_is_float(input_format);
    if (reader->m_impl->input_format.is_float) {
        reader->m_impl->output_pixel_format = AV_PIX_FMT_RGBF32LE;
    } else if (reader->m_impl->input_format.bits_per_component > 8) {
        reader->m_impl->output_pixel_format = AV_PIX_FMT_RGB48LE;
    } else {
        reader->m_impl->output_pixel_format = AV_PIX_FMT_RGB24;
    }

    return reader;
}

Result<VideoFrame> VideoReader::read_next_frame() {
    auto* impl = m_impl.get();

    while (true) {
        int receive_result = avcodec_receive_frame(impl->codec_context.get(), impl->frame.get());
        if (receive_result == 0) {
            auto buffer_res = impl->convert_frame();
            if (!buffer_res) {
                return Unexpected(buffer_res.error());
            }

            VideoFrame output;
            output.buffer = std::move(*buffer_res);
            output.pts_us = std::nullopt;

            int64_t best_effort = impl->frame->best_effort_timestamp;
            if (best_effort == AV_NOPTS_VALUE) {
                best_effort = impl->frame->pts;
            }
            if (best_effort != AV_NOPTS_VALUE) {
                auto* stream = impl->format_context->streams[impl->stream_index];
                output.pts_us =
                    av_rescale_q(best_effort, stream->time_base, AVRational{1, AV_TIME_BASE});
            }

            return output;
        }
        if (receive_result == AVERROR_EOF) {
            return VideoFrame{};
        }
        if (receive_result != AVERROR(EAGAIN)) {
            return Unexpected(Error{ErrorCode::IoError, "FFmpeg: Error receiving frame: " +
                                                            ffmpeg_error_string(receive_result)});
        }

        if (impl->pending_packet) {
            int send_result = avcodec_send_packet(impl->codec_context.get(), impl->packet.get());
            if (send_result == AVERROR(EAGAIN)) {
                continue;
            }
            if (send_result < 0) {
                return Unexpected(Error{ErrorCode::IoError, "FFmpeg: Error sending packet: " +
                                                                ffmpeg_error_string(send_result)});
            }
            impl->pending_packet = false;
            av_packet_unref(impl->packet.get());
            continue;
        }

        if (impl->pending_flush) {
            int send_result = avcodec_send_packet(impl->codec_context.get(), nullptr);
            if (send_result == AVERROR(EAGAIN)) {
                continue;
            }
            if (send_result < 0) {
                return Unexpected(Error{ErrorCode::IoError, "FFmpeg: Error flushing decoder: " +
                                                                ffmpeg_error_string(send_result)});
            }
            impl->pending_flush = false;
            impl->decoder_flushed = true;
            continue;
        }

        if (impl->eof_reached && impl->decoder_flushed) {
            return VideoFrame{};
        }

        int read_result = av_read_frame(impl->format_context.get(), impl->packet.get());
        if (read_result < 0) {
            impl->eof_reached = true;
            if (!impl->decoder_flushed) {
                impl->pending_flush = true;
                continue;
            }
            return VideoFrame{};
        }

        if (impl->packet->stream_index != impl->stream_index) {
            av_packet_unref(impl->packet.get());
            continue;
        }

        impl->pending_packet = true;
    }
}

int VideoReader::width() const {
    return m_impl->codec_context->width;
}

int VideoReader::height() const {
    return m_impl->codec_context->height;
}

double VideoReader::fps() const {
    auto* stream = m_impl->format_context->streams[m_impl->stream_index];
    auto to_double = [](AVRational value) -> double {
        return value.den != 0 ? static_cast<double>(value.num) / value.den : 0.0;
    };

    double guess = to_double(av_guess_frame_rate(m_impl->format_context.get(), stream, nullptr));
    if (guess >= 5.0) {
        return guess;
    }

    double avg = to_double(stream->avg_frame_rate);
    if (avg >= 5.0) {
        return avg;
    }

    double r_frame = to_double(stream->r_frame_rate);
    if (r_frame >= 5.0) {
        return r_frame;
    }

    if (stream->nb_frames > 0 && stream->duration > 0 && stream->time_base.den > 0) {
        double seconds = stream->duration * av_q2d(stream->time_base);
        if (seconds > 0.0) {
            return static_cast<double>(stream->nb_frames) / seconds;
        }
    }

    if (guess > 0.0) {
        return guess;
    }
    if (avg > 0.0) {
        return avg;
    }
    if (r_frame > 0.0) {
        return r_frame;
    }

    return 0.0;
}

std::optional<VideoTimeBase> VideoReader::time_base() const {
    auto* stream = m_impl->format_context->streams[m_impl->stream_index];
    if (stream == nullptr) {
        return std::nullopt;
    }
    if (stream->time_base.num <= 0 || stream->time_base.den <= 0) {
        return std::nullopt;
    }
    return VideoTimeBase{stream->time_base.num, stream->time_base.den};
}

int64_t VideoReader::total_frames() const {
    auto* stream = m_impl->format_context->streams[m_impl->stream_index];
    if (stream->nb_frames > 0) {
        return stream->nb_frames;
    }
    if (stream->duration > 0 && stream->time_base.den > 0) {
        return static_cast<int64_t>(stream->duration * av_q2d(stream->time_base) * fps());
    }
    return -1;
}

VideoFrameFormat VideoReader::format() const {
    return m_impl->input_format;
}

}  // namespace corridorkey
