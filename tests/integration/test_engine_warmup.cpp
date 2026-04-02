#include <algorithm>
#include <catch2/catch_all.hpp>
#include <corridorkey/engine.hpp>
#include <filesystem>

#include "core/torch_trt_session.hpp"

using namespace corridorkey;

namespace {

bool has_stage(const std::vector<StageTiming>& timings, const std::string& name) {
    return std::any_of(timings.begin(), timings.end(),
                       [&](const StageTiming& timing) { return timing.name == name; });
}

}  // namespace

TEST_CASE("Engine warmup happens on first processing call", "[integration][engine]") {
    if (!core::torch_tensorrt_runtime_available()) {
        SKIP("TorchTRT runtime not available");
    }
    const std::filesystem::path model_path =
        std::filesystem::path(PROJECT_ROOT) / "models" / "corridorkey_fp16_512_trt.ts";
    if (!std::filesystem::exists(model_path)) {
        SKIP("Model not available");
    }

    std::vector<StageTiming> create_timings;
    auto create_res =
        Engine::create(model_path, DeviceInfo{"TensorRT", 0, Backend::TensorRT},
                       [&](const StageTiming& timing) { create_timings.push_back(timing); });
    REQUIRE(create_res.has_value());
    REQUIRE_FALSE(has_stage(create_timings, "engine_warmup"));

    ImageBuffer rgb_buf(32, 32, 3);
    ImageBuffer hint_buf(32, 32, 1);
    std::fill(rgb_buf.view().data.begin(), rgb_buf.view().data.end(), 0.0f);
    std::fill(hint_buf.view().data.begin(), hint_buf.view().data.end(), 0.0f);

    std::vector<StageTiming> run_timings;
    auto run_res =
        (*create_res)
            ->process_frame(rgb_buf.view(), hint_buf.view(), {},
                            [&](const StageTiming& timing) { run_timings.push_back(timing); });
    REQUIRE(run_res.has_value());
    REQUIRE(has_stage(run_timings, "engine_warmup"));
    REQUIRE(has_stage(run_timings, "torchtrt_run"));
}
