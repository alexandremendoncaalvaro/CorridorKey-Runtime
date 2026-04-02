#include <algorithm>
#include <catch2/catch_all.hpp>
#include <corridorkey/engine.hpp>
#include <filesystem>

#include "core/torch_trt_session.hpp"

using namespace corridorkey;

namespace {

int count_stage_samples(const std::vector<StageTiming>& timings, const std::string& name) {
    return static_cast<int>(
        std::count_if(timings.begin(), timings.end(),
                      [&](const StageTiming& timing) { return timing.name == name; }));
}

std::uint64_t sum_stage_work_units(const std::vector<StageTiming>& timings,
                                   const std::string& name) {
    std::uint64_t total = 0;
    for (const auto& timing : timings) {
        if (timing.name == name) {
            total += timing.work_units;
        }
    }
    return total;
}

}  // namespace

TEST_CASE("Tiled inference preserves input resolution", "[integration][tiling]") {
    if (!core::torch_tensorrt_runtime_available()) {
        SKIP("TorchTRT runtime not available");
    }
    auto model_path =
        std::filesystem::path(PROJECT_ROOT) / "models" / "corridorkey_fp16_512_trt.ts";
    if (!std::filesystem::exists(model_path)) {
        SKIP("TorchTRT model not found");
    }

    DeviceInfo device{"TensorRT", 0, Backend::TensorRT};
    auto engine_res = Engine::create(model_path, device);
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

TEST_CASE("Tiled CPU inference batches tiles when batch size allows it", "[integration][tiling]") {
    if (!core::torch_tensorrt_runtime_available()) {
        SKIP("TorchTRT runtime not available");
    }
    auto model_path =
        std::filesystem::path(PROJECT_ROOT) / "models" / "corridorkey_fp16_512_trt.ts";
    if (!std::filesystem::exists(model_path)) {
        SKIP("TorchTRT model not found");
    }

    DeviceInfo device{"TensorRT", 0, Backend::TensorRT};
    auto engine_res = Engine::create(model_path, device);
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
    params.batch_size = 2;

    std::vector<StageTiming> timings;
    auto result =
        engine->process_frame(rgb.view(), hint.view(), params,
                              [&](const StageTiming& timing) { timings.push_back(timing); });
    REQUIRE(result.has_value());

    REQUIRE(count_stage_samples(timings, "tile_infer") == 2);
    REQUIRE(sum_stage_work_units(timings, "tile_infer") == 4);
}
