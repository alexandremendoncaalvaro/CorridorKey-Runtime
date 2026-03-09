#pragma once

#include <corridorkey/types.hpp>
#include <filesystem>
#include <memory>

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
    static Result<std::unique_ptr<VideoWriter>> open(
        const std::filesystem::path& path,
        int width, int height, double fps,
        const std::string& codec_name = "mpeg4"
    );
    ~VideoWriter();

    Result<void> write_frame(const Image& image);

private:
    VideoWriter();
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace corridorkey
