#include <catch2/catch_all.hpp>
#include <corridorkey/engine.hpp>

#include "core/mlx_probe.hpp"

using namespace corridorkey;

TEST_CASE("auto_detect returns valid device info", "[unit][device]") {
    auto device = auto_detect();

    REQUIRE_FALSE(device.name.empty());
    REQUIRE(device.available_memory_mb >= 0);
    REQUIRE(device.backend != Backend::Auto);
}

TEST_CASE("list_devices includes at least one device", "[unit][device]") {
    auto devices = list_devices();

    REQUIRE(devices.size() >= 1);

    // Should always include a CPU-capable device
    bool has_cpu = false;
    for (const auto& d : devices) {
        if (d.backend == Backend::CPU) {
            has_cpu = true;
        }
        REQUIRE_FALSE(d.name.empty());
    }

    // If the primary device is not CPU, there should be a CPU fallback
    if (devices.front().backend != Backend::CPU) {
        REQUIRE(has_cpu);
    }

#if defined(__APPLE__)
    if (core::mlx_probe_available()) {
        bool has_mlx = false;
        for (const auto& device : devices) {
            if (device.backend == Backend::MLX) {
                has_mlx = true;
                break;
            }
        }
        REQUIRE(has_mlx);
    }
#endif
}

TEST_CASE("arm64 macOS hardware detection still exposes CoreML capability", "[unit][device]") {
#if defined(__APPLE__) && (defined(__aarch64__) || defined(__arm64__))
    auto device = auto_detect();

    REQUIRE(device.backend == Backend::CoreML);
    REQUIRE(device.name.find("Apple Silicon") != std::string::npos);
#else
    SUCCEED("Apple Silicon-specific detection is not applicable on this build.");
#endif
}

TEST_CASE("apple device listing prioritizes operational backends over diagnostic ones",
          "[unit][device]") {
#if defined(__APPLE__)
    auto devices = list_devices();
    REQUIRE_FALSE(devices.empty());
    REQUIRE(devices.front().backend != Backend::CoreML);
#else
    SUCCEED("Apple ordering is not applicable on this build.");
#endif
}

TEST_CASE("windows device listing prefers TensorRT RTX when available", "[unit][device]") {
#if defined(_WIN32)
    auto devices = list_devices();
    REQUIRE_FALSE(devices.empty());

    bool has_cpu = false;
    bool has_tensorrt = false;
    for (const auto& device : devices) {
        has_cpu = has_cpu || device.backend == Backend::CPU;
        has_tensorrt = has_tensorrt || device.backend == Backend::TensorRT;
    }
    REQUIRE(has_cpu);

    if (has_tensorrt) {
        REQUIRE(devices.front().backend == Backend::TensorRT);
    }
#else
    SUCCEED("Windows TensorRT ordering is not applicable on this build.");
#endif
}
