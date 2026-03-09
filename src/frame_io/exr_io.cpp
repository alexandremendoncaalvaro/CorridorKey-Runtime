#include "exr_io.hpp"
#include <OpenEXR/ImfRgbaFile.h>
#include <OpenEXR/ImfArray.h>
#include <OpenEXR/ImfHeader.h>

namespace corridorkey {

Result<ImageBuffer> read_exr(const std::filesystem::path& path) {
    try {
        Imf::RgbaInputFile file(path.string().c_str());
        Imath::Box2i dw = file.dataWindow();

        int width = dw.max.x - dw.min.x + 1;
        int height = dw.max.y - dw.min.y + 1;

        // Allocate aligned storage for VFX processing
        ImageBuffer buffer(width, height, 4);
        Image view = buffer.view();

        Imf::Array2D<Imf::Rgba> pixels(height, width);
        file.setFrameBuffer(&pixels[0][0] - dw.min.x - dw.min.y * width, 1, width);
        file.readPixels(dw.min.y, dw.max.y);

        // Convert OpenEXR Half RGBA to Aligned Float RGBA (Linearized)
        size_t total_pixels = static_cast<size_t>(width) * height;
        const Imf::Rgba* src_ptr = &pixels[0][0];
        
        for (size_t i = 0; i < total_pixels; ++i) {
            const Imf::Rgba& p = src_ptr[i];
            float* dst = &view.data[i * 4];
            dst[0] = static_cast<float>(p.r);
            dst[1] = static_cast<float>(p.g);
            dst[2] = static_cast<float>(p.b);
            dst[3] = static_cast<float>(p.a);
        }

        return std::move(buffer);
    } catch (const std::exception& e) {
        return unexpected(Error{ ErrorCode::IoError, std::string("Failed to read EXR: ") + e.what() });
    }
}

Result<void> write_exr(const std::filesystem::path& path, const Image& image) {
    if (image.channels < 3) {
        return unexpected(Error{ ErrorCode::InvalidParameters, "EXR write requires at least 3 channels (RGB)" });
    }

    try {
        int width = image.width;
        int height = image.height;

        Imf::RgbaOutputFile file(path.string().c_str(), width, height, Imf::WRITE_RGBA);
        Imf::Array2D<Imf::Rgba> pixels(height, width);
        Imf::Rgba* dst_ptr = &pixels[0][0];

        // Linearized conversion from Aligned Float RGB(A) to OpenEXR Rgba
        size_t total_pixels = static_cast<size_t>(width) * height;
        bool has_alpha = (image.channels == 4);

        for (size_t i = 0; i < total_pixels; ++i) {
            const float* src = &image.data[i * image.channels];
            dst_ptr[i] = Imf::Rgba(src[0], src[1], src[2], has_alpha ? src[3] : 1.0f);
        }

        file.setFrameBuffer(dst_ptr, 1, width);
        file.writePixels(height);

        return {};
    } catch (const std::exception& e) {
        return unexpected(Error{ ErrorCode::IoError, std::string("Failed to write EXR: ") + e.what() });
    }
}

} // namespace corridorkey
