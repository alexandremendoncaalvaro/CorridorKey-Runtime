#include <catch2/catch_all.hpp>

#include "core/windows_rtx_probe.hpp"

TEST_CASE("DirectML provider availability is reflected in Windows GPU list",
          "[integration][windows]") {
#if defined(_WIN32)
    auto gpus = corridorkey::core::list_windows_gpus();
    if (gpus.empty()) {
        SUCCEED();
        return;
    }

    if (!corridorkey::core::directml_provider_available()) {
        SUCCEED();
        return;
    }

    bool any_directml = false;
    for (const auto& gpu : gpus) {
        if (gpu.directml_available) {
            any_directml = true;
            break;
        }
    }
    REQUIRE(any_directml);
#else
    SUCCEED();
#endif
}
