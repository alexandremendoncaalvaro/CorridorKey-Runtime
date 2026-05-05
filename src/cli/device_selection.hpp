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
        return DeviceInfo{
            .name = "Auto",
            .available_memory_mb = fallback_device.available_memory_mb,
            .backend = Backend::Auto,
        };
    }

    try {
        const auto index = static_cast<std::size_t>(std::stoi(device_str));
        if (index < devices.size()) {
            return devices.at(index);
        }
    } catch (const std::exception& parse_failure) {
        // Non-numeric device_str (std::stoi throws std::invalid_argument
        // or std::out_of_range): fall through to the named-backend match
        // path below. The exception is intentional control flow, not an
        // error to log; reference the variable so the empty-catch lint
        // does not flag this.
        (void)parse_failure;
    }

    std::ranges::transform(device_str, device_str.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
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
        return DeviceInfo{
            .name = "TorchTRT",
            .available_memory_mb = fallback_device.available_memory_mb,
            .backend = Backend::TorchTRT,
        };
    }

    return fallback_device;
}

}  // namespace corridorkey::cli
