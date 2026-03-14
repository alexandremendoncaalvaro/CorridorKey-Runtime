#pragma once

#include "ofx_shared.hpp"

#include <string>

namespace corridorkey::ofx {

struct ImageHandleGuard {
    OfxPropertySetHandle handle = nullptr;
    ~ImageHandleGuard();
};

bool get_double(OfxPropertySetHandle props, const char* name, double& value);
bool get_int(OfxPropertySetHandle props, const char* name, int& value);
bool get_string(OfxPropertySetHandle props, const char* name, std::string& value);
bool get_rect_i(OfxPropertySetHandle props, const char* name, OfxRectI& rect);

bool fetch_image(OfxImageClipHandle clip, OfxTime time, OfxPropertySetHandle& image_props);
bool is_depth(const std::string& depth, const char* expected);

void copy_source_to_linear(Image dst, const void* src_data, int row_bytes,
                           const std::string& depth);
void write_output_image(const Image& src, void* dst_data, int row_bytes,
                        const std::string& depth);

}  // namespace corridorkey::ofx
