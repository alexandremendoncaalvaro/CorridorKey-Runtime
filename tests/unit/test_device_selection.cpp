#include <catch2/catch_all.hpp>

#include "cli/device_selection.hpp"

using namespace corridorkey;

TEST_CASE("cli auto device selection preserves Backend::Auto", "[unit][cli]") {
    std::vector<DeviceInfo> devices = {
        {"Apple Silicon MLX", 16384, Backend::MLX},
        {"Generic CPU", 16384, Backend::CPU},
    };

    auto selected = cli::select_device(devices, "auto");

    REQUIRE(selected.backend == Backend::Auto);
    REQUIRE(selected.name == "Auto");
    REQUIRE(selected.available_memory_mb == 16384);
}

TEST_CASE("cli explicit device selection still resolves named backends", "[unit][cli]") {
    std::vector<DeviceInfo> devices = {
        {"Apple Silicon MLX", 16384, Backend::MLX},
        {"Generic CPU", 16384, Backend::CPU},
    };

    auto selected = cli::select_device(devices, "cpu");

    REQUIRE(selected.backend == Backend::CPU);
    REQUIRE(selected.name == "Generic CPU");
}

TEST_CASE("cli RTX aliases resolve to the TensorRT backend", "[unit][cli]") {
    std::vector<DeviceInfo> devices = {
        {"NVIDIA GeForce RTX 3080", 10240, Backend::TensorRT},
        {"Generic CPU", 0, Backend::CPU},
    };

    auto by_rtx = cli::select_device(devices, "rtx");
    REQUIRE(by_rtx.backend == Backend::TensorRT);

    auto by_trt = cli::select_device(devices, "trt");
    REQUIRE(by_trt.backend == Backend::TensorRT);
}
