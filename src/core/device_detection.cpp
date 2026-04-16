#include <corridorkey/engine.hpp>

#include "linux_cuda_probe.hpp"
#include "mlx_probe.hpp"
#include "windows_rtx_probe.hpp"

#if defined(__APPLE__)
#include <sys/sysctl.h>
#include <sys/types.h>
#elif defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <sys/sysinfo.h>
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
    auto gpus = core::list_windows_gpus();

    // Tier 1: NVIDIA RTX (Best Performance)
    for (size_t i = 0; i < gpus.size(); ++i) {
        const auto& gpu = gpus[i];
        if (gpu.tensorrt_rtx_available) {
            device.name = gpu.adapter_name;
            device.backend = Backend::TensorRT;
            device.available_memory_mb = gpu.dedicated_memory_mb;
            device.device_index = static_cast<int>(i);
            return device;
        }
    }

    // Tier 2: Windows ML (Modern Universal / NPU)
    for (size_t i = 0; i < gpus.size(); ++i) {
        const auto& gpu = gpus[i];
        if (gpu.winml_available) {
            device.name = gpu.adapter_name + " (Windows AI)";
            device.backend = Backend::WindowsML;
            device.available_memory_mb = gpu.dedicated_memory_mb;
            device.device_index = static_cast<int>(i);
            return device;
        }
    }

    // Tier 3: Intel OpenVINO (NPU/iGPU)
    for (size_t i = 0; i < gpus.size(); ++i) {
        const auto& gpu = gpus[i];
        if (gpu.openvino_available) {
            device.name = gpu.adapter_name + " (OpenVINO)";
            device.backend = Backend::OpenVINO;
            device.available_memory_mb = gpu.dedicated_memory_mb;
            device.device_index = static_cast<int>(i);
            return device;
        }
    }

    // Tier 4: NVIDIA GTX (CUDA Fallback)
    for (size_t i = 0; i < gpus.size(); ++i) {
        const auto& gpu = gpus[i];
        if (gpu.cuda_available) {
            device.name = gpu.adapter_name + " (CUDA)";
            device.backend = Backend::CUDA;
            device.available_memory_mb = gpu.dedicated_memory_mb;
            device.device_index = static_cast<int>(i);
            return device;
        }
    }

    // Tier 5: AMD / Universal (DirectML Fallback)
    for (size_t i = 0; i < gpus.size(); ++i) {
        const auto& gpu = gpus[i];
        if (gpu.directml_available) {
            device.name = gpu.adapter_name + " (DirectML)";
            device.backend = Backend::DirectML;
            device.available_memory_mb = gpu.dedicated_memory_mb;
            device.device_index = static_cast<int>(i);
            return device;
        }
    }

    // Last Resort: CPU
    MEMORYSTATUSEX status;
    status.dwLength = sizeof(status);
    GlobalMemoryStatusEx(&status);
    device.name = "Windows CPU Baseline";
    device.backend = Backend::CPU;
    device.available_memory_mb = static_cast<int64_t>(status.ullTotalPhys / (1024 * 1024));
#else
    auto linux_gpus = core::list_linux_gpus();
    for (size_t i = 0; i < linux_gpus.size(); ++i) {
        const auto& gpu = linux_gpus[i];
        if (gpu.cuda_available) {
            device.name = gpu.adapter_name + " (CUDA)";
            device.backend = Backend::CUDA;
            device.available_memory_mb = gpu.dedicated_memory_mb;
            device.device_index = static_cast<int>(i);
            return device;
        }
    }

    device.name = "Linux CPU Baseline";
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
    auto gpus = core::list_windows_gpus();
    for (size_t i = 0; i < gpus.size(); ++i) {
        const auto& gpu = gpus[i];
        if (gpu.tensorrt_rtx_available) {
            devices.push_back({gpu.adapter_name + " (TensorRT)", gpu.dedicated_memory_mb,
                               Backend::TensorRT, static_cast<int>(i)});
        }
        if (gpu.cuda_available) {
            devices.push_back({gpu.adapter_name + " (CUDA)", gpu.dedicated_memory_mb, Backend::CUDA,
                               static_cast<int>(i)});
        }
        if (gpu.directml_available) {
            devices.push_back({gpu.adapter_name + " (DirectML)", gpu.dedicated_memory_mb,
                               Backend::DirectML, static_cast<int>(i)});
        }
        if (gpu.winml_available) {
            devices.push_back({gpu.adapter_name + " (Windows AI)", gpu.dedicated_memory_mb,
                               Backend::WindowsML, static_cast<int>(i)});
        }
        if (gpu.openvino_available) {
            devices.push_back({gpu.adapter_name + " (OpenVINO)", gpu.dedicated_memory_mb,
                               Backend::OpenVINO, static_cast<int>(i)});
        }
    }
    devices.push_back({"Generic CPU", 0, Backend::CPU, 0});
#elif defined(__linux__)
    auto linux_gpus = core::list_linux_gpus();
    for (size_t i = 0; i < linux_gpus.size(); ++i) {
        const auto& gpu = linux_gpus[i];
        if (gpu.cuda_available) {
            devices.push_back({gpu.adapter_name + " (CUDA)", gpu.dedicated_memory_mb,
                               Backend::CUDA, static_cast<int>(i)});
        }
    }
    devices.push_back({"Linux CPU Baseline", 0, Backend::CPU, 0});
#else
    devices.push_back(auto_detect());
#endif
    return devices;
}

}  // namespace corridorkey
