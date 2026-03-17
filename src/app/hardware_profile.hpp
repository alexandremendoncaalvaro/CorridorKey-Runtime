#pragma once

#include <corridorkey/types.hpp>

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

        if (device.backend == Backend::CoreML || device.backend == Backend::DirectML) {
            // Unified or generic GPU
            if (device.available_memory_mb >= 32000) {
                return {1536, "int8"};
            }
            if (device.available_memory_mb >= 16000) {
                return {1024, "int8"};
            }
            return {512, "int8"};
        }

        // Dedicated GPUs (CUDA, TensorRT)
        if (device.available_memory_mb >= 24000) {
            return {2048, "fp16"};  // Ultra-high tier (RTX 3090, 4090, etc.)
        }
        if (device.available_memory_mb >= 16000) {
            return {1536, "fp16"};  // High tier
        }
        if (device.available_memory_mb >= 10000) {
            return {1024, "fp16"};  // Medium-high tier
        }
        if (device.available_memory_mb >= 8000) {
            return {768, "fp16"};  // Medium tier
        }

        return {512, "int8"};  // Low tier
    }
};

}  // namespace corridorkey::app
