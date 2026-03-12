#pragma once

#include <corridorkey/types.hpp>
#include <optional>
#include <string>

namespace corridorkey::core {

struct WindowsRtxGpuInfo {
    std::string adapter_name = "";
    std::string driver_version = "";
    int64_t dedicated_memory_mb = 0;
    bool driver_query_available = false;
    bool provider_available = false;
    bool ampere_or_newer = false;
};

std::optional<WindowsRtxGpuInfo> probe_windows_rtx_gpu();
bool tensorrt_rtx_provider_available();

}  // namespace corridorkey::core
