#include "ofx_image_utils.hpp"

#include <algorithm>
#include <cstring>

#include "common/srgb_lut.hpp"

namespace corridorkey::ofx {

ImageHandleGuard::~ImageHandleGuard() {
    if (handle != nullptr && g_suites.image_effect != nullptr) {
        g_suites.image_effect->clipReleaseImage(handle);
    }
}

bool get_double(OfxPropertySetHandle props, const char* name, double& value) {
    return g_suites.property->propGetDouble(props, name, 0, &value) == kOfxStatOK;
}

bool get_int(OfxPropertySetHandle props, const char* name, int& value) {
    return g_suites.property->propGetInt(props, name, 0, &value) == kOfxStatOK;
}

bool get_string(OfxPropertySetHandle props, const char* name, std::string& value) {
    char* raw = nullptr;
    if (g_suites.property->propGetString(props, name, 0, &raw) != kOfxStatOK || raw == nullptr) {
        return false;
    }
    value = raw;
    return true;
}

bool get_rect_i(OfxPropertySetHandle props, const char* name, OfxRectI& rect) {
    int values[4] = {};
    if (g_suites.property->propGetIntN(props, name, 4, values) != kOfxStatOK) {
        return false;
    }
    rect.x1 = values[0];
    rect.y1 = values[1];
    rect.x2 = values[2];
    rect.y2 = values[3];
    return true;
}

bool fetch_image(OfxImageClipHandle clip, OfxTime time, OfxPropertySetHandle& image_props) {
    return g_suites.image_effect->clipGetImage(clip, time, nullptr, &image_props) == kOfxStatOK;
}

bool is_depth(const std::string& depth, const char* expected) {
    return std::strcmp(depth.c_str(), expected) == 0;
}

void copy_source_to_linear(Image dst, const void* src_data, int row_bytes,
                           const std::string& depth) {
    const bool is_float = is_depth(depth, kOfxBitDepthFloat);
    const bool is_byte = is_depth(depth, kOfxBitDepthByte);

    for (int y_pos = 0; y_pos < dst.height; ++y_pos) {
        auto row = reinterpret_cast<const unsigned char*>(src_data) +
                   static_cast<ptrdiff_t>(y_pos) * static_cast<ptrdiff_t>(row_bytes);
        for (int x_pos = 0; x_pos < dst.width; ++x_pos) {
            if (is_float) {
                const float* pixel =
                    reinterpret_cast<const float*>(row) + static_cast<size_t>(x_pos) * 4;
                dst(y_pos, x_pos, 0) = pixel[0];
                dst(y_pos, x_pos, 1) = pixel[1];
                dst(y_pos, x_pos, 2) = pixel[2];
            } else if (is_byte) {
                const unsigned char* pixel = row + static_cast<size_t>(x_pos) * 4;
                dst(y_pos, x_pos, 0) = static_cast<float>(pixel[0]) / 255.0f;
                dst(y_pos, x_pos, 1) = static_cast<float>(pixel[1]) / 255.0f;
                dst(y_pos, x_pos, 2) = static_cast<float>(pixel[2]) / 255.0f;
            }
        }
    }
}

void write_output_image(const Image& src, void* dst_data, int row_bytes, const std::string& depth) {
    const bool is_float = is_depth(depth, kOfxBitDepthFloat);
    const bool is_byte = is_depth(depth, kOfxBitDepthByte);
    const SrgbLut& lut = SrgbLut::instance();

    for (int y_pos = 0; y_pos < src.height; ++y_pos) {
        auto row = reinterpret_cast<unsigned char*>(dst_data) +
                   static_cast<ptrdiff_t>(y_pos) * static_cast<ptrdiff_t>(row_bytes);
        for (int x_pos = 0; x_pos < src.width; ++x_pos) {
            float r = src(y_pos, x_pos, 0);
            float g = src(y_pos, x_pos, 1);
            float b = src(y_pos, x_pos, 2);
            float a = src(y_pos, x_pos, 3);

            if (is_byte) {
                r = lut.to_srgb(r);
                g = lut.to_srgb(g);
                b = lut.to_srgb(b);
            }

            if (is_float) {
                float* pixel = reinterpret_cast<float*>(row) + static_cast<size_t>(x_pos) * 4;
                pixel[0] = r;
                pixel[1] = g;
                pixel[2] = b;
                pixel[3] = a;
            } else if (is_byte) {
                unsigned char* pixel = row + static_cast<size_t>(x_pos) * 4;
                pixel[0] = static_cast<unsigned char>(std::clamp(r * 255.0f + 0.5f, 0.0f, 255.0f));
                pixel[1] = static_cast<unsigned char>(std::clamp(g * 255.0f + 0.5f, 0.0f, 255.0f));
                pixel[2] = static_cast<unsigned char>(std::clamp(b * 255.0f + 0.5f, 0.0f, 255.0f));
                pixel[3] = static_cast<unsigned char>(std::clamp(a * 255.0f + 0.5f, 0.0f, 255.0f));
            }
        }
    }
}

}  // namespace corridorkey::ofx
