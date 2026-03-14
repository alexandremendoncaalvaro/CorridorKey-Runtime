#include "windows_rtx_probe.hpp"

#include <algorithm>
#include <cctype>
#include <string_view>

#if __has_include(<onnxruntime/onnxruntime_cxx_api.h>)
#include <onnxruntime/onnxruntime_cxx_api.h>
#else
#include <onnxruntime/core/session/onnxruntime_cxx_api.h>
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

std::optional<std::string> query_driver_version_for_gpu(const std::string& adapter_name) {
    std::string command = "nvidia-smi --query-gpu=name,driver_version --format=csv,noheader 2>NUL";
    FILE* pipe = _popen(command.c_str(), "r");
    if (pipe == nullptr) {
        return std::nullopt;
    }

    std::array<char, 512> buffer{};
    std::optional<std::string> version = std::nullopt;
    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
        std::string line = trim_copy(buffer.data());
        auto separator = line.find(',');
        if (separator == std::string::npos) {
            continue;
        }

        std::string queried_name = trim_copy(line.substr(0, separator));
        std::string queried_driver = trim_copy(line.substr(separator + 1));
        if (queried_name == adapter_name) {
            version = queried_driver;
            break;
        }
    }

    _pclose(pipe);
    return version;
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

#endif

}  // namespace

bool tensorrt_rtx_provider_available() {
#if defined(_WIN32)
    return provider_available("NvTensorRTRTXExecutionProvider");
#else
    return false;
#endif
}

bool cuda_provider_available() {
#if defined(_WIN32)
    return provider_available("CUDAExecutionProvider");
#else
    return false;
#endif
}

bool directml_provider_available() {
#if defined(_WIN32)
    return provider_available("DML") || provider_available("DirectML");
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
        if (info.vendor_id == kNvidiaVendorId) {
            info.cuda_available = cuda_available;
            info.tensorrt_rtx_available = info.is_rtx && trt_available;

            auto driver = query_driver_version_for_gpu(info.adapter_name);
            if (driver.has_value()) {
                info.driver_query_available = true;
                info.driver_version = *driver;
            }
        }

        gpus.push_back(std::move(info));
    }
#endif
    return gpus;
}

}  // namespace corridorkey::core
