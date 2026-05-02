#include "linux_cuda_probe.hpp"

#if defined(__linux__)

#include <dlfcn.h>

#include <cstring>
#include <mutex>
#include <string>
#include <vector>

namespace corridorkey::core {

namespace {

// Minimal NVML surface we depend on. We resolve these symbols dynamically so
// the Linux build runs on any host — with or without the NVIDIA driver — and
// fails gracefully when libnvidia-ml is missing.
using nvmlReturn_t = int;
constexpr nvmlReturn_t NVML_SUCCESS = 0;

using nvmlDevice_t = void*;

using Fn_nvmlInit_v2 = nvmlReturn_t (*)();
using Fn_nvmlShutdown = nvmlReturn_t (*)();
using Fn_nvmlDeviceGetCount_v2 = nvmlReturn_t (*)(unsigned int*);
using Fn_nvmlDeviceGetHandleByIndex_v2 = nvmlReturn_t (*)(unsigned int, nvmlDevice_t*);
using Fn_nvmlDeviceGetName = nvmlReturn_t (*)(nvmlDevice_t, char*, unsigned int);
using Fn_nvmlSystemGetDriverVersion = nvmlReturn_t (*)(char*, unsigned int);

struct nvmlMemory_t {
    unsigned long long total;
    unsigned long long free;
    unsigned long long used;
};
using Fn_nvmlDeviceGetMemoryInfo = nvmlReturn_t (*)(nvmlDevice_t, nvmlMemory_t*);
using Fn_nvmlDeviceGetCudaComputeCapability = nvmlReturn_t (*)(nvmlDevice_t, int*, int*);

class NvmlLoader {
   public:
    NvmlLoader() {
        load();
    }

    ~NvmlLoader() {
        if (m_handle != nullptr && shutdown_fn != nullptr) {
            shutdown_fn();
            ::dlclose(m_handle);
        }
    }

    NvmlLoader(const NvmlLoader&) = delete;
    NvmlLoader& operator=(const NvmlLoader&) = delete;

    bool available() const {
        return m_ready;
    }

    Fn_nvmlDeviceGetCount_v2 get_count_fn = nullptr;
    Fn_nvmlDeviceGetHandleByIndex_v2 get_handle_fn = nullptr;
    Fn_nvmlDeviceGetName get_name_fn = nullptr;
    Fn_nvmlDeviceGetMemoryInfo get_memory_fn = nullptr;
    Fn_nvmlDeviceGetCudaComputeCapability get_cc_fn = nullptr;
    Fn_nvmlSystemGetDriverVersion get_driver_fn = nullptr;

   private:
    void load() {
        m_handle = ::dlopen("libnvidia-ml.so.1", RTLD_NOW | RTLD_LOCAL);
        if (m_handle == nullptr) {
            m_handle = ::dlopen("libnvidia-ml.so", RTLD_NOW | RTLD_LOCAL);
        }
        if (m_handle == nullptr) {
            return;
        }

        auto init_fn = reinterpret_cast<Fn_nvmlInit_v2>(::dlsym(m_handle, "nvmlInit_v2"));
        shutdown_fn = reinterpret_cast<Fn_nvmlShutdown>(::dlsym(m_handle, "nvmlShutdown"));
        get_count_fn =
            reinterpret_cast<Fn_nvmlDeviceGetCount_v2>(::dlsym(m_handle, "nvmlDeviceGetCount_v2"));
        get_handle_fn = reinterpret_cast<Fn_nvmlDeviceGetHandleByIndex_v2>(
            ::dlsym(m_handle, "nvmlDeviceGetHandleByIndex_v2"));
        get_name_fn =
            reinterpret_cast<Fn_nvmlDeviceGetName>(::dlsym(m_handle, "nvmlDeviceGetName"));
        get_memory_fn = reinterpret_cast<Fn_nvmlDeviceGetMemoryInfo>(
            ::dlsym(m_handle, "nvmlDeviceGetMemoryInfo"));
        get_cc_fn = reinterpret_cast<Fn_nvmlDeviceGetCudaComputeCapability>(
            ::dlsym(m_handle, "nvmlDeviceGetCudaComputeCapability"));
        get_driver_fn = reinterpret_cast<Fn_nvmlSystemGetDriverVersion>(
            ::dlsym(m_handle, "nvmlSystemGetDriverVersion"));

        if (init_fn == nullptr || shutdown_fn == nullptr || get_count_fn == nullptr ||
            get_handle_fn == nullptr || get_name_fn == nullptr || get_memory_fn == nullptr) {
            ::dlclose(m_handle);
            m_handle = nullptr;
            return;
        }

        if (init_fn() != NVML_SUCCESS) {
            ::dlclose(m_handle);
            m_handle = nullptr;
            return;
        }

        m_ready = true;
    }

    void* m_handle = nullptr;
    bool m_ready = false;
    Fn_nvmlShutdown shutdown_fn = nullptr;
};

NvmlLoader& nvml_loader() {
    static NvmlLoader instance;
    return instance;
}

bool name_suggests_rtx(const std::string& name) {
    // NVML reports names like "NVIDIA GeForce RTX 4090". We treat any "RTX"
    // substring as the marker; this matches the heuristic used on Windows.
    return name.find("RTX") != std::string::npos;
}

}  // namespace

std::vector<LinuxGpuInfo> list_linux_gpus() {
    std::vector<LinuxGpuInfo> result;
    auto& loader = nvml_loader();
    if (!loader.available()) {
        return result;
    }

    unsigned int count = 0;
    if (loader.get_count_fn(&count) != NVML_SUCCESS || count == 0) {
        return result;
    }

    std::string driver_version;
    if (loader.get_driver_fn != nullptr) {
        char driver_buffer[80] = {};
        if (loader.get_driver_fn(driver_buffer, sizeof(driver_buffer)) == NVML_SUCCESS) {
            driver_version = driver_buffer;
        }
    }

    for (unsigned int i = 0; i < count; ++i) {
        nvmlDevice_t device = nullptr;
        if (loader.get_handle_fn(i, &device) != NVML_SUCCESS || device == nullptr) {
            continue;
        }

        LinuxGpuInfo info;
        info.driver_version = driver_version;

        char name_buffer[96] = {};
        if (loader.get_name_fn(device, name_buffer, sizeof(name_buffer)) == NVML_SUCCESS) {
            info.adapter_name = name_buffer;
        } else {
            info.adapter_name = "NVIDIA GPU";
        }

        nvmlMemory_t memory = {};
        if (loader.get_memory_fn(device, &memory) == NVML_SUCCESS) {
            info.dedicated_memory_mb = static_cast<int64_t>(memory.total / (1024ULL * 1024ULL));
        }

        if (loader.get_cc_fn != nullptr) {
            int major = 0;
            int minor = 0;
            if (loader.get_cc_fn(device, &major, &minor) == NVML_SUCCESS) {
                info.compute_capability_major = major;
                info.compute_capability_minor = minor;
            }
        }

        info.is_rtx = name_suggests_rtx(info.adapter_name);
        info.cuda_available = true;
        result.push_back(info);
    }

    return result;
}

bool cuda_provider_available_linux() {
    return !list_linux_gpus().empty();
}

}  // namespace corridorkey::core

#else

namespace corridorkey::core {

std::vector<LinuxGpuInfo> list_linux_gpus() {
    return {};
}
bool cuda_provider_available_linux() {
    return false;
}

}  // namespace corridorkey::core

#endif
