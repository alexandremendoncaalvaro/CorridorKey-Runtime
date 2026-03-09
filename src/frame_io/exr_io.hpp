#pragma once

#include <corridorkey/types.hpp>
#include <filesystem>

namespace corridorkey {

/**
 * @brief Private helper functions for EXR I/O using OpenEXR.
 */
Result<Image> read_exr(const std::filesystem::path& path);
Result<void> write_exr(const std::filesystem::path& path, const Image& image);

} // namespace corridorkey
