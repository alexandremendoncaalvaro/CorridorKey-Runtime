#include <corridorkey/frame_io.hpp>
#include "exr_io.hpp"
#include <iostream>

namespace corridorkey {

// TODO: Include private headers for PNG, Video
// #include "png_io.hpp"

Result<ImageBuffer> FrameIO::read_frame(const std::filesystem::path& path) {
    auto ext = path.extension().string();
    
    // Dispatch based on extension
    if (ext == ".exr") {
        return read_exr(path);
    } else if (ext == ".png" || ext == ".jpg" || ext == ".jpeg") {
        // return read_stb(path);
        return unexpected(Error{ ErrorCode::IoError, "PNG/JPG reading not yet implemented" });
    }

    return unexpected(Error{ ErrorCode::IoError, "Unsupported file format: " + ext });
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
    try {
        // Define paths
        auto matte_dir = base_dir / "Matte";
        auto fg_dir = base_dir / "FG";
        auto proc_dir = base_dir / "Processed";
        auto comp_dir = base_dir / "Comp";

        // Create directories
        std::filesystem::create_directories(matte_dir);
        std::filesystem::create_directories(fg_dir);
        std::filesystem::create_directories(proc_dir);
        std::filesystem::create_directories(comp_dir);

        // Change extension to .exr for main outputs
        std::filesystem::path stem = std::filesystem::path(filename).stem();
        
        // 1. Save Alpha Matte (EXR)
        auto matte_res = write_frame(matte_dir / stem.replace_extension(".exr"), result.alpha.const_view());
        if (!matte_res) return matte_res;

        // 2. Save Foreground (EXR)
        auto fg_res = write_frame(fg_dir / stem.replace_extension(".exr"), result.foreground.const_view());
        if (!fg_res) return fg_res;

        // 3. Save Processed (EXR)
        auto proc_res = write_frame(proc_dir / stem.replace_extension(".exr"), result.composite.const_view());
        if (!proc_res) return proc_res;

        // 4. Save Preview Comp (PNG)
        // Note: For Comp, we'll need to implement PNG writing and linear->srgb conversion
        auto comp_path = comp_dir / stem.replace_extension(".png");
        // write_frame(comp_path, result.composite); // To be implemented with PNG and sRGB conversion

        return {};
    } catch (const std::exception& e) {
        return unexpected(Error{ ErrorCode::IoError, std::string("Failed to save result structure: ") + e.what() });
    }
}

} // namespace corridorkey
