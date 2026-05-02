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
    // Quality-ladder rungs the strategy picker chooses from. Promoted out
    // of the get_best_strategy switch arms so cppcoreguidelines-avoid-
    // magic-numbers stays clean and the per-tier defaults are named at
    // the source.
    static constexpr int kCpuTierResolution = 512;
    static constexpr int kMlxTierResolution = 512;
    static constexpr int kCoreMlTierResolution = 1024;
    static constexpr int kFallbackResolution = 512;

    struct Strategy {
        int target_resolution = 0;
        std::string recommended_variant;  // "int8", "fp16", etc.
    };

    static Strategy get_best_strategy(const DeviceInfo& device) {
        if (device.backend == Backend::CPU) {
            return {.target_resolution = kCpuTierResolution, .recommended_variant = "int8"};
        }

        if (device.backend == Backend::MLX) {
            return {.target_resolution = kMlxTierResolution, .recommended_variant = "mlx"};
        }

        if (auto safe_ceiling = max_supported_resolution_for_device(device);
            safe_ceiling.has_value()) {
            const std::string variant =
                (device.backend == Backend::TensorRT || device.backend == Backend::CUDA) ? "fp16"
                                                                                         : "int8";
            return {.target_resolution = *safe_ceiling, .recommended_variant = variant};
        }

        if (device.backend == Backend::CoreML) {
            return {.target_resolution = kCoreMlTierResolution, .recommended_variant = "int8"};
        }

        return {.target_resolution = kFallbackResolution, .recommended_variant = "int8"};
    }
};

}  // namespace corridorkey::app
