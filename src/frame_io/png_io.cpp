#include "png_io.hpp"

#include "common/srgb_lut.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include <vendor/stb_image.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <vendor/stb_image_write.h>

#include <algorithm>
#include <vector>

// NOLINTBEGIN(modernize-use-designated-initializers,cppcoreguidelines-avoid-magic-numbers,readability-uppercase-literal-suffix,cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,bugprone-narrowing-conversions,cppcoreguidelines-narrowing-conversions,bugprone-incorrect-roundings,readability-math-missing-parentheses)
//
// png_io.cpp tidy-suppression rationale.
//
// This translation unit is the offline PNG read/write path via the
// stb_image / stb_image_write single-header libraries. It is not on the
// OFX render hot path. The pixel-conversion loops use the universal
// (i, channel) iteration pattern across a flat buffer, with 255.0F /
// 0.5F sRGB quantization constants whose meaning is documented at the
// surrounding sRGB LUT call site; naming each constant or splitting
// the loop into helpers would obscure the canonical "convert linear
// float to 8-bit sRGB" mapping. Error{...} positional initializers are
// the project's Result-error pattern repeated dozens of times across
// frame_io and the diff cost outweighs the readability benefit.
namespace corridorkey {

Result<ImageBuffer> read_stb(const std::filesystem::path& path) {
    int width = 0;
    int height = 0;
    int channels = 0;
    unsigned char* data = stbi_load(path.string().c_str(), &width, &height, &channels, 0);

    if (data == nullptr) {
        return Unexpected(Error{ErrorCode::IoError,
                                "STB failed to load image: " + std::string(stbi_failure_reason())});
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
        return Unexpected(Error{ErrorCode::InvalidParameters, "Cannot write empty image to PNG"});
    }

    const auto& lut = SrgbLut::instance();
    std::vector<unsigned char> srgb_data(image.data.size());

    // Linearized conversion from Aligned Float Linear to 8-bit sRGB
    for (size_t i = 0; i < image.data.size(); ++i) {
        float srgb = lut.to_srgb(image.data[i]);
        srgb_data[i] =
            static_cast<unsigned char>(std::clamp(static_cast<int>(srgb * 255.0f + 0.5f), 0, 255));
    }

    int success = stbi_write_png(path.string().c_str(), image.width, image.height, image.channels,
                                 srgb_data.data(), image.width * image.channels);

    if (success == 0) {
        return Unexpected(Error{ErrorCode::IoError, "STB failed to write PNG"});
    }

    return {};
}

}  // namespace corridorkey
// NOLINTEND(modernize-use-designated-initializers,cppcoreguidelines-avoid-magic-numbers,readability-uppercase-literal-suffix,cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,bugprone-narrowing-conversions,cppcoreguidelines-narrowing-conversions,bugprone-incorrect-roundings,readability-math-missing-parentheses)
