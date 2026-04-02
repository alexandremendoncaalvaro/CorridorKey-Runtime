#include <corridorkey/engine.hpp>

#include "mlx_probe.hpp"
#include "torch_trt_session.hpp"

#if defined(__APPLE__)
#include <sys/sysctl.h>
#include <sys/types.h>
#elif defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <dxgi.h>
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

#if defined(_WIN32)
struct WindowsGpuInfo {
    std::string adapter_name;
    int64_t dedicated_memory_mb = 0;
    bool is_nvidia = false;
};

std::vector<WindowsGpuInfo> enumerate_dxgi_adapters() {
    std::vector<WindowsGpuInfo> gpus;
    IDXGIFactory1* factory = nullptr;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory)))) {
        return gpus;
    }

    IDXGIAdapter1* adapter = nullptr;
    for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
        DXGI_ADAPTER_DESC1 desc;
        if (SUCCEEDED(adapter->GetDesc1(&desc))) {
            if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
                adapter->Release();
                continue;
            }
            WindowsGpuInfo info;
            int len =
                WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, nullptr, 0, nullptr, nullptr);
            if (len > 0) {
                info.adapter_name.resize(len - 1);
                WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, info.adapter_name.data(), len,
                                    nullptr, nullptr);
            }
            info.dedicated_memory_mb =
                static_cast<int64_t>(desc.DedicatedVideoMemory / (1024 * 1024));
            info.is_nvidia = (desc.VendorId == 0x10DE);
            gpus.push_back(std::move(info));
        }
        adapter->Release();
    }
    factory->Release();
    return gpus;
}
#endif

}  // namespace

DeviceInfo auto_detect() {
    DeviceInfo device;
    device.backend = Backend::CPU;
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
    auto gpus = enumerate_dxgi_adapters();

    // Prefer NVIDIA GPU with Torch-TensorRT runtime available
    for (size_t i = 0; i < gpus.size(); ++i) {
        if (gpus[i].is_nvidia && core::torch_tensorrt_runtime_available()) {
            device.name = gpus[i].adapter_name;
            device.backend = Backend::TensorRT;
            device.available_memory_mb = gpus[i].dedicated_memory_mb;
            device.device_index = static_cast<int>(i);
            return device;
        }
    }

    // Fallback: first discrete GPU
    if (!gpus.empty()) {
        device.name = gpus[0].adapter_name;
        device.backend = Backend::CPU;
        device.available_memory_mb = gpus[0].dedicated_memory_mb;
        device.device_index = 0;
        return device;
    }

    // Last Resort: CPU
    MEMORYSTATUSEX status;
    status.dwLength = sizeof(status);
    GlobalMemoryStatusEx(&status);
    device.name = "Windows CPU Baseline";
    device.backend = Backend::CPU;
    device.available_memory_mb = static_cast<int64_t>(status.ullTotalPhys / (1024 * 1024));
#else
    device.name = "Linux/Generic Device";
    device.backend = Backend::CPU;
    long pages = sysconf(_SC_PHYS_PAGES);
    long page_size = sysconf(_SC_PAGE_SIZE);
    device.available_memory_mb = static_cast<int64_t>((pages * page_size) / (1024 * 1024));
#endif

    return device;
}

std::vector<DeviceInfo> list_devices() {
    std::vector<DeviceInfo> devices;
#if defined(__APPLE__)
    DeviceInfo detected = auto_detect();
    if (detect_apple_silicon() && core::mlx_probe_available()) {
        devices.push_back({"Apple Silicon MLX", detected.available_memory_mb, Backend::MLX});
    }
    devices.push_back({"Generic CPU", detected.available_memory_mb, Backend::CPU});
    if (detected.backend == Backend::CoreML) {
        devices.push_back(detected);
    }
#elif defined(_WIN32)
    auto gpus = enumerate_dxgi_adapters();
    bool has_torch_trt = core::torch_tensorrt_runtime_available();
    for (size_t i = 0; i < gpus.size(); ++i) {
        if (gpus[i].is_nvidia && has_torch_trt) {
            devices.push_back({gpus[i].adapter_name + " (TensorRT)", gpus[i].dedicated_memory_mb,
                               Backend::TensorRT, static_cast<int>(i)});
        }
    }
    devices.push_back({"Generic CPU", 0, Backend::CPU, 0});
#else
    devices.push_back(auto_detect());
#endif
    return devices;
}

}  // namespace corridorkey
