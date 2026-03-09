#include <corridorkey/engine.hpp>

#if defined(__APPLE__)
    #include <sys/types.h>
    #include <sys/sysctl.h>
#elif defined(_WIN32)
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
#else
    #include <unistd.h>
#endif

namespace corridorkey {

DeviceInfo auto_detect() {
    DeviceInfo device;
    device.backend = Backend::CPU; // Default fallback
    device.available_memory_mb = 0;

#if defined(__APPLE__)
    // Detect Apple Silicon for CoreML
    char model[256];
    size_t size = sizeof(model);
    if (sysctlbyname("hw.model", model, &size, NULL, 0) == 0) {
        device.name = std::string("Apple Silicon (") + model + ")";
        device.backend = Backend::CoreML;
    }

    uint64_t mem;
    size = sizeof(mem);
    if (sysctlbyname("hw.memsize", &mem, &size, NULL, 0) == 0) {
        device.available_memory_mb = static_cast<int64_t>(mem / (1024 * 1024));
    }
#elif defined(_WIN32)
    device.name = "Windows Device";
    device.backend = Backend::DirectML;
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
        devices.push_back({ "Generic CPU", devices.back().available_memory_mb, Backend::CPU });
    }
    
    return devices;
}

} // namespace corridorkey
