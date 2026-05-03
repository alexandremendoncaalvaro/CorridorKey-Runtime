#include "windows_rtx_probe.hpp"

#include <algorithm>
#include <cctype>
#include <corridorkey/detail/constants.hpp>
#include <corridorkey/engine.hpp>
#include <optional>
#include <string_view>

#include "windows_rtx_probe_internal.hpp"

#if __has_include(<onnxruntime/core/session/onnxruntime_cxx_api.h>)
#include <onnxruntime/core/session/onnxruntime_cxx_api.h>
#elif __has_include(<onnxruntime/onnxruntime_cxx_api.h>)
#include <onnxruntime/onnxruntime_cxx_api.h>
#else
#error "ONNX Runtime C++ headers not found"
#endif

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <dxgi1_6.h>
#include <windows.h>
#include <wrl/client.h>
#endif

namespace corridorkey::core {

// NOLINTBEGIN(readability-identifier-length,modernize-use-ranges,cppcoreguidelines-pro-type-reinterpret-cast,cppcoreguidelines-avoid-magic-numbers,bugprone-unchecked-string-to-number-conversion,cppcoreguidelines-pro-type-cstyle-cast,modernize-use-using,modernize-use-integer-sign-comparison,cert-dcl50-cpp,cppcoreguidelines-pro-type-const-cast,readability-identifier-naming,modernize-raw-string-literal,readability-container-size-empty,bugprone-command-processor,readability-use-std-min-max,cppcoreguidelines-avoid-non-const-global-variables,bugprone-misplaced-widening-cast,readability-misleading-indentation,cert-env33-c,performance-unnecessary-copy-initialization,readability-named-parameter,readability-isolate-declaration,cert-err34-c,modernize-avoid-variadic-functions,cppcoreguidelines-pro-bounds-constant-array-index)
//
// windows_rtx_probe.cpp tidy-suppression rationale.
//
// The single-character `ch` parameter mirrors the <cctype> predicate
// idiom (std::isspace / std::toupper take an unsigned char) used in
// trim and upper helpers. std::transform with the cctype lambda has no
// std::ranges replacement that preserves the unsigned-char cast
// requirement. The reinterpret_cast on GetProcAddress and on the LUID
// byte buffer is the canonical Win32 / CUDA driver API form for typed
// function-pointer loading and for the cuDeviceGetLuid char* contract.
// The 1024 * 1024 byte-to-MiB conversion is the standard DXGI memory
// reporting unit.
namespace {

#ifdef _WIN32

constexpr unsigned int kNvidiaVendorId = 0x10DE;
constexpr unsigned int kIntelVendorId = 0x8086;
constexpr int kCudaSuccess = 0;
constexpr int kCudaAttributeComputeCapabilityMajor = 75;
constexpr int kCudaAttributeComputeCapabilityMinor = 76;

using CuResult = int;
using CuDevice = int;
using CuInitFn = CuResult(__stdcall*)(unsigned int);
using CuDeviceGetCountFn = CuResult(__stdcall*)(int*);
using CuDeviceGetFn = CuResult(__stdcall*)(CuDevice*, int);
using CuDeviceGetAttributeFn = CuResult(__stdcall*)(int*, int, CuDevice);
using CuDeviceGetLuidFn = CuResult(__stdcall*)(char*, unsigned int*, CuDevice);

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

struct LoadedCudaDriverApi {
    HMODULE module = nullptr;
    CuInitFn cu_init = nullptr;
    CuDeviceGetCountFn cu_device_get_count = nullptr;
    CuDeviceGetFn cu_device_get = nullptr;
    CuDeviceGetAttributeFn cu_device_get_attribute = nullptr;
    CuDeviceGetLuidFn cu_device_get_luid = nullptr;

    LoadedCudaDriverApi() = default;

    LoadedCudaDriverApi(const LoadedCudaDriverApi&) = delete;
    LoadedCudaDriverApi& operator=(const LoadedCudaDriverApi&) = delete;

    LoadedCudaDriverApi(LoadedCudaDriverApi&& other) noexcept
        : module(other.module),
          cu_init(other.cu_init),
          cu_device_get_count(other.cu_device_get_count),
          cu_device_get(other.cu_device_get),
          cu_device_get_attribute(other.cu_device_get_attribute),
          cu_device_get_luid(other.cu_device_get_luid) {
        other.module = nullptr;
        other.cu_init = nullptr;
        other.cu_device_get_count = nullptr;
        other.cu_device_get = nullptr;
        other.cu_device_get_attribute = nullptr;
        other.cu_device_get_luid = nullptr;
    }

    LoadedCudaDriverApi& operator=(LoadedCudaDriverApi&& other) noexcept {
        if (this == &other) {
            return *this;
        }

        if (module != nullptr) {
            FreeLibrary(module);
        }

        module = other.module;
        cu_init = other.cu_init;
        cu_device_get_count = other.cu_device_get_count;
        cu_device_get = other.cu_device_get;
        cu_device_get_attribute = other.cu_device_get_attribute;
        cu_device_get_luid = other.cu_device_get_luid;

        other.module = nullptr;
        other.cu_init = nullptr;
        other.cu_device_get_count = nullptr;
        other.cu_device_get = nullptr;
        other.cu_device_get_attribute = nullptr;
        other.cu_device_get_luid = nullptr;
        return *this;
    }

    ~LoadedCudaDriverApi() {
        if (module != nullptr) {
            FreeLibrary(module);
        }
    }

    [[nodiscard]] bool ready() const {
        return module != nullptr && cu_init != nullptr && cu_device_get_count != nullptr &&
               cu_device_get != nullptr && cu_device_get_attribute != nullptr &&
               cu_device_get_luid != nullptr;
    }
};

template <typename T>
T load_cuda_symbol(HMODULE module, const char* name) {
    return reinterpret_cast<T>(GetProcAddress(module, name));
}

std::optional<LoadedCudaDriverApi> load_cuda_driver_api() {
    LoadedCudaDriverApi api;
    api.module = LoadLibraryW(L"nvcuda.dll");
    if (api.module == nullptr) {
        return std::nullopt;
    }

    api.cu_init = load_cuda_symbol<CuInitFn>(api.module, "cuInit");
    api.cu_device_get_count = load_cuda_symbol<CuDeviceGetCountFn>(api.module, "cuDeviceGetCount");
    api.cu_device_get = load_cuda_symbol<CuDeviceGetFn>(api.module, "cuDeviceGet");
    api.cu_device_get_attribute =
        load_cuda_symbol<CuDeviceGetAttributeFn>(api.module, "cuDeviceGetAttribute");
    api.cu_device_get_luid = load_cuda_symbol<CuDeviceGetLuidFn>(api.module, "cuDeviceGetLuid");
    if (!api.ready()) {
        return std::nullopt;
    }

    return api;
}

std::vector<detail::CudaDeviceIdentity> enumerate_cuda_devices() {
    auto api = load_cuda_driver_api();
    if (!api.has_value() || api->cu_init(0) != kCudaSuccess) {
        return {};
    }

    int device_count = 0;
    if (api->cu_device_get_count(&device_count) != kCudaSuccess || device_count <= 0) {
        return {};
    }

    std::vector<detail::CudaDeviceIdentity> devices;
    devices.reserve(static_cast<size_t>(device_count));
    for (int ordinal = 0; ordinal < device_count; ++ordinal) {
        CuDevice device = 0;
        if (api->cu_device_get(&device, ordinal) != kCudaSuccess) {
            continue;
        }

        int compute_major = 0;
        int compute_minor = 0;
        if (api->cu_device_get_attribute(&compute_major, kCudaAttributeComputeCapabilityMajor,
                                         device) != kCudaSuccess ||
            api->cu_device_get_attribute(&compute_minor, kCudaAttributeComputeCapabilityMinor,
                                         device) != kCudaSuccess) {
            continue;
        }

        std::array<unsigned char, sizeof(LUID)> luid = {};
        unsigned int device_node_mask = 0;
        if (api->cu_device_get_luid(reinterpret_cast<char*>(luid.data()), &device_node_mask,
                                    device) != kCudaSuccess) {
            continue;
        }

        detail::CudaDeviceIdentity identity;
        identity.luid = luid;
        identity.has_luid = true;
        identity.compute_capability_major = compute_major;
        identity.compute_capability_minor = compute_minor;
        devices.push_back(identity);
    }

    return devices;
}

bool provider_available(const std::string& provider_name) {
    try {
        Ort::Env dummy_env{ORT_LOGGING_LEVEL_WARNING, "probe"};
        auto providers = Ort::GetAvailableProviders();
        return std::ranges::find(providers, provider_name) != providers.end();
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
#ifdef _WIN32
    return provider_available(std::string(corridorkey::detail::providers::TENSORRT));
#else
    return false;
#endif
}

bool cuda_provider_available() {
#ifdef _WIN32
    return provider_available(std::string(corridorkey::detail::providers::CUDA));
#else
    return false;
#endif
}

bool directml_provider_available() {
#ifdef _WIN32
    return onnxruntime_export_available("OrtSessionOptionsAppendExecutionProvider_DML") ||
           provider_available("DML") || provider_available("DirectML") ||
           provider_available(std::string(corridorkey::detail::providers::DIRECTML));
#else
    return false;
#endif
}

bool winml_provider_available() {
#ifdef _WIN32
    return provider_available("WinML") ||
           provider_available(std::string(corridorkey::detail::providers::WINML));
#else
    return false;
#endif
}

bool openvino_provider_available() {
#ifdef _WIN32
    return provider_available("OpenVINO") ||
           provider_available(std::string(corridorkey::detail::providers::OPENVINO));
#else
    return false;
#endif
}

std::vector<WindowsGpuInfo> list_windows_gpus() {
    std::vector<WindowsGpuInfo> gpus;
#ifdef _WIN32
    Microsoft::WRL::ComPtr<IDXGIFactory6> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
        return gpus;
    }

    const bool trt_available = tensorrt_rtx_provider_available();
    const bool cuda_available = cuda_provider_available();
    const bool dml_available = directml_provider_available();
    const bool winml_available = winml_provider_available();
    const bool openvino_available = openvino_provider_available();
    const auto cuda_devices = enumerate_cuda_devices();

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
        info.directml_available = dml_available;
        info.winml_available = winml_available;
        info.openvino_available = openvino_available;

        if (info.vendor_id == kNvidiaVendorId) {
            info.cuda_available = cuda_available;

            auto cuda_device =
                detail::find_cuda_device_for_adapter(description.AdapterLuid, cuda_devices);
            if (cuda_device.has_value()) {
                info.driver_query_available = true;
                info.compute_capability_major = cuda_device->compute_capability_major;
                info.compute_capability_minor = cuda_device->compute_capability_minor;
                info.tensorrt_rtx_available =
                    info.is_rtx && trt_available &&
                    detail::compute_capability_supports_tensorrt_rtx(info.compute_capability_major,
                                                                     info.compute_capability_minor);
            }
        } else if (info.vendor_id == kIntelVendorId) {
            info.openvino_available = openvino_available;
        }

        gpus.push_back(std::move(info));
    }
#endif
    return gpus;
}

}  // namespace corridorkey::core
// NOLINTEND(readability-identifier-length,modernize-use-ranges,cppcoreguidelines-pro-type-reinterpret-cast,cppcoreguidelines-avoid-magic-numbers,bugprone-unchecked-string-to-number-conversion,cppcoreguidelines-pro-type-cstyle-cast,modernize-use-using,modernize-use-integer-sign-comparison,cert-dcl50-cpp,cppcoreguidelines-pro-type-const-cast,readability-identifier-naming,modernize-raw-string-literal,readability-container-size-empty,bugprone-command-processor,readability-use-std-min-max,cppcoreguidelines-avoid-non-const-global-variables,bugprone-misplaced-widening-cast,readability-misleading-indentation,cert-env33-c,performance-unnecessary-copy-initialization,readability-named-parameter,readability-isolate-declaration,cert-err34-c,modernize-avoid-variadic-functions,cppcoreguidelines-pro-bounds-constant-array-index)
