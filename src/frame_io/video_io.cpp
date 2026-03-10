#include "video_io.hpp"

#include <algorithm>
#include <vector>

#include "common/srgb_lut.hpp"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/mathematics.h>
#include <libswscale/swscale.h>
}

namespace corridorkey {

// --- RAII Helpers for FFmpeg 8.x ---

struct AvDeleter {
    void operator()(AVFormatContext* p) const {
        AVFormatContext* ptr = p;
        avformat_close_input(&ptr);
    }
    void operator()(AVCodecContext* p) const {
        AVCodecContext* ptr = p;
        avcodec_free_context(&ptr);
    }
    void operator()(AVFrame* p) const {
        AVFrame* ptr = p;
        av_frame_free(&ptr);
    }
    void operator()(AVPacket* p) const {
        AVPacket* ptr = p;
        av_packet_free(&ptr);
    }
    void operator()(SwsContext* p) const {
        sws_freeContext(p);
    }
};

template <typename T>
using AvPtr = std::unique_ptr<T, AvDeleter>;

// --- VideoReader Implementation ---

class VideoReader::Impl {
   public:
    AvPtr<AVFormatContext> format_ctx;
    AvPtr<AVCodecContext> codec_ctx;
    AvPtr<SwsContext> sws_ctx;
    AvPtr<AVFrame> frame;
    AvPtr<AVPacket> packet;
    int stream_index = -1;

    // Reusable intermediate buffer for YUV->RGB conversion
    std::vector<uint8_t> rgb_temp;

    Impl() : frame(av_frame_alloc()), packet(av_packet_alloc()) {}
};

VideoReader::VideoReader() : m_impl(std::make_unique<Impl>()) {}
VideoReader::~VideoReader() = default;

Result<std::unique_ptr<VideoReader>> VideoReader::open(const std::filesystem::path& path) {
    auto reader = std::unique_ptr<VideoReader>(new VideoReader());
    AVFormatContext* fmt_ptr = nullptr;

    if (avformat_open_input(&fmt_ptr, path.string().c_str(), nullptr, nullptr) < 0) {
        return Unexpected(
            Error{ErrorCode::IoError, "FFmpeg: Could not open file " + path.string()});
    }
    reader->m_impl->format_ctx.reset(fmt_ptr);

    if (avformat_find_stream_info(reader->m_impl->format_ctx.get(), nullptr) < 0) {
        return Unexpected(Error{ErrorCode::IoError, "FFmpeg: Could not find stream info"});
    }

    const AVCodec* codec = nullptr;
    reader->m_impl->stream_index = av_find_best_stream(reader->m_impl->format_ctx.get(),
                                                       AVMEDIA_TYPE_VIDEO, -1, -1, &codec, 0);
    if (reader->m_impl->stream_index < 0) {
        return Unexpected(Error{ErrorCode::IoError, "FFmpeg: Could not find video stream"});
    }

    reader->m_impl->codec_ctx.reset(avcodec_alloc_context3(codec));
    avcodec_parameters_to_context(
        reader->m_impl->codec_ctx.get(),
        reader->m_impl->format_ctx->streams[reader->m_impl->stream_index]->codecpar);

    reader->m_impl->codec_ctx->thread_count = 0;  // Auto threads

    if (avcodec_open2(reader->m_impl->codec_ctx.get(), codec, nullptr) < 0) {
        return Unexpected(Error{ErrorCode::IoError, "FFmpeg: Could not open codec"});
    }

    return std::move(reader);
}

Result<ImageBuffer> VideoReader::read_next_frame() {
    auto* impl = m_impl.get();

    while (true) {
        // 1. Try to receive a frame from the decoder first (draining)
        int ret = avcodec_receive_frame(impl->codec_ctx.get(), impl->frame.get());
        if (ret == 0) {
            int w = impl->codec_ctx->width;
            int h = impl->codec_ctx->height;

            // Initialize or update scaler
            impl->sws_ctx.reset(
                sws_getCachedContext(impl->sws_ctx.release(), w, h, impl->codec_ctx->pix_fmt, w, h,
                                     AV_PIX_FMT_RGB24, SWS_BILINEAR, nullptr, nullptr, nullptr));

            // Ensure reusable intermediate buffer is allocated
            size_t rgb_size = static_cast<size_t>(w) * h * 3;
            if (impl->rgb_temp.size() != rgb_size) {
                impl->rgb_temp.resize(rgb_size);
            }

            // Convert YUV -> 8-bit RGB via sws_scale
            uint8_t* rgb_data[1] = {impl->rgb_temp.data()};
            int rgb_linesize[1] = {w * 3};
            sws_scale(impl->sws_ctx.get(), impl->frame->data, impl->frame->linesize, 0, h, rgb_data,
                      rgb_linesize);

            // Convert 8-bit sRGB -> float sRGB into aligned ImageBuffer
            ImageBuffer buffer(w, h, 3);
            Image view = buffer.view();

            for (size_t i = 0; i < rgb_size; ++i) {
                view.data[i] = impl->rgb_temp[i] / 255.0f;
            }

            return std::move(buffer);
        }

        if (ret != AVERROR(EAGAIN) && ret != AVERROR_EOF) {
            return Unexpected(Error{ErrorCode::IoError, "FFmpeg: Error receiving frame"});
        }

        // 2. Need more data? Read a packet.
        if (av_read_frame(impl->format_ctx.get(), impl->packet.get()) < 0) {
            // Signal EOF to decoder
            avcodec_send_packet(impl->codec_ctx.get(), nullptr);

            // Try one last time to receive (flushing)
            ret = avcodec_receive_frame(impl->codec_ctx.get(), impl->frame.get());
            if (ret == 0) {
                // ... same frame processing logic as above ...
                // To keep it dry, we should probably factor this out, but for now:
                int w = impl->codec_ctx->width;
                int h = impl->codec_ctx->height;
                impl->sws_ctx.reset(sws_getCachedContext(
                    impl->sws_ctx.release(), w, h, impl->codec_ctx->pix_fmt, w, h, AV_PIX_FMT_RGB24,
                    SWS_BILINEAR, nullptr, nullptr, nullptr));
                size_t rgb_size = static_cast<size_t>(w) * h * 3;
                impl->rgb_temp.resize(rgb_size);
                uint8_t* rgb_data[1] = {impl->rgb_temp.data()};
                int rgb_linesize[1] = {w * 3};
                sws_scale(impl->sws_ctx.get(), impl->frame->data, impl->frame->linesize, 0, h,
                          rgb_data, rgb_linesize);
                ImageBuffer buffer(w, h, 3);
                for (size_t i = 0; i < rgb_size; ++i)
                    buffer.view().data[i] = impl->rgb_temp[i] / 255.0f;
                return std::move(buffer);
            }
            return ImageBuffer();  // Real EOF
        }

        if (impl->packet->stream_index == impl->stream_index) {
            avcodec_send_packet(impl->codec_ctx.get(), impl->packet.get());
        }
        av_packet_unref(impl->packet.get());
    }
}

int VideoReader::width() const {
    return m_impl->codec_ctx->width;
}
int VideoReader::height() const {
    return m_impl->codec_ctx->height;
}
double VideoReader::fps() const {
    auto r = m_impl->format_ctx->streams[m_impl->stream_index]->avg_frame_rate;
    return r.den ? (double)r.num / r.den : 0.0;
}

int64_t VideoReader::total_frames() const {
    auto* stream = m_impl->format_ctx->streams[m_impl->stream_index];
    if (stream->nb_frames > 0) return stream->nb_frames;
    if (stream->duration > 0 && stream->time_base.den > 0) {
        return static_cast<int64_t>(stream->duration * av_q2d(stream->time_base) * fps());
    }
    return -1;  // Unknown
}

// --- VideoWriter Implementation ---

class VideoWriter::Impl {
   public:
    AvPtr<AVFormatContext> format_ctx;
    AvPtr<AVCodecContext> codec_ctx;
    AVStream* stream = nullptr;  // Raw pointer (not owned)
    AvPtr<SwsContext> sws_ctx;
    AvPtr<AVFrame> frame;
    AvPtr<AVPacket> packet;

    // Pre-allocated temp buffer for float->uint8 conversion (reused per frame)
    std::vector<uint8_t> rgb24_temp;
    int64_t frame_count = 0;

    Impl() : frame(av_frame_alloc()), packet(av_packet_alloc()) {}
};

VideoWriter::VideoWriter() : m_impl(std::make_unique<Impl>()) {}
VideoWriter::~VideoWriter() {
    if (m_impl && m_impl->codec_ctx) {
        // Flush encoder
        if (avcodec_send_frame(m_impl->codec_ctx.get(), nullptr) >= 0) {
            while (avcodec_receive_packet(m_impl->codec_ctx.get(), m_impl->packet.get()) == 0) {
                av_packet_rescale_ts(m_impl->packet.get(), m_impl->codec_ctx->time_base,
                                     m_impl->format_ctx->streams[0]->time_base);
                m_impl->packet->stream_index = 0;
                av_interleaved_write_frame(m_impl->format_ctx.get(), m_impl->packet.get());
                av_packet_unref(m_impl->packet.get());
            }
        }

        av_write_trailer(m_impl->format_ctx.get());
        if (!(m_impl->format_ctx->oformat->flags & AVFMT_NOFILE)) {
            avio_closep(&m_impl->format_ctx->pb);
        }
    }
}

Result<std::unique_ptr<VideoWriter>> VideoWriter::open(const std::filesystem::path& path, int width,
                                                       int height, double fps,
                                                       const std::string& codec_name) {
    auto writer = std::unique_ptr<VideoWriter>(new VideoWriter());
    auto* impl = writer->m_impl.get();

    AVFormatContext* fmt_ptr = nullptr;
    if (avformat_alloc_output_context2(&fmt_ptr, nullptr, nullptr, path.string().c_str()) < 0) {
        return Unexpected(Error{ErrorCode::IoError, "FFmpeg: Could not create output context"});
    }
    impl->format_ctx.reset(fmt_ptr);

    const AVCodec* codec = avcodec_find_encoder_by_name(codec_name.c_str());
    if (!codec) {
        return Unexpected(Error{ErrorCode::IoError, "FFmpeg: Codec not found: " + codec_name});
    }

    AVStream* st = avformat_new_stream(impl->format_ctx.get(), nullptr);
    if (!st) return Unexpected(Error{ErrorCode::IoError, "FFmpeg: Could not create stream"});

    impl->codec_ctx.reset(avcodec_alloc_context3(codec));
    impl->codec_ctx->width = width;
    impl->codec_ctx->height = height;
    impl->codec_ctx->time_base = av_inv_q(av_d2q(fps, 60000));
    impl->codec_ctx->pix_fmt = AV_PIX_FMT_YUV420P;

    st->time_base = impl->codec_ctx->time_base;
    st->avg_frame_rate = av_d2q(fps, 60000);

    if (impl->format_ctx->oformat->flags & AVFMT_GLOBALHEADER) {
        impl->codec_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }

    if (avcodec_open2(impl->codec_ctx.get(), codec, nullptr) < 0) {
        return Unexpected(Error{ErrorCode::IoError, "FFmpeg: Could not open encoder"});
    }

    avcodec_parameters_from_context(st->codecpar, impl->codec_ctx.get());

    if (!(impl->format_ctx->oformat->flags & AVFMT_NOFILE)) {
        if (avio_open(&impl->format_ctx->pb, path.string().c_str(), AVIO_FLAG_WRITE) < 0) {
            return Unexpected(Error{ErrorCode::IoError, "FFmpeg: Could not open output file"});
        }
    }

    if (avformat_write_header(impl->format_ctx.get(), nullptr) < 0) {
        return Unexpected(Error{ErrorCode::IoError, "FFmpeg: Could not write header"});
    }

    // Pre-allocate temp buffer for the writer's lifetime
    impl->rgb24_temp.resize(static_cast<size_t>(width) * height * 3);

    return std::move(writer);
}

Result<void> VideoWriter::write_frame(const Image& image) {
    auto* impl = m_impl.get();

    // Resize temp buffer if needed (should match open dimensions, but safety first)
    size_t required_size = static_cast<size_t>(image.width) * image.height * 3;
    if (impl->rgb24_temp.size() < required_size) {
        impl->rgb24_temp.resize(required_size);
    }

    impl->sws_ctx.reset(sws_getCachedContext(impl->sws_ctx.release(), image.width, image.height,
                                             AV_PIX_FMT_RGB24, impl->codec_ctx->width,
                                             impl->codec_ctx->height, impl->codec_ctx->pix_fmt,
                                             SWS_BILINEAR, nullptr, nullptr, nullptr));

    impl->frame->format = impl->codec_ctx->pix_fmt;
    impl->frame->width = impl->codec_ctx->width;
    impl->frame->height = impl->codec_ctx->height;
    if (av_frame_get_buffer(impl->frame.get(), 0) < 0) {
        return Unexpected(Error{ErrorCode::IoError, "FFmpeg: Could not allocate frame buffer"});
    }

    // Convert float sRGB -> 8-bit sRGB into pre-allocated buffer
    int channels = image.channels;
    for (int y = 0; y < image.height; ++y) {
        for (int x = 0; x < image.width; ++x) {
            size_t src_idx = (y * image.width + x) * channels;
            size_t dst_idx = (y * image.width + x) * 3;
            for (int c = 0; c < 3; ++c) {
                // If the input has less than 3 channels, use the first channel for all (grayscale)
                float val = image.data[src_idx + std::min(c, channels - 1)];
                impl->rgb24_temp[dst_idx + c] =
                    static_cast<uint8_t>(std::clamp(static_cast<int>(val * 255.0f + 0.5f), 0, 255));
            }
        }
    }

    const uint8_t* src_data[1] = {impl->rgb24_temp.data()};
    int src_linesize[1] = {image.width * 3};
    sws_scale(impl->sws_ctx.get(), src_data, src_linesize, 0, image.height, impl->frame->data,
              impl->frame->linesize);

    impl->frame->pts = impl->frame_count++;

    if (avcodec_send_frame(impl->codec_ctx.get(), impl->frame.get()) >= 0) {
        while (avcodec_receive_packet(impl->codec_ctx.get(), impl->packet.get()) == 0) {
            av_packet_rescale_ts(impl->packet.get(), impl->codec_ctx->time_base,
                                 impl->format_ctx->streams[0]->time_base);
            impl->packet->stream_index = 0;
            av_interleaved_write_frame(impl->format_ctx.get(), impl->packet.get());
            av_packet_unref(impl->packet.get());
        }
    }

    return {};
}

}  // namespace corridorkey
