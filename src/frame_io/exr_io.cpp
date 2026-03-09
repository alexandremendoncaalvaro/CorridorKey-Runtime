#include "exr_io.hpp"
#include <OpenEXR/ImfRgbaFile.h>
#include <OpenEXR/ImfArray.h>
#include <OpenEXR/ImfHeader.h>
#include <iostream>

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

        // Convert OpenEXR Half RGBA to Aligned Float RGBA
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                const Imf::Rgba& p = pixels[y][x];
                int idx = (y * width + x) * 4;
                view.data[idx + 0] = (float)p.r;
                view.data[idx + 1] = (float)p.g;
                view.data[idx + 2] = (float)p.b;
                view.data[idx + 3] = (float)p.a;
            }
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

        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                int idx = (y * width + x) * image.channels;
                float r = image.data[idx + 0];
                float g = image.data[idx + 1];
                float b = image.data[idx + 2];
                float a = (image.channels == 4) ? image.data[idx + 3] : 1.0f;
                pixels[y][x] = Imf::Rgba(r, g, b, a);
            }
        }

        file.setFrameBuffer(&pixels[0][0], 1, width);
        file.writePixels(height);

        return {};
    } catch (const std::exception& e) {
        return unexpected(Error{ ErrorCode::IoError, std::string("Failed to write EXR: ") + e.what() });
    }
}

} // namespace corridorkey
