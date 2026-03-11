#include <catch2/catch_all.hpp>
#include <corridorkey/engine.hpp>
#include <corridorkey/frame_io.hpp>

#include "app/job_orchestrator.hpp"
#include "core/mlx_probe.hpp"

using namespace corridorkey;

TEST_CASE("mlx bridge executes a single frame through the runtime", "[integration][mlx]") {
#if !defined(__APPLE__)
    SUCCEED("MLX runtime execution is only applicable on macOS.");
#else
    if (!core::mlx_probe_available()) {
        SUCCEED("MLX runtime support is not linked in this build.");
        return;
    }

    const auto model_path =
        std::filesystem::path(PROJECT_ROOT) / "models" / "corridorkey_mlx_bridge_512.mlxfn";
    if (!std::filesystem::exists(model_path)) {
        SUCCEED("MLX bridge artifact is not available locally.");
        return;
    }

    ImageBuffer rgb(64, 64, 3);
    ImageBuffer hint(64, 64, 1);

    for (int y_pos = 0; y_pos < 64; ++y_pos) {
        for (int x_pos = 0; x_pos < 64; ++x_pos) {
            rgb.view()(y_pos, x_pos, 0) = 0.1F;
            rgb.view()(y_pos, x_pos, 1) = 0.8F;
            rgb.view()(y_pos, x_pos, 2) = 0.1F;
            hint.view()(y_pos, x_pos, 0) =
                x_pos > 20 && x_pos < 44 && y_pos > 20 && y_pos < 44 ? 1.0F : 0.0F;
        }
    }

    auto engine = Engine::create(model_path, DeviceInfo{"Apple Silicon MLX", 16000, Backend::MLX});
    REQUIRE(engine.has_value());
    REQUIRE(engine.value()->current_device().backend == Backend::MLX);
    REQUIRE(engine.value()->recommended_resolution() == 512);

    auto result = engine.value()->process_frame(rgb.view(), hint.view(), {});
    REQUIRE(result.has_value());
    REQUIRE(result->alpha.view().width == 64);
    REQUIRE(result->alpha.view().height == 64);
    REQUIRE(result->foreground.view().width == 64);
    REQUIRE(result->foreground.view().height == 64);
    REQUIRE(result->processed.view().width == 64);
    REQUIRE(result->processed.view().height == 64);
#endif
}

TEST_CASE("mlx safetensors pack prefers the lowest bridge resolution by default",
          "[integration][mlx]") {
#if !defined(__APPLE__)
    SUCCEED("MLX runtime execution is only applicable on macOS.");
#else
    if (!core::mlx_probe_available()) {
        SUCCEED("MLX runtime support is not linked in this build.");
        return;
    }

    const auto safetensors_path =
        std::filesystem::path(PROJECT_ROOT) / "models" / "corridorkey_mlx.safetensors";
    const auto bridge_512 =
        std::filesystem::path(PROJECT_ROOT) / "models" / "corridorkey_mlx_bridge_512.mlxfn";
    if (!std::filesystem::exists(safetensors_path) || !std::filesystem::exists(bridge_512)) {
        SUCCEED("MLX pack or bridge artifacts are not available locally.");
        return;
    }

    auto engine =
        Engine::create(safetensors_path, DeviceInfo{"Apple Silicon MLX", 16000, Backend::MLX});
    REQUIRE(engine.has_value());
    REQUIRE(engine.value()->current_device().backend == Backend::MLX);
    REQUIRE(engine.value()->recommended_resolution() == 512);
#endif
}

TEST_CASE("job orchestrator keeps auto resolution compatible with mlx bridge",
          "[integration][mlx][regression]") {
#if !defined(__APPLE__)
    SUCCEED("MLX runtime execution is only applicable on macOS.");
#else
    if (!core::mlx_probe_available()) {
        SUCCEED("MLX runtime support is not linked in this build.");
        return;
    }

    const auto model_path =
        std::filesystem::path(PROJECT_ROOT) / "models" / "corridorkey_mlx_bridge_512.mlxfn";
    if (!std::filesystem::exists(model_path)) {
        SUCCEED("MLX bridge artifact is not available locally.");
        return;
    }

    const auto temp_root = std::filesystem::path(PROJECT_ROOT) / "build" / "test_mlx_job";
    std::filesystem::create_directories(temp_root);
    const auto input_path = temp_root / "input.png";
    const auto output_path = temp_root / "output";

    ImageBuffer rgb(64, 64, 3);
    for (int y_pos = 0; y_pos < 64; ++y_pos) {
        for (int x_pos = 0; x_pos < 64; ++x_pos) {
            rgb.view()(y_pos, x_pos, 0) = 0.1F;
            rgb.view()(y_pos, x_pos, 1) = 0.8F;
            rgb.view()(y_pos, x_pos, 2) = 0.1F;
        }
    }

    auto write_res = frame_io::write_frame(input_path, rgb.view());
    REQUIRE(write_res.has_value());

    app::JobRequest request;
    request.input_path = input_path;
    request.output_path = output_path;
    request.model_path = model_path;
    request.device = DeviceInfo{"Apple Silicon MLX", 16000, Backend::MLX};
    request.params.target_resolution = 0;

    auto result = app::JobOrchestrator::run(request);
    REQUIRE(result.has_value());
    REQUIRE(std::filesystem::exists(output_path / "Comp" / "input.png"));
#endif
}
