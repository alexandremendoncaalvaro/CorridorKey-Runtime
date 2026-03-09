#pragma once

#include <corridorkey/api_export.hpp>
#include <corridorkey/types.hpp>
#include <filesystem>

namespace corridorkey::frame_io {

/**
 * @brief Read a single frame from disk.
 * Returns an owned ImageBuffer.
 */
CORRIDORKEY_API Result<ImageBuffer> read_frame(const std::filesystem::path& path);

/**
 * @brief Write a single frame to disk.
 * Takes a non-owning Image view.
 */
CORRIDORKEY_API Result<void> write_frame(const std::filesystem::path& path, const Image& image);

/**
 * @brief Save a full result (Alpha, FG, Processed, Comp) following the VFX-standard directory structure.
 */
CORRIDORKEY_API Result<void> save_result(
    const std::filesystem::path& base_dir,
    const std::string& filename,
    const FrameResult& result
);

} // namespace corridorkey::frame_io
