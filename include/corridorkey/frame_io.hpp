#pragma once

#include <corridorkey/api_export.hpp>
#include <corridorkey/types.hpp>
#include <filesystem>
#include <memory>

namespace corridorkey {

/**
 * @brief Interface for image and video I/O operations.
 */
class CORRIDORKEY_API FrameIO {
public:
    virtual ~FrameIO() = default;

    /**
     * @brief Read a single frame from disk.
     * Returns an owned ImageBuffer.
     */
    static Result<ImageBuffer> read_frame(const std::filesystem::path& path);

    /**
     * @brief Write a single frame to disk.
     * Takes a non-owning Image view.
     */
    static Result<void> write_frame(const std::filesystem::path& path, const Image& image);

    /**
     * @brief Save a full result (Alpha, FG, Processed, Comp) following the VFX-standard directory structure.
     */
    static Result<void> save_result(
        const std::filesystem::path& base_dir,
        const std::string& filename,
        const FrameResult& result
    );
};

} // namespace corridorkey
