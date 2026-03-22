#include "windows_rtx_probe.hpp"

#include <algorithm>
#include <cctype>
#include <corridorkey/detail/constants.hpp>
#include <corridorkey/engine.hpp>
#include <string_view>

#if __has_include(<onnxruntime/core/session/onnxruntime_cxx_api.h>)
#include <onnxruntime/core/session/onnxruntime_cxx_api.h>
#elif __has_include(<onnxruntime/onnxruntime_cxx_api.h>)
#include <onnxruntime/onnxruntime_cxx_api.h>
#else
#error "ONNX Runtime C++ headers not found"
#endif

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <dxgi1_6.h>
#include <windows.h>
#include <wrl/client.h>

#include <array>
#include <cstdio>
#include <sstream>
#endif

namespace corridorkey::core {

namespace {

#if defined(_WIN32)

constexpr unsigned int kNvidiaVendorId = 0x10DE;
constexpr unsigned int kAmdVendorId = 0x1002;
constexpr unsigned int kIntelVendorId = 0x8086;

std::string trim_copy(std::string value) {
    auto is_space = [](unsigned char ch) { return std::isspace(ch) != 0; };

    while (!value.empty() && is_space(static_cast<unsigned char>(value.front()))) {
        value.erase(value.begin());
    }
    while (!value.empty() && is_space(static_cast<unsigned char>(value.back()))) {
        value.pop_back();
    }
    return value;
}

std::string upper_copy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::toupper(ch)); });
    return value;
}

std::string utf8_from_wide(const std::wstring& wide) {
    if (wide.empty()) {
        return {};
    }

    int size = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (size <= 1) {
        return {};
    }

    std::string utf8(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, utf8.data(), size, nullptr, nullptr);
    utf8.pop_back();
    return utf8;
}

bool has_rtx_branding(const std::string& adapter_name) {
    return upper_copy(adapter_name).find("RTX") != std::string::npos;
}

struct NvidiaQueryData {
    std::string driver_version;
    int compute_major = 0;
    int compute_minor = 0;
};

std::optional<NvidiaQueryData> query_nvidia_gpu_properties(const std::string& adapter_name) {
    std::string command = "nvidia-smi --query-gpu=name,driver_version,compute_cap --format=csv,noheader 2>NUL";
    FILE* pipe = _popen(command.c_str(), "r");
    if (pipe == nullptr) {
        return std::nullopt;
    }

    std::array<char, 512> buffer{};
    std::optional<NvidiaQueryData> result = std::nullopt;
    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
        std::string line = trim_copy(buffer.data());
        size_t first_comma = line.find(',');
        if (first_comma == std::string::npos) {
            continue;
        }

        size_t second_comma = line.find(',', first_comma + 1);
        if (second_comma == std::string::npos) {
            continue;
        }

        std::string queried_name = trim_copy(line.substr(0, first_comma));
        std::string queried_driver = trim_copy(line.substr(first_comma + 1, second_comma - first_comma - 1));
        std::string queried_cap = trim_copy(line.substr(second_comma + 1));

        if (queried_name == adapter_name) {
            NvidiaQueryData data;
            data.driver_version = queried_driver;
            size_t dot_pos = queried_cap.find('.');
            if (dot_pos != std::string::npos) {
                try {
                    data.compute_major = std::stoi(trim_copy(queried_cap.substr(0, dot_pos)));
                    data.compute_minor = std::stoi(trim_copy(queried_cap.substr(dot_pos + 1)));
                } catch (...) {
                    // Ignore parse errors
                }
            }
            result = data;
            break;
        }
    }

    _pclose(pipe);
    return result;
}


bool provider_available(const std::string& provider_name) {
    try {
        Ort::Env dummy_env{ORT_LOGGING_LEVEL_WARNING, "probe"};
        auto providers = Ort::GetAvailableProviders();
        return std::find(providers.begin(), providers.end(), provider_name) != providers.end();
    } catch (...) {
        return false;
    }
}

bool onnxruntime_export_available(const char* export_name) {
    HMODULE runtime_module = GetModuleHandleW(L"onnxruntime.dll");
    if (runtime_module == nullptr) {
        runtime_module = LoadLibraryW(L"onnxruntime.dll");
    }
    if (runtime_module == nullptr) {
        return false;
    }
    return GetProcAddress(runtime_module, export_name) != nullptr;
}

#endif

}  // namespace

bool tensorrt_rtx_provider_available() {
#if defined(_WIN32)
    return provider_available(std::string(corridorkey::detail::providers::TENSORRT));
#else
    return false;
#endif
}

bool cuda_provider_available() {
#if defined(_WIN32)
    return provider_available(std::string(corridorkey::detail::providers::CUDA));
#else
    return false;
#endif
}

bool directml_provider_available() {
#if defined(_WIN32)
    return onnxruntime_export_available("OrtSessionOptionsAppendExecutionProvider_DML") ||
           provider_available("DML") || provider_available("DirectML") ||
           provider_available(std::string(corridorkey::detail::providers::DIRECTML));
#else
    return false;
#endif
}

bool winml_provider_available() {
#if defined(_WIN32)
    return provider_available("WinML") || provider_available(std::string(corridorkey::detail::providers::WINML));
#else
    return false;
#endif
}

bool openvino_provider_available() {
#if defined(_WIN32)
    return provider_available("OpenVINO") || provider_available(std::string(corridorkey::detail::providers::OPENVINO));
#else
    return false;
#endif
}

std::vector<WindowsGpuInfo> list_windows_gpus() {
    std::vector<WindowsGpuInfo> gpus;
#if defined(_WIN32)
    Microsoft::WRL::ComPtr<IDXGIFactory6> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
        return gpus;
    }

    bool trt_available = tensorrt_rtx_provider_available();
    bool cuda_available = cuda_provider_available();
    bool dml_available = directml_provider_available();
    bool winml_available = winml_provider_available();
    bool openvino_available = openvino_provider_available();

    for (UINT adapter_index = 0;; ++adapter_index) {
        Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
        if (factory->EnumAdapters1(adapter_index, &adapter) == DXGI_ERROR_NOT_FOUND) {
            break;
        }

        DXGI_ADAPTER_DESC1 description{};
        if (FAILED(adapter->GetDesc1(&description))) {
            continue;
        }
        if ((description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0) {
            continue;
        }

        WindowsGpuInfo info;
        info.adapter_name = trim_copy(utf8_from_wide(description.Description));
        info.dedicated_memory_mb =
            static_cast<int64_t>(description.DedicatedVideoMemory / (1024ULL * 1024ULL));
        info.vendor_id = description.VendorId;
        info.is_rtx = has_rtx_branding(info.adapter_name);

        // Capability Matrix
        info.directml_available = dml_available;
        info.winml_available = winml_available;
        info.openvino_available = openvino_available;

        if (info.vendor_id == kNvidiaVendorId) {
            info.cuda_available = cuda_available;

            auto props = query_nvidia_gpu_properties(info.adapter_name);
            if (props.has_value()) {
                info.driver_query_available = true;
                info.driver_version = props->driver_version;
                info.compute_capability_major = props->compute_major;
                info.compute_capability_minor = props->compute_minor;
                
                // TensorRT RTX EP requires CC 8.6, 8.9, 12.0 and above.
                bool cc_supported = (info.compute_capability_major > 8) || 
                                    (info.compute_capability_major == 8 && info.compute_capability_minor >= 6);
                
                info.tensorrt_rtx_available = info.is_rtx && trt_available && cc_supported;
            } else {
                info.tensorrt_rtx_available = false; // Disable if we can't verify compute capability
            }
        } else if (info.vendor_id == kIntelVendorId) {
            // Intel specific optimizations (OpenVINO)
            info.openvino_available = openvino_available;
        }

        gpus.push_back(std::move(info));
    }
#endif
    return gpus;
}

}  // namespace corridorkey::core
