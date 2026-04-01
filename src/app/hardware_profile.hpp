#pragma once

#include <corridorkey/types.hpp>

#include "runtime_contracts.hpp"

namespace corridorkey::app {

/**
 * @brief Logic to select the best resolution and model variant based on hardware capabilities.
 * Implements the "Hardware Tiers" section of the specification.
 */
class HardwareProfile {
   public:
    struct Strategy {
        int target_resolution;
        std::string recommended_variant;  // "int8", "fp16", etc.
    };

    static Strategy get_best_strategy(const DeviceInfo& device) {
        if (device.backend == Backend::CPU) {
            return {512, "int8"};
        }

        if (device.backend == Backend::MLX) {
            return {512, "mlx"};
        }

        if (auto safe_ceiling = max_supported_resolution_for_device(device);
            safe_ceiling.has_value()) {
            const std::string variant =
                (device.backend == Backend::TensorRT || device.backend == Backend::CUDA) ? "fp16"
                                                                                         : "int8";
            return {*safe_ceiling, variant};
        }

        if (device.backend == Backend::CoreML) {
            return {1024, "int8"};
        }

        return {512, "int8"};
    }
};

}  // namespace corridorkey::app
