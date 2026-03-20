#pragma once

#include <string>

#include "ofx_shared.hpp"

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
bool is_clip_connected(OfxImageClipHandle clip);
bool is_depth(const std::string& depth, const char* expected);
bool is_alpha_hint_single_channel(const std::string& components);
bool is_alpha_hint_rgb(const std::string& components);
std::string alpha_hint_interpretation_label(const std::string& components);

void copy_source_to_linear(Image dst, const void* src_data, int row_bytes,
                           const std::string& depth);
void copy_alpha_hint(Image dst, const void* src_data, int row_bytes, const std::string& depth,
                     const std::string& components);
void write_output_image(const Image& src, void* dst_data, int row_bytes, const std::string& depth,
                        bool apply_srgb = false);

}  // namespace corridorkey::ofx
