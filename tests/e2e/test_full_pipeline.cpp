#include <catch2/catch_all.hpp>
#include <corridorkey/engine.hpp>
#include <filesystem>

#include "core/torch_trt_session.hpp"

using namespace corridorkey;

TEST_CASE("End-to-End: Full Video Pipeline Sanity Check", "[e2e][video]") {
    if (!core::torch_tensorrt_runtime_available()) {
        SKIP("TorchTRT runtime not available");
    }

    std::filesystem::path model_path =
        std::filesystem::path(PROJECT_ROOT) / "models" / "corridorkey_fp16_512_trt.ts";

    if (!std::filesystem::exists(model_path)) {
        SKIP("TorchTRT model not found (" + model_path.string() + ")");
    }

    DeviceInfo device{"TensorRT", 0, Backend::TensorRT};
    auto engine_res = Engine::create(model_path, device);
    REQUIRE(engine_res.has_value());
    auto engine = std::move(*engine_res);

    SECTION("Process a tiny generated video") {
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
