#include <corridorkey/engine.hpp>

#if defined(__APPLE__)
#include <sys/sysctl.h>
#include <sys/types.h>
#elif defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace corridorkey {

DeviceInfo auto_detect() {
    DeviceInfo device;
    device.backend = Backend::CPU;  // Default fallback
    device.available_memory_mb = 0;

#if defined(__APPLE__)
    int is_arm64 = 0;
    size_t arm64_size = sizeof(is_arm64);
    bool apple_silicon =
        sysctlbyname("hw.optional.arm64", &is_arm64, &arm64_size, NULL, 0) == 0 && is_arm64 == 1;

    char model[256];
    size_t size = sizeof(model);
    if (sysctlbyname("hw.model", model, &size, NULL, 0) == 0) {
        if (apple_silicon) {
            device.name = std::string("Apple Silicon (") + model + ")";
            device.backend = Backend::CoreML;
        } else {
            device.name = std::string("Mac (") + model + ")";
        }
    } else if (apple_silicon) {
        device.name = "Apple Silicon";
        device.backend = Backend::CoreML;
    } else {
        device.name = "Mac";
    }

    uint64_t mem;
    size = sizeof(mem);
    if (sysctlbyname("hw.memsize", &mem, &size, NULL, 0) == 0) {
        device.available_memory_mb = static_cast<int64_t>(mem / (1024 * 1024));
    }
#elif defined(_WIN32)
    device.name = "Windows CPU Baseline";
    device.backend = Backend::CPU;
    MEMORYSTATUSEX status;
    status.dwLength = sizeof(status);
    GlobalMemoryStatusEx(&status);
    device.available_memory_mb = static_cast<int64_t>(status.ullTotalPhys / (1024 * 1024));
#else
    device.name = "Linux/Generic Device";
    // Check for NVIDIA via a lightweight check (simplified for now)
    device.backend = Backend::CPU;
    long pages = sysconf(_SC_PHYS_PAGES);
    long page_size = sysconf(_SC_PAGE_SIZE);
    device.available_memory_mb = static_cast<int64_t>((pages * page_size) / (1024 * 1024));
#endif

    return device;
}

std::vector<DeviceInfo> list_devices() {
    std::vector<DeviceInfo> devices;
    devices.push_back(auto_detect());

    // Always include CPU as a fallback option
    if (devices.back().backend != Backend::CPU) {
        devices.push_back({"Generic CPU", devices.back().available_memory_mb, Backend::CPU});
    }

    return devices;
}

}  // namespace corridorkey
