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
        float* dst_row = &dst(y_pos, 0, 0);
        if (is_float) {
            const float* src_pixel = reinterpret_cast<const float*>(row);
            for (int x_pos = 0; x_pos < dst.width; ++x_pos) {
                dst_row[0] = src_pixel[0];
                dst_row[1] = src_pixel[1];
                dst_row[2] = src_pixel[2];
                src_pixel += 4;
                dst_row += 3;
            }
        } else if (is_byte) {
            const unsigned char* src_pixel = row;
            for (int x_pos = 0; x_pos < dst.width; ++x_pos) {
                dst_row[0] = static_cast<float>(src_pixel[0]) / 255.0f;
                dst_row[1] = static_cast<float>(src_pixel[1]) / 255.0f;
                dst_row[2] = static_cast<float>(src_pixel[2]) / 255.0f;
                src_pixel += 4;
                dst_row += 3;
            }
        }
    }
}

void write_output_image(const Image& src, void* dst_data, int row_bytes, const std::string& depth,
                        bool apply_srgb) {
    const bool is_float = is_depth(depth, kOfxBitDepthFloat);
    const bool is_byte = is_depth(depth, kOfxBitDepthByte);
    const SrgbLut& lut = SrgbLut::instance();

    for (int y_pos = 0; y_pos < src.height; ++y_pos) {
        auto row = reinterpret_cast<unsigned char*>(dst_data) +
                   static_cast<ptrdiff_t>(y_pos) * static_cast<ptrdiff_t>(row_bytes);
        const float* src_row = &src(y_pos, 0, 0);
        if (is_float) {
            float* dst_pixel = reinterpret_cast<float*>(row);
            if (apply_srgb) {
                for (int x_pos = 0; x_pos < src.width; ++x_pos) {
                    float a = src_row[3];
                    if (a > 0.0f) {
                        float inv_a = 1.0f / a;
                        dst_pixel[0] = lut.to_srgb(src_row[0] * inv_a) * a;
                        dst_pixel[1] = lut.to_srgb(src_row[1] * inv_a) * a;
                        dst_pixel[2] = lut.to_srgb(src_row[2] * inv_a) * a;
                    } else {
                        dst_pixel[0] = 0.0f;
                        dst_pixel[1] = 0.0f;
                        dst_pixel[2] = 0.0f;
                    }
                    dst_pixel[3] = a;
                    src_row += 4;
                    dst_pixel += 4;
                }
            } else {
                for (int x_pos = 0; x_pos < src.width; ++x_pos) {
                    dst_pixel[0] = src_row[0];
                    dst_pixel[1] = src_row[1];
                    dst_pixel[2] = src_row[2];
                    dst_pixel[3] = src_row[3];
                    src_row += 4;
                    dst_pixel += 4;
                }
            }
        } else if (is_byte) {
            unsigned char* dst_pixel = row;
            for (int x_pos = 0; x_pos < src.width; ++x_pos) {
                float a = src_row[3];
                if (a > 0.0f) {
                    float inv_a = 1.0f / a;
                    float r = lut.to_srgb(src_row[0] * inv_a) * a;
                    float g = lut.to_srgb(src_row[1] * inv_a) * a;
                    float b = lut.to_srgb(src_row[2] * inv_a) * a;
                    dst_pixel[0] =
                        static_cast<unsigned char>(std::clamp(r * 255.0f + 0.5f, 0.0f, 255.0f));
                    dst_pixel[1] =
                        static_cast<unsigned char>(std::clamp(g * 255.0f + 0.5f, 0.0f, 255.0f));
                    dst_pixel[2] =
                        static_cast<unsigned char>(std::clamp(b * 255.0f + 0.5f, 0.0f, 255.0f));
                } else {
                    dst_pixel[0] = 0;
                    dst_pixel[1] = 0;
                    dst_pixel[2] = 0;
                }
                dst_pixel[3] =
                    static_cast<unsigned char>(std::clamp(a * 255.0f + 0.5f, 0.0f, 255.0f));
                src_row += 4;
                dst_pixel += 4;
            }
        }
    }
}

}  // namespace corridorkey::ofx
