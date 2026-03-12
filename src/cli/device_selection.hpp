#pragma once

#include <algorithm>
#include <cctype>
#include <corridorkey/engine.hpp>
#include <string>
#include <vector>

namespace corridorkey::cli {

inline std::string backend_name(Backend backend) {
    switch (backend) {
        case Backend::CPU:
            return "cpu";
        case Backend::CoreML:
            return "coreml";
        case Backend::CUDA:
            return "cuda";
        case Backend::TensorRT:
            return "tensorrt";
        case Backend::DirectML:
            return "dml";
        case Backend::MLX:
            return "mlx";
        default:
            return "auto";
    }
}

inline DeviceInfo select_device(const std::vector<DeviceInfo>& devices, std::string device_str) {
    if (devices.empty()) {
        return DeviceInfo{};
    }

    DeviceInfo fallback_device = devices.front();
    if (device_str == "auto") {
        return DeviceInfo{"Auto", fallback_device.available_memory_mb, Backend::Auto};
    }

    try {
        int index = std::stoi(device_str);
        if (index >= 0 && index < static_cast<int>(devices.size())) {
            return devices[static_cast<size_t>(index)];
        }
    } catch (...) {
    }

    std::transform(device_str.begin(), device_str.end(), device_str.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    if (device_str == "rtx" || device_str == "trt") {
        device_str = "tensorrt";
    }
    for (const auto& device : devices) {
        if (backend_name(device.backend) == device_str) {
            return device;
        }
    }

    return fallback_device;
}

}  // namespace corridorkey::cli
