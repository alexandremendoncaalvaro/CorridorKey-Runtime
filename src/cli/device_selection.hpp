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
        case Backend::TorchTRT:
            return "torchtrt";
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
    if (device_str == "torch_trt" || device_str == "torch-trt" || device_str == "ttrt") {
        device_str = "torchtrt";
    }
    for (const auto& device : devices) {
        if (backend_name(device.backend) == device_str) {
            return device;
        }
    }
    // TorchTRT is not in the standard detected-devices list (it is selected
    // via .ts artifact on the blue model pack rather than via hardware
    // probe). Synthesize a device entry so --device torchtrt routes to the
    // TorchTrtSession factory regardless of probe output.
    if (device_str == "torchtrt") {
        return DeviceInfo{"TorchTRT", fallback_device.available_memory_mb, Backend::TorchTRT};
    }

    return fallback_device;
}

}  // namespace corridorkey::cli
