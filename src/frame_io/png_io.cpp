#include "png_io.hpp"
#include "core/perf_utils.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include <vendor/stb_image.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <vendor/stb_image_write.h>

#include <vector>
#include <algorithm>

namespace corridorkey {

Result<ImageBuffer> read_stb(const std::filesystem::path& path) {
    int width, height, channels;
    unsigned char* data = stbi_load(path.string().c_str(), &width, &height, &channels, 0);

    if (!data) {
        return unexpected(Error{ ErrorCode::IoError, "STB failed to load image: " + std::string(stbi_failure_reason()) });
    }

    ImageBuffer buffer(width, height, channels);
    Image view = buffer.view();
    const auto& lut = SrgbLut::instance();

    // Linearized conversion from 8-bit sRGB to Aligned Float Linear
    size_t total_elements = static_cast<size_t>(width) * height * channels;
    for (size_t i = 0; i < total_elements; ++i) {
        view.data[i] = lut.to_linear(data[i] / 255.0f);
    }

    stbi_image_free(data);
    return std::move(buffer);
}

Result<void> write_png(const std::filesystem::path& path, const Image& image) {
    if (image.empty()) {
        return unexpected(Error{ ErrorCode::InvalidParameters, "Cannot write empty image to PNG" });
    }

    const auto& lut = SrgbLut::instance();
    std::vector<unsigned char> srgb_data(image.data.size());

    // Linearized conversion from Aligned Float Linear to 8-bit sRGB
    for (size_t i = 0; i < image.data.size(); ++i) {
        float srgb = lut.to_srgb(image.data[i]);
        srgb_data[i] = static_cast<unsigned char>(std::clamp(static_cast<int>(srgb * 255.0f + 0.5f), 0, 255));
    }

    int success = stbi_write_png(path.string().c_str(), image.width, image.height, image.channels, srgb_data.data(), image.width * image.channels);

    if (!success) {
        return unexpected(Error{ ErrorCode::IoError, "STB failed to write PNG" });
    }

    return {};
}

} // namespace corridorkey
