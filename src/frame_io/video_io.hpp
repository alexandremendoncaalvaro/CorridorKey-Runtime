#pragma once

#include <corridorkey/types.hpp>
#include <filesystem>
#include <memory>
#include <optional>
#include <vector>

namespace corridorkey {

/**
 * @brief Captured input video frame characteristics relevant to encoding.
 */
struct VideoFrameFormat {
    int bits_per_component = 8;
    bool is_float = false;
};

/**
 * @brief Decoded video frame with optional timestamp in microseconds.
 */
struct VideoFrame {
    ImageBuffer buffer;
    std::optional<int64_t> pts_us;
};

/**
 * @brief Time base for timestamp conversion.
 */
struct VideoTimeBase {
    int numerator = 0;
    int denominator = 0;

    [[nodiscard]] bool is_valid() const {
        return numerator > 0 && denominator > 0;
    }
};

/**
 * @brief Resolved video output plan for a specific container and encoder.
 */
struct VideoOutputPlan {
    std::filesystem::path output_path;
    std::string container_extension;
    std::string encoder_name;
    VideoOutputMode mode = VideoOutputMode::Lossless;
    bool lossless = false;
    int bits_per_component = 8;
    int pixel_format = -1;
};

/**
 * @brief Summary of video output support for diagnostics and release gating.
 */
struct VideoOutputSupport {
    VideoOutputMode default_mode = VideoOutputMode::Lossless;
    std::string default_container;
    std::string default_encoder;
    bool lossless_available = false;
    std::string lossless_unavailable_reason;
};

/**
 * @brief Simple RAII reader for video files using FFmpeg 8.x.
 */
class VideoReader {
   public:
    static Result<std::unique_ptr<VideoReader>> open(const std::filesystem::path& path);
    ~VideoReader();

    /**
     * @brief Read the next frame from the video.
     * Returns a VideoFrame with an empty buffer (view().empty() == true) at EOF.
     */
    Result<VideoFrame> read_next_frame();

    int width() const;
    int height() const;
    double fps() const;
    std::optional<VideoTimeBase> time_base() const;
    int64_t total_frames() const;
    VideoFrameFormat format() const;

   private:
    VideoReader();
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

/**
 * @brief Simple RAII writer for video files.
 */
class VideoWriter {
   public:
    static Result<std::unique_ptr<VideoWriter>> open(const std::filesystem::path& path, int width,
                                                     int height, double fps,
                                                     const VideoFrameFormat& input_format,
                                                     const VideoOutputOptions& options = {},
                                                     const std::string& codec_name = "",
                                                     std::optional<VideoTimeBase> time_base =
                                                         std::nullopt);
    static Result<std::unique_ptr<VideoWriter>> open(const std::filesystem::path& path, int width,
                                                     int height, double fps,
                                                     const std::string& codec_name = "",
                                                     std::optional<VideoTimeBase> time_base =
                                                         std::nullopt);
    ~VideoWriter();

    Result<void> write_frame(const Image& image);
    Result<void> write_frame(const Image& image, std::optional<int64_t> pts_us);
    Result<void> finalize();

   private:
    VideoWriter();
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

/**
 * @brief Detect whether the current FFmpeg build can use VideoToolbox H.264 encoding.
 */
bool is_videotoolbox_available();

/**
 * @brief Resolve a full output plan based on container, encoder availability, and policy.
 */
Result<VideoOutputPlan> resolve_video_output_plan(const std::filesystem::path& output_path,
                                                  const VideoOutputOptions& options,
                                                  const VideoFrameFormat& input_format);

/**
 * @brief Resolve an output path when the container is implicit.
 */
Result<std::filesystem::path> resolve_video_output_path(const std::filesystem::path& output_path,
                                                        const VideoOutputOptions& options,
                                                        const VideoFrameFormat& input_format);

/**
 * @brief Inspect default video output support and lossless availability.
 */
VideoOutputSupport inspect_video_output_support(const VideoFrameFormat& input_format);

/**
 * @brief Select the default output encoder for a given path.
 */
std::string default_video_encoder_for_path(const std::filesystem::path& path);

/**
 * @brief Enumerate encoder candidates for a given output path in priority order.
 */
std::vector<std::string> available_video_encoders_for_path(const std::filesystem::path& path);

}  // namespace corridorkey
