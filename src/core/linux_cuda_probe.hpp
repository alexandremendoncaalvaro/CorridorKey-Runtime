#pragma once

#include <corridorkey/api_export.hpp>
#include <corridorkey/types.hpp>
#include <cstdint>
#include <string>
#include <vector>

namespace corridorkey::core {

struct LinuxGpuInfo {
    std::string adapter_name = "";
    std::string driver_version = "";
    int64_t dedicated_memory_mb = 0;
    int compute_capability_major = 0;
    int compute_capability_minor = 0;
    bool cuda_available = false;
    bool is_rtx = false;
};

// Enumerate NVIDIA GPUs through NVML (libnvidia-ml.so.1). Returns an empty
// vector when the NVML library is unavailable or returns no devices, which
// is the same signal the caller uses to fall back to the CPU path.
CORRIDORKEY_API std::vector<LinuxGpuInfo> list_linux_gpus();
CORRIDORKEY_API bool cuda_provider_available_linux();

}  // namespace corridorkey::core
