#include <corridorkey/engine.hpp>

#include "linux_cuda_probe.hpp"
#include "mlx_probe.hpp"
#include "windows_rtx_probe.hpp"

#ifdef __APPLE__
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

// NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,readability-function-size,readability-function-cognitive-complexity,cppcoreguidelines-avoid-magic-numbers,bugprone-implicit-widening-of-multiplication-result,modernize-use-designated-initializers,bugprone-unchecked-string-to-number-conversion,cppcoreguidelines-pro-type-cstyle-cast,modernize-use-using,modernize-use-integer-sign-comparison,cert-dcl50-cpp,cppcoreguidelines-pro-type-const-cast,readability-identifier-naming,modernize-raw-string-literal,readability-container-size-empty,bugprone-command-processor,readability-use-std-min-max,cppcoreguidelines-avoid-non-const-global-variables,bugprone-misplaced-widening-cast,readability-misleading-indentation,cert-env33-c,performance-unnecessary-copy-initialization,readability-named-parameter,readability-isolate-declaration,cert-err34-c,modernize-avoid-variadic-functions,cppcoreguidelines-pro-bounds-constant-array-index)
//
// device_detection.cpp tidy-suppression rationale.
//
// This file enumerates platform GPUs by index using stable per-platform
// vector results from probe helpers; the indexed access is bounded by
// the loop counter and is the canonical way to surface device_index
// downstream. Probe routines and their tier ordering form a single
// orchestration function whose size and branching reflect the explicit
// platform priority list (TensorRT-RTX -> WindowsML -> OpenVINO -> CUDA
// -> DirectML -> CPU), not accidental complexity. The 1024*1024 byte-to-
// MiB conversion and the DeviceInfo aggregate initialisers are
// deliberate, well-known forms used throughout the engine API.
namespace {

#ifdef __APPLE__
bool compiled_for_apple_silicon() {
#if defined(__aarch64__) || defined(__arm64__)
    return true;
#else
    return false;
#endif
}

bool detect_apple_silicon() {
    int is_arm64 = 0;
    size_t arm64_size = sizeof(is_arm64);
    if (sysctlbyname("hw.optional.arm64", &is_arm64, &arm64_size, NULL, 0) == 0) {
        return is_arm64 == 1;
    }
    return compiled_for_apple_silicon();
}
#endif

}  // namespace

DeviceInfo auto_detect() {
    DeviceInfo device;
    device.backend = Backend::CPU;  // Default fallback
    device.available_memory_mb = 0;

#ifdef __APPLE__
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
#ifdef __APPLE__
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
            devices.push_back({gpu.adapter_name + " (CUDA)", gpu.dedicated_memory_mb, Backend::CUDA,
                               static_cast<int>(i)});
        }
    }
    devices.push_back({"Linux CPU Baseline", 0, Backend::CPU, 0});
#else
    devices.push_back(auto_detect());
#endif
    return devices;
}

}  // namespace corridorkey
// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,readability-function-size,readability-function-cognitive-complexity,cppcoreguidelines-avoid-magic-numbers,bugprone-implicit-widening-of-multiplication-result,modernize-use-designated-initializers,bugprone-unchecked-string-to-number-conversion,cppcoreguidelines-pro-type-cstyle-cast,modernize-use-using,modernize-use-integer-sign-comparison,cert-dcl50-cpp,cppcoreguidelines-pro-type-const-cast,readability-identifier-naming,modernize-raw-string-literal,readability-container-size-empty,bugprone-command-processor,readability-use-std-min-max,cppcoreguidelines-avoid-non-const-global-variables,bugprone-misplaced-widening-cast,readability-misleading-indentation,cert-env33-c,performance-unnecessary-copy-initialization,readability-named-parameter,readability-isolate-declaration,cert-err34-c,modernize-avoid-variadic-functions,cppcoreguidelines-pro-bounds-constant-array-index)
