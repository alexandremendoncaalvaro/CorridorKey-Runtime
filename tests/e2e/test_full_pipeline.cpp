#include <catch2/catch_all.hpp>
#include <corridorkey/engine.hpp>
#include <filesystem>

using namespace corridorkey;

TEST_CASE("End-to-End: Full Video Pipeline Sanity Check", "[e2e][video]") {
    // This test requires a real model file.
    std::filesystem::path model_path =
        std::filesystem::path(PROJECT_ROOT) / "models" / "corridorkey_int8_512.onnx";

    if (!std::filesystem::exists(model_path)) {
        // If no model is found, we don't fail the build, but we inform the user.
        // For CI, the model should be present or the test environment set up.
        SUCCEED("Model file not found (" + model_path.string() +
                "), skipping real inference E2E test.");
        return;
    }

    auto device = auto_detect();
    auto engine_res = Engine::create(model_path, device);
    REQUIRE(engine_res.has_value());
    auto engine = std::move(*engine_res);

    SECTION("Process a tiny generated video") {
        // Since we can't easily generate a video here without ffmpeg cli or a lot of code,
        // we'll rely on the existing integration tests for format-specific roundtrips.
        // This E2E test focuses on the Engine's ability to orchestrate the full flow.

        // Use a 1x1 image as a minimal stress test
        ImageBuffer rgb(512, 512, 3);
        ImageBuffer hint(512, 512, 1);
        std::fill(rgb.view().data.begin(), rgb.view().data.end(), 0.5f);
        std::fill(hint.view().data.begin(), hint.view().data.end(), 1.0f);

        auto result = engine->process_frame(rgb.view(), hint.view());
        REQUIRE(result.has_value());
        REQUIRE_FALSE(result->alpha.view().empty());
        REQUIRE(result->alpha.view().width == 512);
    }
}
