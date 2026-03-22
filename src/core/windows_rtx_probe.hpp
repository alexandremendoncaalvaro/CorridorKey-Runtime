#pragma once

#include <corridorkey/api_export.hpp>
#include <corridorkey/types.hpp>
#include <optional>
#include <string>
#include <vector>

namespace corridorkey::core {

struct WindowsGpuInfo {
    std::string adapter_name = "";
    std::string driver_version = "";
    int64_t dedicated_memory_mb = 0;
    unsigned int vendor_id = 0;
    bool driver_query_available = false;
    bool tensorrt_rtx_available = false;
    bool cuda_available = false;
    bool directml_available = false;
    bool winml_available = false;
    bool openvino_available = false;
    bool is_rtx = false;
    int compute_capability_major = 0;
    int compute_capability_minor = 0;
};

CORRIDORKEY_API std::vector<WindowsGpuInfo> list_windows_gpus();
CORRIDORKEY_API bool tensorrt_rtx_provider_available();
CORRIDORKEY_API bool cuda_provider_available();
CORRIDORKEY_API bool directml_provider_available();
CORRIDORKEY_API bool winml_provider_available();
CORRIDORKEY_API bool openvino_provider_available();

}  // namespace corridorkey::core
