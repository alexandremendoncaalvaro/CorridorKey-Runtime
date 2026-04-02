#include <catch2/catch_all.hpp>
#include <corridorkey/engine.hpp>
#include <filesystem>
#include <optional>
#include <vector>

#include "../test_model_artifact_utils.hpp"

using namespace corridorkey;

TEST_CASE("engine strict create rejects GPU requests that only succeed via CPU fallback",
          "[integration][engine][regression]") {
#if !defined(_WIN32)
    SKIP("Windows-only fallback policy test");
#else
    const std::filesystem::path model_path = "models/corridorkey_int8_512.onnx";
    if (auto reason = corridorkey::tests::unusable_model_artifact_reason(model_path);
        reason.has_value()) {
        SKIP(*reason);
    }

    const std::vector<DeviceInfo> fallback_candidates = {
        DeviceInfo{"Windows AI", 0, Backend::WindowsML},
        DeviceInfo{"OpenVINO", 0, Backend::OpenVINO},
    };

    std::optional<DeviceInfo> requested_device;
    std::optional<BackendFallbackInfo> fallback_info;

    for (const auto& candidate : fallback_candidates) {
        auto tolerant_engine = Engine::create(model_path, candidate);
        if (!tolerant_engine) {
            continue;
        }
        if ((*tolerant_engine)->current_device().backend != Backend::CPU) {
            continue;
        }

        requested_device = candidate;
        fallback_info = (*tolerant_engine)->backend_fallback();
        break;
    }

    if (!requested_device.has_value()) {
        SKIP("No GPU-to-CPU fallback scenario available on this machine");
    }

    REQUIRE(fallback_info.has_value());
    REQUIRE(fallback_info->requested_backend == requested_device->backend);
    REQUIRE(fallback_info->selected_backend == Backend::CPU);

    EngineCreateOptions strict_options;
    strict_options.allow_cpu_fallback = false;
    strict_options.disable_cpu_ep_fallback = true;

    auto strict_engine = Engine::create(model_path, *requested_device, nullptr, strict_options);
    REQUIRE_FALSE(strict_engine.has_value());
#endif
}
