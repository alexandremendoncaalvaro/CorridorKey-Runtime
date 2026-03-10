#include <catch2/catch_all.hpp>
#include <corridorkey/engine.hpp>
#include <filesystem>

using namespace corridorkey;

TEST_CASE("Tiled inference preserves input resolution", "[integration][tiling]") {
    auto model_path = std::filesystem::path(PROJECT_ROOT) / "models" / "corridorkey_int8_512.onnx";
    if (!std::filesystem::exists(model_path)) {
        SUCCEED("Model file not found, skipping tiled inference integration test.");
        return;
    }

    DeviceInfo cpu_device{"Generic CPU", 0, Backend::CPU};
    auto engine_res = Engine::create(model_path, cpu_device);
    REQUIRE(engine_res.has_value());
    auto engine = std::move(*engine_res);

    const int width = 520;
    const int height = 520;

    ImageBuffer rgb(width, height, 3);
    ImageBuffer hint(width, height, 1);
    std::fill(rgb.view().data.begin(), rgb.view().data.end(), 0.5f);
    std::fill(hint.view().data.begin(), hint.view().data.end(), 1.0f);

    InferenceParams params;
    params.target_resolution = 512;
    params.enable_tiling = true;
    params.tile_padding = 32;

    auto result = engine->process_frame(rgb.view(), hint.view(), params);
    REQUIRE(result.has_value());
    REQUIRE(result->alpha.view().width == width);
    REQUIRE(result->alpha.view().height == height);
    REQUIRE(result->foreground.view().width == width);
    REQUIRE(result->foreground.view().height == height);
    REQUIRE(result->processed.view().width == width);
    REQUIRE(result->composite.view().height == height);
}
