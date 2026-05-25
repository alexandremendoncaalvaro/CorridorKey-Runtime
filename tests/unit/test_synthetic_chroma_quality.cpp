#include <catch2/catch_all.hpp>
#include <cstddef>

#include "../visual_quality_synthetic.hpp"
#include "plugins/adobe/adobe_bridge.hpp"
#include "post_process/color_utils.hpp"

using namespace corridorkey;
using namespace corridorkey::adobe;
using namespace corridorkey::tests;

namespace {

// NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,readability-identifier-length,cppcoreguidelines-avoid-magic-numbers,readability-function-cognitive-complexity,readability-function-size,modernize-use-designated-initializers,modernize-loop-convert)

ImageBuffer adobe_argb128_to_rgba(Image argb) {
    ImageBuffer rgba_buf(argb.width, argb.height, 4);
    Image rgba = rgba_buf.view();
    for (int y_pos = 0; y_pos < argb.height; ++y_pos) {
        for (int x_pos = 0; x_pos < argb.width; ++x_pos) {
            rgba(y_pos, x_pos, 0) = argb(y_pos, x_pos, 1);
            rgba(y_pos, x_pos, 1) = argb(y_pos, x_pos, 2);
            rgba(y_pos, x_pos, 2) = argb(y_pos, x_pos, 3);
            rgba(y_pos, x_pos, 3) = argb(y_pos, x_pos, 0);
        }
    }
    return rgba_buf;
}

void premultiply_rgb_by_alpha(Image rgba) {
    for (int y_pos = 0; y_pos < rgba.height; ++y_pos) {
        for (int x_pos = 0; x_pos < rgba.width; ++x_pos) {
            const float alpha = rgba(y_pos, x_pos, 3);
            rgba(y_pos, x_pos, 0) *= alpha;
            rgba(y_pos, x_pos, 1) *= alpha;
            rgba(y_pos, x_pos, 2) *= alpha;
        }
    }
}

FrameResult frame_result_from_fixture(const SyntheticChromaFixture& fixture) {
    FrameResult result;
    result.alpha = copy_image_buffer(fixture.alpha.const_view());
    result.foreground = copy_image_buffer(fixture.foreground.const_view());
    result.processed = copy_image_buffer(fixture.expected_processed.const_view());
    result.post_processed = true;
    return result;
}

int count_alpha_pixels(Image alpha, float minimum, float maximum, int y_limit = -1, int x_min = 0) {
    int count = 0;
    const int effective_y_limit = y_limit >= 0 ? std::min(y_limit, alpha.height) : alpha.height;
    for (int y_pos = 0; y_pos < effective_y_limit; ++y_pos) {
        for (int x_pos = x_min; x_pos < alpha.width; ++x_pos) {
            const float value = alpha(y_pos, x_pos);
            if (value >= minimum && value <= maximum) {
                ++count;
            }
        }
    }
    return count;
}

void darken_edge(Image processed, Image alpha, float scale) {
    for (int y_pos = 0; y_pos < processed.height; ++y_pos) {
        for (int x_pos = 0; x_pos < processed.width; ++x_pos) {
            const float alpha_value = alpha(y_pos, x_pos);
            if (alpha_value > 0.02F && alpha_value < 0.98F) {
                processed(y_pos, x_pos, 0) *= scale;
                processed(y_pos, x_pos, 1) *= scale;
                processed(y_pos, x_pos, 2) *= scale;
            }
        }
    }
}

void inject_stains(Image processed, Image alpha) {
    for (int y_pos = 0; y_pos < processed.height; ++y_pos) {
        for (int x_pos = 0; x_pos < processed.width; ++x_pos) {
            const float alpha_value = alpha(y_pos, x_pos);
            if ((alpha_value <= 0.02F || alpha_value >= 0.98F) &&
                (((x_pos * 7) + (y_pos * 13)) % 41 == 0)) {
                processed(y_pos, x_pos, 0) = std::min(1.0F, processed(y_pos, x_pos, 0) + 0.18F);
            }
        }
    }
}

ImageBuffer resized_processed(Image alpha, Image foreground, bool lanczos) {
    const int low_width = alpha.width / 2;
    const int low_height = alpha.height / 2;
    ColorUtils::State state;
    ImageBuffer low_alpha = ColorUtils::resize_area(alpha, low_width, low_height, state);
    ImageBuffer low_foreground = ColorUtils::resize_area(foreground, low_width, low_height, state);

    ImageBuffer high_alpha(alpha.width, alpha.height, 1);
    ImageBuffer high_foreground(foreground.width, foreground.height, 3);
    if (lanczos) {
        ColorUtils::resize_lanczos_into(low_alpha.const_view(), high_alpha.view(), state);
        ColorUtils::resize_lanczos_into(low_foreground.const_view(), high_foreground.view(), state);
    } else {
        ColorUtils::resize_into(low_alpha.const_view(), high_alpha.view());
        ColorUtils::resize_into(low_foreground.const_view(), high_foreground.view());
    }
    return build_expected_processed(high_alpha.const_view(), high_foreground.const_view());
}

// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,readability-identifier-length,cppcoreguidelines-avoid-magic-numbers,readability-function-cognitive-complexity,readability-function-size,modernize-use-designated-initializers,modernize-loop-convert)

}  // namespace

TEST_CASE("synthetic chroma fixture contains hair motion blur and green spill ground truth",
          "[unit][quality][synthetic]") {
    auto fixture = make_synthetic_chroma_fixture(96, 64);
    Image alpha = fixture.alpha.const_view();
    Image source = fixture.source.const_view();

    CHECK(count_alpha_pixels(alpha, 0.02F, 0.98F) > 650);
    CHECK(count_alpha_pixels(alpha, 0.05F, 0.85F, 24) > 45);
    CHECK(count_alpha_pixels(alpha, 0.04F, 0.42F, -1, 62) > 20);
    CHECK(source(0, 0, 1) > 0.80F);
    CHECK(source(0, 0, 0) < 0.08F);
}

TEST_CASE("synthetic quality metrics accept the oracle and reject edge aura and stains",
          "[unit][quality][synthetic][regression]") {
    auto fixture = make_synthetic_chroma_fixture(96, 64);
    const auto oracle_report = evaluate_processed_quality(fixture.expected_processed.const_view(),
                                                          fixture.expected_processed.const_view(),
                                                          fixture.alpha.const_view());
    CHECK(passes_visual_quality(oracle_report));
    CHECK(oracle_report.edge_pixel_count > 650);

    ImageBuffer aura_buf = copy_image_buffer(fixture.expected_processed.const_view());
    darken_edge(aura_buf.view(), fixture.alpha.const_view(), 0.62F);
    const auto aura_report = evaluate_processed_quality(
        aura_buf.const_view(), fixture.expected_processed.const_view(), fixture.alpha.const_view());
    CHECK_FALSE(passes_visual_quality(aura_report));
    CHECK(aura_report.edge_luma_bias < -0.01F);

    ImageBuffer stained_buf = copy_image_buffer(fixture.expected_processed.const_view());
    inject_stains(stained_buf.view(), fixture.alpha.const_view());
    const auto stained_report = evaluate_processed_quality(stained_buf.const_view(),
                                                           fixture.expected_processed.const_view(),
                                                           fixture.alpha.const_view());
    CHECK_FALSE(passes_visual_quality(stained_report));
    CHECK(stained_report.stable_region_max_error > 0.10F);
}

TEST_CASE("adobe processed output matches the synthetic chroma oracle",
          "[unit][adobe][runtime][quality][synthetic][regression]") {
    auto fixture = make_synthetic_chroma_fixture(96, 64);
    FrameResult result = frame_result_from_fixture(fixture);
    ImageBuffer argb_buf(96, 64, 4);
    Image argb = argb_buf.view();

    auto write_status = copy_runtime_result_to_adobe_frame(
        result,
        AdobeMutableFrameView{
            .data = argb.data.data(),
            .data_size_bytes = argb.data.size() * sizeof(float),
            .width = argb.width,
            .height = argb.height,
            .row_bytes = argb.width * 4 * static_cast<int>(sizeof(float)),
            .pixel_format = AdobePixelFormat::Argb128,
        },
        0);

    REQUIRE(write_status.has_value());
    ImageBuffer actual_rgba = adobe_argb128_to_rgba(argb);
    premultiply_rgb_by_alpha(actual_rgba.view());
    const auto report = evaluate_processed_quality(actual_rgba.const_view(),
                                                   fixture.expected_processed.const_view(),
                                                   fixture.alpha.const_view());
    CHECK(passes_visual_quality(report));
}

TEST_CASE("bilinear and lanczos resize synthetic mattes without halo class artifacts",
          "[unit][quality][synthetic][resize][regression]") {
    auto fixture = make_synthetic_chroma_fixture(128, 96);

    ImageBuffer bilinear_processed =
        resized_processed(fixture.alpha.const_view(), fixture.foreground.const_view(), false);
    const auto bilinear_report = evaluate_processed_quality(bilinear_processed.const_view(),
                                                            fixture.expected_processed.const_view(),
                                                            fixture.alpha.const_view());
    CAPTURE(bilinear_report.max_color_error);
    CAPTURE(bilinear_report.max_alpha_error);
    CAPTURE(bilinear_report.edge_mean_luma_abs_error);
    CAPTURE(bilinear_report.stable_region_max_error);
    CAPTURE(bilinear_report.edge_luma_bias);
    CHECK(bilinear_report.edge_mean_luma_abs_error < 0.006F);
    CHECK(std::abs(bilinear_report.edge_luma_bias) < 0.003F);

    ImageBuffer lanczos_processed =
        resized_processed(fixture.alpha.const_view(), fixture.foreground.const_view(), true);
    const auto lanczos_report = evaluate_processed_quality(lanczos_processed.const_view(),
                                                           fixture.expected_processed.const_view(),
                                                           fixture.alpha.const_view());
    CAPTURE(lanczos_report.max_color_error);
    CAPTURE(lanczos_report.max_alpha_error);
    CAPTURE(lanczos_report.edge_mean_luma_abs_error);
    CAPTURE(lanczos_report.stable_region_max_error);
    CAPTURE(lanczos_report.edge_luma_bias);
    CHECK(lanczos_report.edge_mean_luma_abs_error < 0.006F);
    CHECK(std::abs(lanczos_report.edge_luma_bias) < 0.003F);
}
