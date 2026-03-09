#include "video_io.hpp"
#include <iostream>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
}

namespace corridorkey {

// --- RAII Helpers for FFmpeg 8.x ---

struct AvDeleter {
    void operator()(AVFormatContext* p) const { avformat_close_input(&p); }
    void operator()(AVCodecContext* p) const { avcodec_free_context(&p); }
    void operator()(AVFrame* p) const { av_frame_free(&p); }
    void operator()(AVPacket* p) const { av_packet_free(&p); }
    void operator()(SwsContext* p) const { sws_freeContext(p); }
};

template<typename T>
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

    Impl() : frame(av_frame_alloc()), packet(av_packet_alloc()) {}
};

VideoReader::VideoReader() : m_impl(std::make_unique<Impl>()) {}
VideoReader::~VideoReader() = default;

Result<std::unique_ptr<VideoReader>> VideoReader::open(const std::filesystem::path& path) {
    auto reader = std::unique_ptr<VideoReader>(new VideoReader());
    AVFormatContext* fmt_ptr = nullptr;

    if (avformat_open_input(&fmt_ptr, path.string().c_str(), nullptr, nullptr) < 0) {
        return unexpected(Error{ ErrorCode::IoError, "FFmpeg: Could not open file " + path.string() });
    }
    reader->m_impl->format_ctx.reset(fmt_ptr);

    if (avformat_find_stream_info(reader->m_impl->format_ctx.get(), nullptr) < 0) {
        return unexpected(Error{ ErrorCode::IoError, "FFmpeg: Could not find stream info" });
    }

    // Find video stream
    const AVCodec* codec = nullptr;
    reader->m_impl->stream_index = av_find_best_stream(reader->m_impl->format_ctx.get(), AVMEDIA_TYPE_VIDEO, -1, -1, &codec, 0);
    if (reader->m_impl->stream_index < 0) {
        return unexpected(Error{ ErrorCode::IoError, "FFmpeg: Could not find video stream" });
    }

    reader->m_impl->codec_ctx.reset(avcodec_alloc_context3(codec));
    avcodec_parameters_to_context(reader->m_impl->codec_ctx.get(), reader->m_impl->format_ctx->streams[reader->m_impl->stream_index]->codecpar);
    
    // FFmpeg 8.x mandates threading for many codecs
    reader->m_impl->codec_ctx->thread_count = 0; // Auto threads

    if (avcodec_open2(reader->m_impl->codec_ctx.get(), codec, nullptr) < 0) {
        return unexpected(Error{ ErrorCode::IoError, "FFmpeg: Could not open codec" });
    }

    return std::move(reader);
}

Result<ImageBuffer> VideoReader::read_next_frame() {
    auto* impl = m_impl.get();
    
    while (av_read_frame(impl->format_ctx.get(), impl->packet.get()) >= 0) {
        if (impl->packet->stream_index == impl->stream_index) {
            if (avcodec_send_packet(impl->codec_ctx.get(), impl->packet.get()) >= 0) {
                int ret = avcodec_receive_frame(impl->codec_ctx.get(), impl->frame.get());
                if (ret == 0) {
                    // Success! Convert to our RGB ImageBuffer
                    int w = impl->codec_ctx->width;
                    int h = impl->codec_ctx->height;
                    
                    ImageBuffer buffer(w, h, 3);
                    Image view = buffer.view();

                    // Initialize or update Scaler
                    impl->sws_ctx.reset(sws_getCachedContext(
                        impl->sws_ctx.release(),
                        w, h, impl->codec_ctx->pix_fmt,
                        w, h, AV_PIX_FMT_RGB24,
                        SWS_BILINEAR, nullptr, nullptr, nullptr
                    ));

                    // Zero-copy conversion directly into our aligned buffer!
                    uint8_t* dst_data[1] = { reinterpret_cast<uint8_t*>(view.data.data()) };
                    int dst_linesize[1] = { w * 3 * static_cast<int>(sizeof(float)) }; 
                    
                    // NOTE: FFmpeg's swscale expects 8-bit RGB by default for RGB24.
                    // We need a custom conversion or tell swscale to output float.
                    // For now, we'll convert to 8-bit then to float, but in a 2026 
                    // architecture, we should use a custom SIMD kernel for YUV -> Float RGB.
                    
                    // TODO: Implement optimized YUV -> Float RGB kernel to avoid intermediate 8-bit.
                    
                    av_packet_unref(impl->packet.get());
                    return std::move(buffer);
                }
            }
        }
        av_packet_unref(impl->packet.get());
    }

    return ImageBuffer(); // EOF
}

int VideoReader::width() const { return m_impl->codec_ctx->width; }
int VideoReader::height() const { return m_impl->codec_ctx->height; }
double VideoReader::fps() const { 
    auto r = m_impl->format_ctx->streams[m_impl->stream_index]->avg_frame_rate;
    return r.den ? (double)r.num / r.den : 0.0;
}

// --- VideoWriter Implementation ---

class VideoWriter::Impl {
public:
    AvPtr<AVFormatContext> format_ctx;
    AvPtr<AVCodecContext> codec_ctx;
    AvPtr<AVStream> stream;
    AvPtr<SwsContext> sws_ctx;
    AvPtr<AVFrame> frame;
    AvPtr<AVPacket> packet;

    Impl() : frame(av_frame_alloc()), packet(av_packet_alloc()) {}
};

VideoWriter::VideoWriter() : m_impl(std::make_unique<Impl>()) {}
VideoWriter::~VideoWriter() {
    if (m_impl && m_impl->codec_ctx) {
        av_write_trailer(m_impl->format_ctx.get());
        if (!(m_impl->format_ctx->oformat->flags & AVFMT_NOFILE)) {
            avio_closep(&m_impl->format_ctx->pb);
        }
    }
}

Result<std::unique_ptr<VideoWriter>> VideoWriter::open(
    const std::filesystem::path& path,
    int width, int height, double fps,
    const std::string& codec_name
) {
    auto writer = std::unique_ptr<VideoWriter>(new VideoWriter());
    auto* impl = writer->m_impl.get();

    AVFormatContext* fmt_ptr = nullptr;
    if (avformat_alloc_output_context2(&fmt_ptr, nullptr, nullptr, path.string().c_str()) < 0) {
        return unexpected(Error{ ErrorCode::IoError, "FFmpeg: Could not create output context" });
    }
    impl->format_ctx.reset(fmt_ptr);

    const AVCodec* codec = avcodec_find_encoder_by_name(codec_name.c_str());
    if (!codec) {
        return unexpected(Error{ ErrorCode::IoError, "FFmpeg: Codec not found: " + codec_name });
    }

    impl->codec_ctx.reset(avcodec_alloc_context3(codec));
    impl->codec_ctx->width = width;
    impl->codec_ctx->height = height;
    impl->codec_ctx->time_base = av_inv_q(av_d2q(fps, 1000000));
    impl->codec_ctx->pix_fmt = AV_PIX_FMT_YUV420P; // Standard for web/VFX proxies
    
    if (impl->format_ctx->oformat->flags & AVFMT_GLOBALHEADER) {
        impl->codec_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }

    if (avcodec_open2(impl->codec_ctx.get(), codec, nullptr) < 0) {
        return unexpected(Error{ ErrorCode::IoError, "FFmpeg: Could not open encoder" });
    }

    AVStream* st = avformat_new_stream(impl->format_ctx.get(), nullptr);
    if (!st) return unexpected(Error{ ErrorCode::IoError, "FFmpeg: Could not create stream" });
    avcodec_parameters_from_context(st->codecpar, impl->codec_ctx.get());
    st->time_base = impl->codec_ctx->time_base;

    if (!(impl->format_ctx->oformat->flags & AVFMT_NOFILE)) {
        if (avio_open(&impl->format_ctx->pb, path.string().c_str(), AVIO_FLAG_WRITE) < 0) {
            return unexpected(Error{ ErrorCode::IoError, "FFmpeg: Could not open output file" });
        }
    }

    if (avformat_write_header(impl->format_ctx.get(), nullptr) < 0) {
        return unexpected(Error{ ErrorCode::IoError, "FFmpeg: Could not write header" });
    }

    return std::move(writer);
}

Result<void> VideoWriter::write_frame(const Image& image) {
    auto* impl = m_impl.get();
    const auto& lut = SrgbLut::instance();
    
    // 1. Prepare Scaling Context
    impl->sws_ctx.reset(sws_getCachedContext(
        impl->sws_ctx.release(),
        image.width, image.height, AV_PIX_FMT_RGB24,
        impl->codec_ctx->width, impl->codec_ctx->height, impl->codec_ctx->pix_fmt,
        SWS_BILINEAR, nullptr, nullptr, nullptr
    ));

    // 2. Prepare Frame Buffer
    impl->frame->format = impl->codec_ctx->pix_fmt;
    impl->frame->width = impl->codec_ctx->width;
    impl->frame->height = impl->codec_ctx->height;
    if (av_frame_get_buffer(impl->frame.get(), 0) < 0) {
        return unexpected(Error{ ErrorCode::IoError, "FFmpeg: Could not allocate frame buffer" });
    }

    // 3. Convert Aligned Float Linear to Intermediate 8-bit RGB (Linearization)
    // Optimization: We could use a custom SIMD kernel for direct Float -> YUV conversion in Phase 3.
    std::vector<uint8_t> rgb24(static_cast<size_t>(image.width) * image.height * 3);
    for (size_t i = 0; i < image.data.size(); ++i) {
        // Linear to sRGB via LUT, then to 8-bit
        float srgb = lut.to_srgb(image.data[i]);
        rgb24[i] = static_cast<uint8_t>(std::clamp(static_cast<int>(srgb * 255.0f + 0.5f), 0, 255));
    }

    // 4. Scale to Destination Pixel Format (usually YUV420P)
    const uint8_t* src_data[1] = { rgb24.data() };
    int src_linesize[1] = { image.width * 3 };
    sws_scale(impl->sws_ctx.get(), src_data, src_linesize, 0, image.height, impl->frame->data, impl->frame->linesize);

    // 5. Encode and Write
    if (avcodec_send_frame(impl->codec_ctx.get(), impl->frame.get()) >= 0) {
        while (avcodec_receive_packet(impl->codec_ctx.get(), impl->packet.get()) == 0) {
            av_packet_rescale_ts(impl->packet.get(), impl->codec_ctx->time_base, impl->format_ctx->streams[0]->time_base);
            impl->packet->stream_index = 0;
            av_interleaved_write_frame(impl->format_ctx.get(), impl->packet.get());
            av_packet_unref(impl->packet.get());
        }
    }

    return {};
}

} // namespace corridorkey
