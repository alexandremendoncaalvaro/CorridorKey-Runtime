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

bool is_ampere_or_newer_name(const std::string& adapter_name) {
    auto upper = upper_copy(adapter_name);
    auto rtx_pos = upper.find("RTX");
    if (rtx_pos == std::string::npos) {
        return false;
    }

    auto after_rtx = upper.substr(rtx_pos + 3);
    if (after_rtx.find('A') != std::string::npos) {
        return true;
    }

    int number = 0;
    bool parsing = false;
    for (char ch : after_rtx) {
        if (std::isdigit(static_cast<unsigned char>(ch)) != 0) {
            parsing = true;
            number = number * 10 + (ch - '0');
            continue;
        }
        if (parsing) {
            break;
        }
    }

    return parsing && number >= 3000;
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

#endif

}  // namespace

bool tensorrt_rtx_provider_available() {
    try {
        auto providers = Ort::GetAvailableProviders();
        return std::find(providers.begin(), providers.end(), "NvTensorRTRTXExecutionProvider") !=
               providers.end();
    } catch (...) {
        return false;
    }
}

std::optional<WindowsRtxGpuInfo> probe_windows_rtx_gpu() {
#if !defined(_WIN32)
    return std::nullopt;
#else
    Microsoft::WRL::ComPtr<IDXGIFactory6> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
        return std::nullopt;
    }

    std::optional<WindowsRtxGpuInfo> best_gpu = std::nullopt;
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
        if (description.VendorId != kNvidiaVendorId) {
            continue;
        }

        auto adapter_name = trim_copy(utf8_from_wide(description.Description));
        if (upper_copy(adapter_name).find("RTX") == std::string::npos) {
            continue;
        }

        WindowsRtxGpuInfo candidate;
        candidate.adapter_name = adapter_name;
        candidate.dedicated_memory_mb =
            static_cast<int64_t>(description.DedicatedVideoMemory / (1024ULL * 1024ULL));
        candidate.ampere_or_newer = is_ampere_or_newer_name(adapter_name);

        auto driver_version = query_driver_version_for_gpu(adapter_name);
        if (driver_version.has_value()) {
            candidate.driver_query_available = true;
            candidate.driver_version = *driver_version;
        }

        if (!best_gpu.has_value() ||
            candidate.dedicated_memory_mb > best_gpu->dedicated_memory_mb) {
            best_gpu = candidate;
        }
    }

    if (best_gpu.has_value()) {
        best_gpu->provider_available = tensorrt_rtx_provider_available();
    }

    return best_gpu;
#endif
}

}  // namespace corridorkey::core
