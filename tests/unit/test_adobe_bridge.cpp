#include <algorithm>
#include <array>
#include <catch2/catch_all.hpp>
#include <cstdint>
#include <memory>

#include "common/srgb_lut.hpp"
#include "plugins/adobe/adobe_bridge.hpp"
#include "plugins/adobe/adobe_runtime_client_cache.hpp"

using namespace corridorkey;
using namespace corridorkey::adobe;

namespace {

AdobePrepareSessionOptions make_prepare_options() {
    AdobePrepareSessionOptions options;
    options.host_surface = "after_effects";
    options.effect_identity = "com.corridorkey.effect";
    options.model_path = "models/corridorkey_dynamic_blue_fp16.ts";
    options.requested_quality_mode = 3;
    options.requested_resolution = 2048;
    options.effective_resolution = 1536;
    options.prepare_timeout_ms = 45000;
    return options;
}

ImageBuffer copy_image(Image source) {
    ImageBuffer copy(source.width, source.height, source.channels);
    std::copy(source.data.begin(), source.data.end(), copy.view().data.begin());
    return copy;
}

std::uint8_t quantize_adobe_argb8(float value) {
    return static_cast<std::uint8_t>(
        std::clamp((std::clamp(value, 0.0F, 1.0F) * 255.0F) + 0.5F, 0.0F, 255.0F));
}

void require_images_equal(Image actual, Image expected, float margin = 0.0001F) {
    REQUIRE(actual.width == expected.width);
    REQUIRE(actual.height == expected.height);
    REQUIRE(actual.channels == expected.channels);
    REQUIRE(actual.data.size() == expected.data.size());

    for (std::size_t index = 0; index < actual.data.size(); ++index) {
        CHECK(actual.data[index] == Catch::Approx(expected.data[index]).margin(margin));
    }
}

}  // namespace

TEST_CASE("adobe bridge copies ARGB32 frames with row padding into runtime buffers",
          "[unit][adobe][runtime]") {
    constexpr int width = 2;
    constexpr int height = 2;
    constexpr int row_bytes = 12;
    std::array<std::uint8_t, 24> pixels{
        255, 10, 20, 30, 128, 40, 50, 60, 0, 0, 0, 0, 64, 70, 80, 90, 0, 1, 2, 3, 0, 0, 0, 0,
    };

    const AdobeFrameView frame{
        .data = pixels.data(),
        .data_size_bytes = pixels.size(),
        .width = width,
        .height = height,
        .row_bytes = row_bytes,
        .pixel_format = AdobePixelFormat::Argb32,
    };

    auto converted = copy_adobe_frame_to_runtime(frame);

    REQUIRE(converted.has_value());
    auto rgb = converted->rgb.view();
    auto alpha_hint = converted->alpha_hint.view();
    REQUIRE(rgb.width == width);
    REQUIRE(rgb.height == height);
    REQUIRE(rgb.channels == 3);
    REQUIRE(alpha_hint.channels == 1);
    CHECK(rgb(0, 0, 0) == Catch::Approx(10.0F / 255.0F));
    CHECK(rgb(0, 0, 1) == Catch::Approx(20.0F / 255.0F));
    CHECK(rgb(0, 0, 2) == Catch::Approx(30.0F / 255.0F));
    CHECK(alpha_hint(0, 0) == Catch::Approx(1.0F));
    CHECK(rgb(1, 0, 0) == Catch::Approx(70.0F / 255.0F));
    CHECK(alpha_hint(1, 0) == Catch::Approx(64.0F / 255.0F));
    CHECK(rgb(1, 1, 2) == Catch::Approx(3.0F / 255.0F));
    CHECK(alpha_hint(1, 1) == Catch::Approx(0.0F));
}

TEST_CASE("adobe bridge normalizes ARGB64 frames using Adobe deep-color white",
          "[unit][adobe][runtime]") {
    constexpr int row_bytes = 16;
    std::array<std::uint16_t, 16> pixels{
        32768, 16384, 8192, 4096, 0, 0, 0, 0, 16384, 32768, 0, 8192, 0, 0, 0, 0,
    };

    const AdobeFrameView frame{
        .data = pixels.data(),
        .data_size_bytes = pixels.size() * sizeof(std::uint16_t),
        .width = 1,
        .height = 2,
        .row_bytes = row_bytes,
        .pixel_format = AdobePixelFormat::Argb64,
    };

    auto converted = copy_adobe_frame_to_runtime(frame);

    REQUIRE(converted.has_value());
    auto rgb = converted->rgb.view();
    auto alpha_hint = converted->alpha_hint.view();
    CHECK(alpha_hint(0, 0) == Catch::Approx(1.0F));
    CHECK(rgb(0, 0, 0) == Catch::Approx(0.5F));
    CHECK(rgb(0, 0, 1) == Catch::Approx(0.25F));
    CHECK(rgb(0, 0, 2) == Catch::Approx(0.125F));
    CHECK(alpha_hint(1, 0) == Catch::Approx(0.5F));
    CHECK(rgb(1, 0, 0) == Catch::Approx(1.0F));
    CHECK(rgb(1, 0, 1) == Catch::Approx(0.0F));
    CHECK(rgb(1, 0, 2) == Catch::Approx(0.25F));
}

TEST_CASE("adobe bridge copies ARGB128 float frames without quantizing values",
          "[unit][adobe][runtime]") {
    std::array<float, 8> pixels{
        0.25F, 0.5F, 1.25F, -0.5F, 1.0F, 0.0F, 0.75F, 2.0F,
    };

    const AdobeFrameView frame{
        .data = pixels.data(),
        .data_size_bytes = pixels.size() * sizeof(float),
        .width = 2,
        .height = 1,
        .row_bytes = 32,
        .pixel_format = AdobePixelFormat::Argb128,
    };

    auto converted = copy_adobe_frame_to_runtime(frame);

    REQUIRE(converted.has_value());
    auto rgb = converted->rgb.view();
    auto alpha_hint = converted->alpha_hint.view();
    CHECK(alpha_hint(0, 0) == Catch::Approx(0.25F));
    CHECK(rgb(0, 0, 0) == Catch::Approx(0.5F));
    CHECK(rgb(0, 0, 1) == Catch::Approx(1.25F));
    CHECK(rgb(0, 0, 2) == Catch::Approx(-0.5F));
    CHECK(alpha_hint(0, 1) == Catch::Approx(1.0F));
    CHECK(rgb(0, 1, 2) == Catch::Approx(2.0F));
}

TEST_CASE("adobe bridge copies Premiere BGRA32 frames into runtime RGB order",
          "[unit][adobe][runtime]") {
    std::array<std::uint8_t, 8> pixels{
        5, 10, 20, 40, 200, 150, 100, 255,
    };

    const AdobeFrameView frame{
        .data = pixels.data(),
        .data_size_bytes = pixels.size(),
        .width = 2,
        .height = 1,
        .row_bytes = 8,
        .pixel_format = AdobePixelFormat::Bgra32,
    };

    auto converted = copy_adobe_frame_to_runtime(frame);

    REQUIRE(converted.has_value());
    auto rgb = converted->rgb.view();
    auto alpha_hint = converted->alpha_hint.view();
    CHECK(rgb(0, 0, 0) == Catch::Approx(20.0F / 255.0F));
    CHECK(rgb(0, 0, 1) == Catch::Approx(10.0F / 255.0F));
    CHECK(rgb(0, 0, 2) == Catch::Approx(5.0F / 255.0F));
    CHECK(alpha_hint(0, 0) == Catch::Approx(40.0F / 255.0F));
    CHECK(rgb(0, 1, 0) == Catch::Approx(100.0F / 255.0F));
    CHECK(rgb(0, 1, 1) == Catch::Approx(150.0F / 255.0F));
    CHECK(rgb(0, 1, 2) == Catch::Approx(200.0F / 255.0F));
    CHECK(alpha_hint(0, 1) == Catch::Approx(1.0F));
}

TEST_CASE("adobe matte controls apply OFX-compatible alpha adjustments",
          "[unit][adobe][runtime][matte]") {
    FrameResult result;
    result.alpha = ImageBuffer(3, 1, 1);
    result.foreground = ImageBuffer(3, 1, 3);
    auto alpha = result.alpha.view();
    alpha(0, 0) = 0.0F;
    alpha(0, 1) = 0.5F;
    alpha(0, 2) = 1.0F;

    AdobeMatteParams params{
        .black_point = 0.25,
        .white_point = 0.75,
        .shrink_grow_pixels = 0.0,
        .edge_blur_pixels = 0.0,
        .gamma = 1.0,
    };
    AlphaEdgeState state;

    apply_adobe_matte_params(result, params, 1920, 1080, state);

    CHECK(alpha(0, 0) == Catch::Approx(0.0F));
    CHECK(alpha(0, 1) == Catch::Approx(0.5F));
    CHECK(alpha(0, 2) == Catch::Approx(1.0F));
}

TEST_CASE("adobe matte controls scale pixel-radius controls from the OFX baseline",
          "[unit][adobe][runtime][matte]") {
    FrameResult result;
    result.alpha = ImageBuffer(5, 1, 1);
    result.foreground = ImageBuffer(5, 1, 3);
    auto alpha = result.alpha.view();
    std::fill(alpha.data.begin(), alpha.data.end(), 0.0F);
    alpha(0, 2) = 1.0F;

    AdobeMatteParams params{
        .black_point = 0.0,
        .white_point = 1.0,
        .shrink_grow_pixels = 1.0,
        .edge_blur_pixels = 0.0,
        .gamma = 1.0,
    };
    AlphaEdgeState state;

    apply_adobe_matte_params(result, params, 3840, 2160, state);

    CHECK(alpha(0, 0) == Catch::Approx(1.0F));
    CHECK(alpha(0, 1) == Catch::Approx(1.0F));
    CHECK(alpha(0, 2) == Catch::Approx(1.0F));
    CHECK(alpha(0, 3) == Catch::Approx(1.0F));
    CHECK(alpha(0, 4) == Catch::Approx(1.0F));
}

TEST_CASE("adobe bridge canonicalizes blue-green runtime frames through the green domain",
          "[unit][adobe][runtime][screen-color][regression]") {
    AdobeRuntimeFrame frame;
    frame.rgb = ImageBuffer(3, 1, 3);
    frame.alpha_hint = ImageBuffer(3, 1, 1);

    auto rgb = frame.rgb.view();
    rgb(0, 0, 0) = 1.0F;
    rgb(0, 0, 1) = 0.0F;
    rgb(0, 0, 2) = 0.0F;
    rgb(0, 1, 0) = 1.0F;
    rgb(0, 1, 1) = 1.0F;
    rgb(0, 1, 2) = 1.0F;
    rgb(0, 2, 0) = 0.12F;
    rgb(0, 2, 1) = 0.24F;
    rgb(0, 2, 2) = 0.84F;

    auto alpha = frame.alpha_hint.view();
    alpha(0, 0) = 0.25F;
    alpha(0, 1) = 0.5F;
    alpha(0, 2) = 0.75F;

    ImageBuffer original_rgb = copy_image(frame.rgb.view());
    ImageBuffer original_alpha = copy_image(frame.alpha_hint.view());

    const ScreenColorTransform transform =
        canonicalize_adobe_runtime_frame_for_screen_color(frame, ScreenColorMode::BlueGreen);

    CHECK_FALSE(transform.is_identity);
    CHECK(frame.rgb.view()(0, 0, 0) == Catch::Approx(1.0F).margin(0.0001F));
    CHECK(frame.rgb.view()(0, 0, 1) == Catch::Approx(0.0F).margin(0.0001F));
    CHECK(frame.rgb.view()(0, 0, 2) == Catch::Approx(0.0F).margin(0.0001F));
    CHECK(frame.rgb.view()(0, 1, 0) == Catch::Approx(1.0F).margin(0.0001F));
    CHECK(frame.rgb.view()(0, 1, 1) == Catch::Approx(1.0F).margin(0.0001F));
    CHECK(frame.rgb.view()(0, 1, 2) == Catch::Approx(1.0F).margin(0.0001F));
    CHECK(frame.rgb.view()(0, 2, 1) > frame.rgb.view()(0, 2, 2));
    require_images_equal(frame.alpha_hint.view(), original_alpha.view());

    restore_from_green_domain(frame.rgb.view(), transform);
    require_images_equal(frame.rgb.view(), original_rgb.view(), 0.0002F);
}

TEST_CASE("adobe bridge converts linear source frames to the runtime sRGB domain",
          "[unit][adobe][runtime][color]") {
    AdobeRuntimeFrame frame;
    frame.rgb = ImageBuffer(2, 1, 3);
    frame.alpha_hint = ImageBuffer(2, 1, 1);

    auto rgb = frame.rgb.view();
    rgb(0, 0, 0) = 0.0F;
    rgb(0, 0, 1) = 0.18F;
    rgb(0, 0, 2) = 1.0F;
    rgb(0, 1, 0) = 0.25F;
    rgb(0, 1, 1) = 0.5F;
    rgb(0, 1, 2) = 0.75F;
    auto alpha = frame.alpha_hint.view();
    alpha(0, 0) = 0.25F;
    alpha(0, 1) = 0.75F;

    apply_adobe_input_color_space(frame, true);

    const auto& lut = SrgbLut::instance();
    CHECK(rgb(0, 0, 0) == Catch::Approx(lut.to_srgb(0.0F)));
    CHECK(rgb(0, 0, 1) == Catch::Approx(lut.to_srgb(0.18F)));
    CHECK(rgb(0, 0, 2) == Catch::Approx(lut.to_srgb(1.0F)));
    CHECK(rgb(0, 1, 0) == Catch::Approx(lut.to_srgb(0.25F)));
    CHECK(rgb(0, 1, 1) == Catch::Approx(lut.to_srgb(0.5F)));
    CHECK(rgb(0, 1, 2) == Catch::Approx(lut.to_srgb(0.75F)));
    CHECK(alpha(0, 0) == Catch::Approx(0.25F));
    CHECK(alpha(0, 1) == Catch::Approx(0.75F));
}

TEST_CASE("adobe bridge prefers an explicit Alpha Hint layer over source alpha",
          "[unit][adobe][runtime][alpha-hint]") {
    AdobeRuntimeFrame frame;
    frame.rgb = ImageBuffer(2, 1, 3);
    frame.alpha_hint = ImageBuffer(2, 1, 1);
    std::fill(frame.alpha_hint.view().data.begin(), frame.alpha_hint.view().data.end(), 1.0F);

    std::array<std::uint8_t, 8> hint_pixels{
        64, 0, 0, 0, 191, 0, 0, 0,
    };
    const AdobeFrameView hint_frame{
        .data = hint_pixels.data(),
        .data_size_bytes = hint_pixels.size(),
        .width = 2,
        .height = 1,
        .row_bytes = 8,
        .pixel_format = AdobePixelFormat::Argb32,
    };

    auto source = resolve_alpha_hint_source(frame, &hint_frame, AlphaHintPolicy::AutoRoughFallback);

    REQUIRE(source.has_value());
    CHECK(*source == AdobeAlphaHintSource::ExternalLayerAlpha);
    CHECK(frame.alpha_hint.view()(0, 0) == Catch::Approx(64.0F / 255.0F));
    CHECK(frame.alpha_hint.view()(0, 1) == Catch::Approx(191.0F / 255.0F));
}

TEST_CASE("adobe bridge reads visible grayscale Alpha Hint layers with opaque alpha",
          "[unit][adobe][runtime][alpha-hint][regression]") {
    AdobeRuntimeFrame frame;
    frame.rgb = ImageBuffer(2, 1, 3);
    frame.alpha_hint = ImageBuffer(2, 1, 1);
    std::fill(frame.alpha_hint.view().data.begin(), frame.alpha_hint.view().data.end(), 1.0F);

    std::array<std::uint8_t, 8> hint_pixels{
        255, 64, 64, 64, 255, 191, 191, 191,
    };
    const AdobeFrameView hint_frame{
        .data = hint_pixels.data(),
        .data_size_bytes = hint_pixels.size(),
        .width = 2,
        .height = 1,
        .row_bytes = 8,
        .pixel_format = AdobePixelFormat::Argb32,
    };

    auto source = resolve_alpha_hint_source(frame, &hint_frame, AlphaHintPolicy::AutoRoughFallback);

    REQUIRE(source.has_value());
    CHECK(*source == AdobeAlphaHintSource::ExternalLayerRed);
    CHECK(frame.alpha_hint.view()(0, 0) == Catch::Approx(64.0F / 255.0F));
    CHECK(frame.alpha_hint.view()(0, 1) == Catch::Approx(191.0F / 255.0F));
}

TEST_CASE("adobe bridge reads deep-color grayscale Alpha Hint layers with opaque alpha",
          "[unit][adobe][runtime][alpha-hint][regression]") {
    AdobeRuntimeFrame frame;
    frame.rgb = ImageBuffer(2, 1, 3);
    frame.alpha_hint = ImageBuffer(2, 1, 1);
    std::fill(frame.alpha_hint.view().data.begin(), frame.alpha_hint.view().data.end(), 1.0F);

    std::array<std::uint16_t, 8> hint_pixels{
        32768, 8192, 8192, 8192, 32768, 24576, 24576, 24576,
    };
    const AdobeFrameView hint_frame{
        .data = hint_pixels.data(),
        .data_size_bytes = hint_pixels.size() * sizeof(std::uint16_t),
        .width = 2,
        .height = 1,
        .row_bytes = 16,
        .pixel_format = AdobePixelFormat::Argb64,
    };

    auto source = resolve_alpha_hint_source(frame, &hint_frame, AlphaHintPolicy::AutoRoughFallback);

    REQUIRE(source.has_value());
    CHECK(*source == AdobeAlphaHintSource::ExternalLayerRed);
    CHECK(frame.alpha_hint.view()(0, 0) == Catch::Approx(0.25F));
    CHECK(frame.alpha_hint.view()(0, 1) == Catch::Approx(0.75F));
}

TEST_CASE("adobe bridge converts linear visible Alpha Hint layers to runtime sRGB",
          "[unit][adobe][runtime][alpha-hint][regression]") {
    AdobeRuntimeFrame frame;
    frame.rgb = ImageBuffer(2, 1, 3);
    frame.alpha_hint = ImageBuffer(2, 1, 1);
    std::fill(frame.alpha_hint.view().data.begin(), frame.alpha_hint.view().data.end(), 1.0F);

    std::array<float, 8> hint_pixels{
        1.0F, 0.25F, 0.25F, 0.25F, 1.0F, 0.5F, 0.5F, 0.5F,
    };
    const AdobeFrameView hint_frame{
        .data = hint_pixels.data(),
        .data_size_bytes = hint_pixels.size() * sizeof(float),
        .width = 2,
        .height = 1,
        .row_bytes = 8 * static_cast<int>(sizeof(float)),
        .pixel_format = AdobePixelFormat::Argb128,
    };

    auto source =
        resolve_alpha_hint_source(frame, &hint_frame, AlphaHintPolicy::AutoRoughFallback, true);

    const auto& lut = SrgbLut::instance();
    REQUIRE(source.has_value());
    CHECK(*source == AdobeAlphaHintSource::ExternalLayerRed);
    CHECK(frame.alpha_hint.view()(0, 0) == Catch::Approx(lut.to_srgb(0.25F)));
    CHECK(frame.alpha_hint.view()(0, 1) == Catch::Approx(lut.to_srgb(0.5F)));
}

TEST_CASE("adobe bridge leaves real Alpha Hint layer alpha linear",
          "[unit][adobe][runtime][alpha-hint][regression]") {
    AdobeRuntimeFrame frame;
    frame.rgb = ImageBuffer(2, 1, 3);
    frame.alpha_hint = ImageBuffer(2, 1, 1);
    std::fill(frame.alpha_hint.view().data.begin(), frame.alpha_hint.view().data.end(), 1.0F);

    std::array<float, 8> hint_pixels{
        0.25F, 0.9F, 0.9F, 0.9F, 0.75F, 0.1F, 0.1F, 0.1F,
    };
    const AdobeFrameView hint_frame{
        .data = hint_pixels.data(),
        .data_size_bytes = hint_pixels.size() * sizeof(float),
        .width = 2,
        .height = 1,
        .row_bytes = 8 * static_cast<int>(sizeof(float)),
        .pixel_format = AdobePixelFormat::Argb128,
    };

    auto source =
        resolve_alpha_hint_source(frame, &hint_frame, AlphaHintPolicy::AutoRoughFallback, true);

    REQUIRE(source.has_value());
    CHECK(*source == AdobeAlphaHintSource::ExternalLayerAlpha);
    CHECK(frame.alpha_hint.view()(0, 0) == Catch::Approx(0.25F));
    CHECK(frame.alpha_hint.view()(0, 1) == Catch::Approx(0.75F));
}

TEST_CASE("adobe bridge labels alpha hint source decisions for diagnostics",
          "[unit][adobe][runtime][alpha-hint]") {
    CHECK(adobe_alpha_hint_source_label(AdobeAlphaHintSource::ExternalLayerAlpha) ==
          "external_layer_alpha");
    CHECK(adobe_alpha_hint_source_label(AdobeAlphaHintSource::ExternalLayerRed) ==
          "external_layer_red");
    CHECK(adobe_alpha_hint_source_label(AdobeAlphaHintSource::SourceAlpha) == "source_alpha");
    CHECK(adobe_alpha_hint_source_label(AdobeAlphaHintSource::RoughFallback) == "rough_fallback");
}

TEST_CASE("adobe bridge falls back when an explicit Alpha Hint layer is fully opaque",
          "[unit][adobe][runtime][alpha-hint][regression]") {
    AdobeRuntimeFrame frame;
    frame.rgb = ImageBuffer(2, 1, 3);
    frame.alpha_hint = ImageBuffer(2, 1, 1);
    auto rgb = frame.rgb.view();
    rgb(0, 0, 0) = 0.0F;
    rgb(0, 0, 1) = 1.0F;
    rgb(0, 0, 2) = 0.0F;
    rgb(0, 1, 0) = 1.0F;
    rgb(0, 1, 1) = 0.0F;
    rgb(0, 1, 2) = 0.0F;
    std::fill(frame.alpha_hint.view().data.begin(), frame.alpha_hint.view().data.end(), 1.0F);

    std::array<std::uint8_t, 8> hint_pixels{
        255, 0, 0, 0, 255, 0, 0, 0,
    };
    const AdobeFrameView hint_frame{
        .data = hint_pixels.data(),
        .data_size_bytes = hint_pixels.size(),
        .width = 2,
        .height = 1,
        .row_bytes = 8,
        .pixel_format = AdobePixelFormat::Argb32,
    };

    auto source = resolve_alpha_hint_source(frame, &hint_frame, AlphaHintPolicy::AutoRoughFallback);

    REQUIRE(source.has_value());
    CHECK(*source == AdobeAlphaHintSource::RoughFallback);
    CHECK(frame.alpha_hint.view()(0, 0) < frame.alpha_hint.view()(0, 1));
}

TEST_CASE("adobe bridge preserves source alpha when an external Alpha Hint layer is unreadable",
          "[unit][adobe][runtime][alpha-hint][regression]") {
    AdobeRuntimeFrame frame;
    frame.rgb = ImageBuffer(2, 1, 3);
    frame.alpha_hint = ImageBuffer(2, 1, 1);
    auto alpha = frame.alpha_hint.view();
    alpha(0, 0) = 0.0F;
    alpha(0, 1) = 1.0F;

    std::array<std::uint8_t, 8> hint_pixels{
        255, 255, 255, 255, 255, 255, 255, 255,
    };
    const AdobeFrameView hint_frame{
        .data = hint_pixels.data(),
        .data_size_bytes = hint_pixels.size(),
        .width = 2,
        .height = 1,
        .row_bytes = 8,
        .pixel_format = AdobePixelFormat::Argb32,
    };

    auto source = resolve_alpha_hint_source(frame, &hint_frame, AlphaHintPolicy::AutoRoughFallback);

    REQUIRE(source.has_value());
    CHECK(*source == AdobeAlphaHintSource::SourceAlpha);
    CHECK(frame.alpha_hint.view()(0, 0) == Catch::Approx(0.0F));
    CHECK(frame.alpha_hint.view()(0, 1) == Catch::Approx(1.0F));
}

TEST_CASE("adobe bridge generates rough fallback when no external hint is readable",
          "[unit][adobe][runtime][alpha-hint]") {
    AdobeRuntimeFrame frame;
    frame.rgb = ImageBuffer(2, 1, 3);
    frame.alpha_hint = ImageBuffer(2, 1, 1);
    auto rgb = frame.rgb.view();
    rgb(0, 0, 0) = 0.0F;
    rgb(0, 0, 1) = 1.0F;
    rgb(0, 0, 2) = 0.0F;
    rgb(0, 1, 0) = 1.0F;
    rgb(0, 1, 1) = 0.0F;
    rgb(0, 1, 2) = 0.0F;
    std::fill(frame.alpha_hint.view().data.begin(), frame.alpha_hint.view().data.end(), 1.0F);

    auto source = resolve_alpha_hint_source(frame, nullptr, AlphaHintPolicy::AutoRoughFallback);

    REQUIRE(source.has_value());
    CHECK(*source == AdobeAlphaHintSource::RoughFallback);
    CHECK(frame.alpha_hint.view()(0, 0) < frame.alpha_hint.view()(0, 1));
}

TEST_CASE("adobe bridge ignores nearly opaque source alpha when resolving alpha hints",
          "[unit][adobe][runtime][alpha-hint][regression]") {
    AdobeRuntimeFrame frame;
    frame.rgb = ImageBuffer(2, 1, 3);
    frame.alpha_hint = ImageBuffer(2, 1, 1);
    auto rgb = frame.rgb.view();
    rgb(0, 0, 0) = 0.0F;
    rgb(0, 0, 1) = 1.0F;
    rgb(0, 0, 2) = 0.0F;
    rgb(0, 1, 0) = 1.0F;
    rgb(0, 1, 1) = 0.0F;
    rgb(0, 1, 2) = 0.0F;
    std::fill(frame.alpha_hint.view().data.begin(), frame.alpha_hint.view().data.end(), 0.9985F);

    auto source = resolve_alpha_hint_source(frame, nullptr, AlphaHintPolicy::AutoRoughFallback);

    REQUIRE(source.has_value());
    CHECK(*source == AdobeAlphaHintSource::RoughFallback);
    CHECK(frame.alpha_hint.view()(0, 0) < frame.alpha_hint.view()(0, 1));
}

TEST_CASE("adobe bridge accepts varied source alpha as an implicit alpha hint",
          "[unit][adobe][runtime][alpha-hint]") {
    AdobeRuntimeFrame frame;
    frame.rgb = ImageBuffer(3, 1, 3);
    frame.alpha_hint = ImageBuffer(3, 1, 1);
    auto alpha = frame.alpha_hint.view();
    alpha(0, 0) = 1.0F;
    alpha(0, 1) = 0.0F;
    alpha(0, 2) = 0.5F;

    auto source = resolve_alpha_hint_source(frame, nullptr, AlphaHintPolicy::AutoRoughFallback);

    REQUIRE(source.has_value());
    CHECK(*source == AdobeAlphaHintSource::SourceAlpha);
    CHECK(frame.alpha_hint.view()(0, 0) == Catch::Approx(1.0F));
    CHECK(frame.alpha_hint.view()(0, 1) == Catch::Approx(0.0F));
    CHECK(frame.alpha_hint.view()(0, 2) == Catch::Approx(0.5F));
}

TEST_CASE("adobe bridge blocks Require External Hint when only opaque source alpha exists",
          "[unit][adobe][runtime][alpha-hint][regression]") {
    AdobeRuntimeFrame frame;
    frame.rgb = ImageBuffer(1, 1, 3);
    frame.alpha_hint = ImageBuffer(1, 1, 1);
    frame.alpha_hint.view()(0, 0) = 1.0F;

    auto source = resolve_alpha_hint_source(frame, nullptr, AlphaHintPolicy::RequireExternalHint);

    REQUIRE_FALSE(source.has_value());
    CHECK(source.error().code == ErrorCode::InvalidParameters);
    CHECK(source.error().message.find("Alpha Hint Layer") != std::string::npos);
}

TEST_CASE("adobe bridge rejects unsupported pixel formats and undersized views",
          "[unit][adobe][runtime][regression]") {
    std::array<std::uint8_t, 8> pixels{};

    auto unsupported = copy_adobe_frame_to_runtime(AdobeFrameView{
        .data = pixels.data(),
        .data_size_bytes = pixels.size(),
        .width = 1,
        .height = 1,
        .row_bytes = 4,
        .pixel_format = static_cast<AdobePixelFormat>(255),
    });
    REQUIRE_FALSE(unsupported.has_value());
    CHECK(unsupported.error().code == ErrorCode::InvalidParameters);
    CHECK(unsupported.error().message.find("Unsupported Adobe pixel format") != std::string::npos);

    auto short_row = copy_adobe_frame_to_runtime(AdobeFrameView{
        .data = pixels.data(),
        .data_size_bytes = pixels.size(),
        .width = 1,
        .height = 1,
        .row_bytes = 3,
        .pixel_format = AdobePixelFormat::Argb32,
    });
    REQUIRE_FALSE(short_row.has_value());
    CHECK(short_row.error().message.find("row bytes") != std::string::npos);

    auto short_buffer = copy_adobe_frame_to_runtime(AdobeFrameView{
        .data = pixels.data(),
        .data_size_bytes = 7,
        .width = 1,
        .height = 2,
        .row_bytes = 4,
        .pixel_format = AdobePixelFormat::Argb32,
    });
    REQUIRE_FALSE(short_buffer.has_value());
    CHECK(short_buffer.error().message.find("buffer") != std::string::npos);
}

TEST_CASE("adobe bridge rejects oversized host frames before allocation",
          "[unit][adobe][runtime][regression]") {
    std::array<std::uint8_t, 4> pixels{};

    auto converted = copy_adobe_frame_to_runtime(AdobeFrameView{
        .data = pixels.data(),
        .data_size_bytes = 0,
        .width = 8193,
        .height = 1,
        .row_bytes = 8193 * 4,
        .pixel_format = AdobePixelFormat::Argb32,
    });

    REQUIRE_FALSE(converted.has_value());
    CHECK(converted.error().code == ErrorCode::InvalidParameters);
    CHECK(converted.error().message.find("dimensions") != std::string::npos);

    converted = copy_adobe_frame_to_runtime(AdobeFrameView{
        .data = pixels.data(),
        .data_size_bytes = 0,
        .width = 8192,
        .height = 4321,
        .row_bytes = 8192 * 4,
        .pixel_format = AdobePixelFormat::Argb32,
    });

    REQUIRE_FALSE(converted.has_value());
    CHECK(converted.error().code == ErrorCode::InvalidParameters);
    CHECK(converted.error().message.find("pixel count") != std::string::npos);
}

TEST_CASE("adobe bridge writes runtime processed output as linear straight-alpha ARGB32",
          "[unit][adobe][runtime]") {
    FrameResult result;
    result.alpha = ImageBuffer(2, 1, 1);
    result.foreground = ImageBuffer(2, 1, 3);
    auto alpha = result.alpha.view();
    auto foreground = result.foreground.view();
    alpha(0, 0) = 0.5F;
    alpha(0, 1) = 1.0F;
    foreground(0, 0, 0) = 1.0F;
    foreground(0, 0, 1) = 0.5F;
    foreground(0, 0, 2) = 0.0F;
    foreground(0, 1, 0) = 0.25F;
    foreground(0, 1, 1) = 0.75F;
    foreground(0, 1, 2) = 1.0F;

    std::array<std::uint8_t, 8> output{};
    auto write_status =
        copy_runtime_result_to_adobe_frame(result,
                                           AdobeMutableFrameView{
                                               .data = output.data(),
                                               .data_size_bytes = output.size(),
                                               .width = 2,
                                               .height = 1,
                                               .row_bytes = 8,
                                               .pixel_format = AdobePixelFormat::Argb32,
                                           },
                                           0);

    REQUIRE(write_status.has_value());
    const auto& lut = SrgbLut::instance();
    CHECK(output == std::array<std::uint8_t, 8>{
                        quantize_adobe_argb8(0.5F),
                        quantize_adobe_argb8(lut.to_linear(1.0F)),
                        quantize_adobe_argb8(lut.to_linear(0.5F)),
                        quantize_adobe_argb8(lut.to_linear(0.0F)),
                        quantize_adobe_argb8(1.0F),
                        quantize_adobe_argb8(lut.to_linear(0.25F)),
                        quantize_adobe_argb8(lut.to_linear(0.75F)),
                        quantize_adobe_argb8(lut.to_linear(1.0F)),
                    });
}

TEST_CASE("adobe bridge writes matte-only output without foreground buffers",
          "[unit][adobe][runtime]") {
    FrameResult result;
    result.alpha = ImageBuffer(1, 1, 1);
    auto alpha = result.alpha.view();
    alpha(0, 0) = 0.25F;

    std::array<std::uint16_t, 4> output{};
    auto write_status = copy_runtime_result_to_adobe_frame(
        result,
        AdobeMutableFrameView{
            .data = output.data(),
            .data_size_bytes = output.size() * sizeof(std::uint16_t),
            .width = 1,
            .height = 1,
            .row_bytes = 8,
            .pixel_format = AdobePixelFormat::Argb64,
        },
        1);

    REQUIRE(write_status.has_value());
    CHECK(output == std::array<std::uint16_t, 4>{32768, 8192, 8192, 8192});
}

TEST_CASE("adobe bridge writes processed output as linear straight-alpha ARGB128",
          "[unit][adobe][runtime]") {
    FrameResult result;
    result.alpha = ImageBuffer(1, 1, 1);
    result.foreground = ImageBuffer(1, 1, 3);
    auto alpha = result.alpha.view();
    auto foreground = result.foreground.view();
    alpha(0, 0) = 0.25F;
    foreground(0, 0, 0) = 1.0F;
    foreground(0, 0, 1) = 0.5F;
    foreground(0, 0, 2) = 0.0F;

    std::array<float, 4> output{};
    auto write_status =
        copy_runtime_result_to_adobe_frame(result,
                                           AdobeMutableFrameView{
                                               .data = output.data(),
                                               .data_size_bytes = output.size() * sizeof(float),
                                               .width = 1,
                                               .height = 1,
                                               .row_bytes = 16,
                                               .pixel_format = AdobePixelFormat::Argb128,
                                           },
                                           0);

    REQUIRE(write_status.has_value());
    const auto& lut = SrgbLut::instance();
    CHECK(output[0] == Catch::Approx(0.25F));
    CHECK(output[1] == Catch::Approx(lut.to_linear(1.0F)));
    CHECK(output[2] == Catch::Approx(lut.to_linear(0.5F)));
    CHECK(output[3] == Catch::Approx(0.0F));
}

TEST_CASE("adobe bridge writes source matte output as linear straight-alpha ARGB128",
          "[unit][adobe][runtime][regression]") {
    FrameResult result;
    result.alpha = ImageBuffer(1, 1, 1);
    auto alpha = result.alpha.view();
    alpha(0, 0) = 0.25F;

    AdobeRuntimeFrame source_frame;
    source_frame.rgb = ImageBuffer(1, 1, 3);
    auto source = source_frame.rgb.view();
    source(0, 0, 0) = 0.25F;
    source(0, 0, 1) = 0.5F;
    source(0, 0, 2) = 1.0F;

    std::array<float, 4> output{};
    auto write_status =
        copy_runtime_result_to_adobe_frame(result,
                                           AdobeMutableFrameView{
                                               .data = output.data(),
                                               .data_size_bytes = output.size() * sizeof(float),
                                               .width = 1,
                                               .height = 1,
                                               .row_bytes = 16,
                                               .pixel_format = AdobePixelFormat::Argb128,
                                           },
                                           3, &source_frame);

    REQUIRE(write_status.has_value());
    const auto& lut = SrgbLut::instance();
    CHECK(output[0] == Catch::Approx(0.25F));
    CHECK(output[1] == Catch::Approx(lut.to_linear(0.25F)));
    CHECK(output[2] == Catch::Approx(lut.to_linear(0.5F)));
    CHECK(output[3] == Catch::Approx(lut.to_linear(1.0F)));
}

TEST_CASE("adobe bridge writes foreground-only output into BGRA32 frames",
          "[unit][adobe][runtime]") {
    FrameResult result;
    result.alpha = ImageBuffer(1, 1, 1);
    result.foreground = ImageBuffer(1, 1, 3);
    auto alpha = result.alpha.view();
    auto foreground = result.foreground.view();
    alpha(0, 0) = 0.1F;
    foreground(0, 0, 0) = 1.0F;
    foreground(0, 0, 1) = 0.5F;
    foreground(0, 0, 2) = 0.0F;

    std::array<std::uint8_t, 4> output{};
    auto write_status =
        copy_runtime_result_to_adobe_frame(result,
                                           AdobeMutableFrameView{
                                               .data = output.data(),
                                               .data_size_bytes = output.size(),
                                               .width = 1,
                                               .height = 1,
                                               .row_bytes = 4,
                                               .pixel_format = AdobePixelFormat::Bgra32,
                                           },
                                           2);

    REQUIRE(write_status.has_value());
    CHECK(output == std::array<std::uint8_t, 4>{0, 128, 255, 255});
}

TEST_CASE("adobe bridge rejects unknown output modes", "[unit][adobe][runtime][regression]") {
    FrameResult result;
    result.alpha = ImageBuffer(1, 1, 1);
    result.foreground = ImageBuffer(1, 1, 3);

    std::array<std::uint8_t, 4> output{};
    auto write_status =
        copy_runtime_result_to_adobe_frame(result,
                                           AdobeMutableFrameView{
                                               .data = output.data(),
                                               .data_size_bytes = output.size(),
                                               .width = 1,
                                               .height = 1,
                                               .row_bytes = 4,
                                               .pixel_format = AdobePixelFormat::Argb32,
                                           },
                                           99);

    REQUIRE_FALSE(write_status.has_value());
    CHECK(write_status.error().code == ErrorCode::InvalidParameters);
    CHECK(write_status.error().message.find("output mode") != std::string::npos);
}

TEST_CASE("adobe bridge builds prepare requests with Adobe-scoped session identity",
          "[unit][adobe][runtime]") {
    auto options = make_prepare_options();
    options.node_identity = "blue";
    options.client_instance_id = "layer-7";
    options.requested_device = DeviceInfo{"RTX 4090", 24576, Backend::TorchTRT};
    options.engine_options.allow_cpu_fallback = false;
    options.engine_options.disable_cpu_ep_fallback = true;

    auto request = build_adobe_prepare_session_request(options);

    REQUIRE(request.has_value());
    CHECK(request->client_instance_id == "adobe:after_effects:com.corridorkey.effect:layer-7");
    CHECK(request->node_identity == "adobe:after_effects:com.corridorkey.effect:blue");
    CHECK(request->model_path == options.model_path);
    CHECK(request->artifact_name == "corridorkey_dynamic_blue_fp16.ts");
    CHECK(request->requested_device.backend == Backend::TorchTRT);
    CHECK_FALSE(request->engine_options.allow_cpu_fallback);
    CHECK(request->engine_options.disable_cpu_ep_fallback);
    CHECK(request->requested_quality_mode == 3);
    CHECK(request->requested_resolution == 2048);
    CHECK(request->effective_resolution == 1536);
    CHECK(request->prepare_timeout_ms == 45000);
}

TEST_CASE("adobe bridge escapes prepare identity components", "[unit][adobe][runtime]") {
    auto options = make_prepare_options();
    options.node_identity = "node:blue";
    options.host_surface = "after:effects";
    options.effect_identity = "com%corridorkey:effect";
    options.client_instance_id = "layer:7";

    auto request = build_adobe_prepare_session_request(options);

    REQUIRE(request.has_value());
    CHECK(request->node_identity == "adobe:after%3Aeffects:com%25corridorkey%3Aeffect:node%3Ablue");
    CHECK(request->client_instance_id ==
          "adobe:after%3Aeffects:com%25corridorkey%3Aeffect:layer%3A7");
}

TEST_CASE("adobe bridge rejects invalid prepare option ranges",
          "[unit][adobe][runtime][regression]") {
    const auto expect_invalid = [](AdobePrepareSessionOptions options,
                                   const std::string& expected_message) {
        auto request = build_adobe_prepare_session_request(options);
        REQUIRE_FALSE(request.has_value());
        CHECK(request.error().code == ErrorCode::InvalidParameters);
        CHECK(request.error().message.find(expected_message) != std::string::npos);
    };

    auto options = make_prepare_options();
    options.requested_quality_mode = -1;
    expect_invalid(options, "quality");

    options = make_prepare_options();
    options.requested_quality_mode = 5;
    expect_invalid(options, "quality");

    options = make_prepare_options();
    options.requested_resolution = -1;
    expect_invalid(options, "resolution");

    options = make_prepare_options();
    options.effective_resolution = 2049;
    expect_invalid(options, "resolution");

    options = make_prepare_options();
    options.prepare_timeout_ms = -1;
    expect_invalid(options, "timeout");

    options = make_prepare_options();
    options.prepare_timeout_ms = 600001;
    expect_invalid(options, "timeout");
}

TEST_CASE("adobe runtime bridge operations return Result errors without a client",
          "[unit][adobe][runtime]") {
    AdobeRuntimeBridge bridge(std::unique_ptr<app::HostPluginRuntimeClient>{});
    AdobePrepareSessionOptions options;
    options.host_surface = "premiere";
    options.effect_identity = "com.corridorkey.effect";
    options.model_path = "models/corridorkey_fp16_1024.onnx";

    auto health = bridge.health();
    auto prepare = bridge.prepare_session(options);
    auto release = bridge.release_session();

    REQUIRE_FALSE(health.has_value());
    REQUIRE_FALSE(prepare.has_value());
    REQUIRE_FALSE(release.has_value());
    CHECK(health.error().message.find("runtime client") != std::string::npos);
    CHECK(prepare.error().message.find("runtime client") != std::string::npos);
    CHECK(release.error().message.find("runtime client") != std::string::npos);
}

TEST_CASE("adobe runtime client cache reuses one client for a sequence key",
          "[unit][adobe][runtime][regression]") {
    const std::uint64_t key = next_adobe_runtime_client_key();
    app::HostPluginRuntimeClientOptions options;

    auto first = acquire_cached_adobe_runtime_client(key, options);
    auto second = acquire_cached_adobe_runtime_client(key, options);

    REQUIRE(first.has_value());
    REQUIRE(second.has_value());
    CHECK(first->get() == second->get());

    release_cached_adobe_runtime_client(key);

    auto third = acquire_cached_adobe_runtime_client(key, options);
    REQUIRE(third.has_value());
    CHECK(third->get() != first->get());

    release_cached_adobe_runtime_client(key);
}
