#include <corridorkey/engine.hpp>

#include "mlx_probe.hpp"
#include "windows_rtx_probe.hpp"

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

namespace {

bool compiled_for_apple_silicon() {
#if defined(__APPLE__) && (defined(__aarch64__) || defined(__arm64__))
    return true;
#else
    return false;
#endif
}

bool detect_apple_silicon() {
#if defined(__APPLE__)
    int is_arm64 = 0;
    size_t arm64_size = sizeof(is_arm64);
    if (sysctlbyname("hw.optional.arm64", &is_arm64, &arm64_size, NULL, 0) == 0) {
        return is_arm64 == 1;
    }
#endif
    return compiled_for_apple_silicon();
}

}  // namespace

DeviceInfo auto_detect() {
    DeviceInfo device;
    device.backend = Backend::CPU;  // Default fallback
    device.available_memory_mb = 0;

#if defined(__APPLE__)
    bool apple_silicon = detect_apple_silicon();

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
    MEMORYSTATUSEX status;
    status.dwLength = sizeof(status);
    GlobalMemoryStatusEx(&status);

    auto windows_rtx_gpu = core::probe_windows_rtx_gpu();
    if (windows_rtx_gpu.has_value() && windows_rtx_gpu->ampere_or_newer &&
        windows_rtx_gpu->provider_available) {
        device.name = windows_rtx_gpu->adapter_name;
        device.backend = Backend::TensorRT;
        device.available_memory_mb = windows_rtx_gpu->dedicated_memory_mb;
        return device;
    }

    device.name = "Windows CPU Baseline";
    device.backend = Backend::CPU;
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
    DeviceInfo detected = auto_detect();
#if defined(__APPLE__)
    if (detect_apple_silicon() && core::mlx_probe_available()) {
        devices.push_back({"Apple Silicon MLX", detected.available_memory_mb, Backend::MLX});
    }

    devices.push_back({"Generic CPU", detected.available_memory_mb, Backend::CPU});

    if (detected.backend == Backend::CoreML) {
        devices.push_back(detected);
    }
#elif defined(_WIN32)
    auto windows_rtx_gpu = core::probe_windows_rtx_gpu();
    if (windows_rtx_gpu.has_value() && windows_rtx_gpu->ampere_or_newer &&
        windows_rtx_gpu->provider_available) {
        devices.push_back({windows_rtx_gpu->adapter_name, windows_rtx_gpu->dedicated_memory_mb,
                           Backend::TensorRT});
    }

    devices.push_back({"Generic CPU", 0, Backend::CPU});
#else
    devices.push_back(detected);
    if (detected.backend != Backend::CPU) {
        devices.push_back({"Generic CPU", detected.available_memory_mb, Backend::CPU});
    }
#endif

    return devices;
}

}  // namespace corridorkey
