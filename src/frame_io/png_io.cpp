#include "png_io.hpp"
#include <src/post_process/color_utils.hpp>

#define STB_IMAGE_IMPLEMENTATION
#include <vendor/stb_image.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <vendor/stb_image_write.h>

#include <iostream>
#include <vector>
#include <cmath>

namespace corridorkey {

Result<ImageBuffer> read_stb(const std::filesystem::path& path) {
    int width, height, channels;
    // std::filesystem::path::string() is fine here as stb handles UTF-8 on Windows internally 
    // or via wide-char wrappers if needed.
    unsigned char* data = stbi_load(path.string().c_str(), &width, &height, &channels, 0);

    if (!data) {
        return unexpected(Error{ ErrorCode::IoError, "STB failed to load image: " + std::string(stbi_failure_reason()) });
    }

    ImageBuffer buffer(width, height, channels);
    Image view = buffer.view();

    // Convert 8-bit sRGB to Aligned Float Linear
    // Optimization: sRGB LUT could be used here too if we read many PNGs
    for (int i = 0; i < width * height * channels; ++i) {
        float srgb = data[i] / 255.0f;
        // Piecewise sRGB to Linear
        view.data[i] = (srgb <= 0.04045f) ? (srgb / 12.92f) : std::pow((srgb + 0.055f) / 1.055f, 2.4f);
    }

    stbi_image_free(data);
    return std::move(buffer);
}

Result<void> write_png(const std::filesystem::path& path, const Image& image) {
    if (image.empty()) {
        return unexpected(Error{ ErrorCode::InvalidParameters, "Cannot write empty image to PNG" });
    }

    int width = image.width;
    int height = image.height;
    int channels = image.channels;

    std::vector<unsigned char> srgb_data(width * height * channels);

    // Convert Aligned Float Linear to 8-bit sRGB
    for (int i = 0; i < width * height * channels; ++i) {
        float lin = image.data[i];
        float srgb = (lin <= 0.0031308f) ? (lin * 12.92f) : (1.055f * std::pow(lin, 1.0f / 2.4f) - 0.055f);
        
        // Clamp and convert to 8-bit
        int val = static_cast<int>(srgb * 255.0f + 0.5f);
        srgb_data[i] = static_cast<unsigned char>(std::clamp(val, 0, 255));
    }

    int success = stbi_write_png(path.string().c_str(), width, height, channels, srgb_data.data(), width * channels);

    if (!success) {
        return unexpected(Error{ ErrorCode::IoError, "STB failed to write PNG" });
    }

    return {};
}

} // namespace corridorkey
