#include <array>
#include <catch2/catch_all.hpp>
#include <cstdint>
#include <memory>

#include "plugins/adobe/adobe_bridge.hpp"

using namespace corridorkey;
using namespace corridorkey::adobe;

TEST_CASE("adobe bridge copies ARGB32 frames with row padding into runtime buffers",
          "[unit][adobe][runtime]") {
    constexpr int width = 2;
    constexpr int height = 2;
    constexpr int row_bytes = 12;
    std::array<std::uint8_t, 24> pixels{
        255, 10,  20,  30,  128, 40, 50, 60,  0, 0, 0, 0,
        64,  70,  80,  90,  0,   1,  2,  3,   0, 0, 0, 0,
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
        32768, 16384, 8192, 4096, 0, 0, 0, 0,
        16384, 32768, 0,    8192, 0, 0, 0, 0,
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
        0.25F, 0.5F, 1.25F, -0.5F,
        1.0F,  0.0F, 0.75F, 2.0F,
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
        5, 10, 20, 40,
        200, 150, 100, 255,
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
    CHECK(unsupported.error().message.find("Unsupported Adobe pixel format") !=
          std::string::npos);

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

TEST_CASE("adobe bridge builds prepare requests with Adobe-scoped session identity",
          "[unit][adobe][runtime]") {
    AdobePrepareSessionOptions options;
    options.host_surface = "after_effects";
    options.effect_identity = "com.corridorkey.effect";
    options.client_instance_id = "layer-7";
    options.model_path = "models/corridorkey_dynamic_blue_fp16.ts";
    options.requested_device = DeviceInfo{"RTX 4090", 24576, Backend::TorchTRT};
    options.engine_options.allow_cpu_fallback = false;
    options.engine_options.disable_cpu_ep_fallback = true;
    options.requested_quality_mode = 3;
    options.requested_resolution = 2048;
    options.effective_resolution = 1536;
    options.prepare_timeout_ms = 45000;

    auto request = build_adobe_prepare_session_request(options);

    REQUIRE(request.has_value());
    CHECK(request->client_instance_id ==
          "adobe:after_effects:com.corridorkey.effect:layer-7");
    CHECK(request->node_identity == "adobe:after_effects:com.corridorkey.effect");
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
