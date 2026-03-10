#pragma once

#include <corridorkey/types.hpp>
#include <filesystem>

namespace corridorkey {

/**
 * @brief Private helper functions for PNG/JPG I/O using stb_image.
 */
Result<ImageBuffer> read_stb(const std::filesystem::path& path);
Result<void> write_png(const std::filesystem::path& path, const Image& image);

}  // namespace corridorkey
