#include <corridorkey/frame_io.hpp>
#include "exr_io.hpp"
#include <iostream>

namespace corridorkey {

// TODO: Include private headers for PNG, Video
// #include "png_io.hpp"

Result<Image> FrameIO::read_frame(const std::filesystem::path& path) {
    auto ext = path.extension().string();
    
    // Dispatch based on extension
    if (ext == ".exr") {
        return read_exr(path);
    } else if (ext == ".png" || ext == ".jpg" || ext == ".jpeg") {
        // return read_stb(path);
        return std::unexpected(Error{ ErrorCode::IoError, "PNG/JPG reading not yet implemented" });
    }

    return std::unexpected(Error{ ErrorCode::IoError, "Unsupported file format: " + ext });
}

Result<void> FrameIO::write_frame(const std::filesystem::path& path, const Image& image) {
    auto ext = path.extension().string();

    if (ext == ".exr") {
        return write_exr(path, image);
    } else if (ext == ".png") {
        // return write_png(path, image);
        return std::unexpected(Error{ ErrorCode::IoError, "PNG writing not yet implemented" });
    }

    return std::unexpected(Error{ ErrorCode::IoError, "Unsupported output format: " + ext });
}

Result<void> FrameIO::save_result(
    const std::filesystem::path& base_dir,
    const std::string& filename,
    const FrameResult& result
) {
    (void)base_dir;
    (void)filename;
    (void)result;

    // TODO: Create the standard VFX structure (Matte/, FG/, Processed/, Comp/)
    // and write the corresponding files.
    return {};
}

} // namespace corridorkey
