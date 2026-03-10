#include <corridorkey/frame_io.hpp>

#include "exr_io.hpp"
#include "png_io.hpp"

namespace corridorkey::frame_io {

Result<ImageBuffer> read_frame(const std::filesystem::path& path) {
    auto ext = path.extension().string();

    if (ext == ".exr") {
        return read_exr(path);
    } else if (ext == ".png" || ext == ".jpg" || ext == ".jpeg") {
        return read_stb(path);
    }

    return unexpected(Error{ErrorCode::IoError, "Unsupported file format: " + ext});
}

Result<void> write_frame(const std::filesystem::path& path, const Image& image) {
    auto ext = path.extension().string();

    if (ext == ".exr") {
        return write_exr(path, image);
    } else if (ext == ".png") {
        return write_png(path, image);
    }

    return unexpected(Error{ErrorCode::IoError, "Unsupported output format: " + ext});
}

Result<void> save_result(const std::filesystem::path& base_dir, const std::string& filename,
                         const FrameResult& result) {
    try {
        auto matte_dir = base_dir / "Matte";
        auto fg_dir = base_dir / "FG";
        auto proc_dir = base_dir / "Processed";
        auto comp_dir = base_dir / "Comp";

        std::filesystem::create_directories(matte_dir);
        std::filesystem::create_directories(fg_dir);
        std::filesystem::create_directories(proc_dir);
        std::filesystem::create_directories(comp_dir);

        auto base_name = std::filesystem::path(filename).stem().string();

        // 1. Save Alpha Matte (EXR)
        auto matte_res = write_frame(matte_dir / (base_name + ".exr"), result.alpha.const_view());
        if (!matte_res) return matte_res;

        // 2. Save Foreground (EXR)
        auto fg_res = write_frame(fg_dir / (base_name + ".exr"), result.foreground.const_view());
        if (!fg_res) return fg_res;

        // 3. Save Processed / Premultiplied RGBA (EXR)
        auto proc_res = write_frame(proc_dir / (base_name + ".exr"), result.processed.const_view());
        if (!proc_res) return proc_res;

        // 4. Save Preview Comp (PNG)
        auto comp_res = write_frame(comp_dir / (base_name + ".png"), result.composite.const_view());
        if (!comp_res) return comp_res;

        return {};
    } catch (const std::exception& e) {
        return unexpected(
            Error{ErrorCode::IoError, std::string("Failed to save result structure: ") + e.what()});
    }
}

}  // namespace corridorkey::frame_io
