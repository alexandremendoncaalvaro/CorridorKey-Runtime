#pragma once

#include <corridorkey/types.hpp>
#include <filesystem>
#include <memory>
#include <vector>

namespace corridorkey {

/**
 * @brief Simple RAII reader for video files using FFmpeg 8.x.
 */
class VideoReader {
   public:
    static Result<std::unique_ptr<VideoReader>> open(const std::filesystem::path& path);
    ~VideoReader();

    /**
     * @brief Read the next frame from the video.
     * Returns an empty ImageBuffer (view().empty() == true) at EOF.
     */
    Result<ImageBuffer> read_next_frame();

    int width() const;
    int height() const;
    double fps() const;
    int64_t total_frames() const;

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
                                                     const std::string& codec_name = "");
    ~VideoWriter();

    Result<void> write_frame(const Image& image);

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
 * @brief Select the default output encoder for a given path.
 */
std::string default_video_encoder_for_path(const std::filesystem::path& path);

/**
 * @brief Enumerate encoder candidates for a given output path in priority order.
 */
std::vector<std::string> available_video_encoders_for_path(const std::filesystem::path& path);

}  // namespace corridorkey
