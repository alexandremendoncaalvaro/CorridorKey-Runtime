#include <catch2/catch_all.hpp>
#include <corridorkey/engine.hpp>

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
}

TEST_CASE("arm64 macOS builds prefer CoreML detection", "[unit][device]") {
#if defined(__APPLE__) && (defined(__aarch64__) || defined(__arm64__))
    auto device = auto_detect();

    REQUIRE(device.backend == Backend::CoreML);
    REQUIRE(device.name.find("Apple Silicon") != std::string::npos);
#else
    SUCCEED("Apple Silicon-specific detection is not applicable on this build.");
#endif
}
