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
