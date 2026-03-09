#pragma once

#include <corridorkey/api_export.hpp>
#include <expected>
#include <string>
#include <vector>
#include <cstdint>
#include <memory>

namespace corridorkey {

/**
 * @brief Error codes for library operations.
 */
enum class ErrorCode {
    Success = 0,
    ModelLoadFailed,
    InferenceFailed,
    IoError,
    InvalidParameters,
    Cancelled,
    HardwareNotSupported
};

/**
 * @brief Rich error information.
 */
struct Error {
    ErrorCode code;
    std::string message;
};

/**
 * @brief Result type for robust error handling.
 */
template<typename T>
using Result = std::expected<T, Error>;

/**
 * @brief Hardware backends supported by the runtime.
 */
enum class Backend {
    Auto,
    CPU,
    CUDA,
    TensorRT,
    CoreML,
    DirectML
};

/**
 * @brief Information about a detected hardware device.
 */
struct DeviceInfo {
    std::string name;
    int64_t available_memory_mb;
    Backend backend;
};

/**
 * @brief Simple image wrapper for passing data to/from the library.
 * This holds raw float data in linear space.
 */
struct Image {
    int width = 0;
    int height = 0;
    int channels = 0;
    std::vector<float> data; // Interleaved float data (HWC)
};

/**
 * @brief Parameters to control the inference and post-processing.
 */
struct InferenceParams {
    float despill_strength = 1.0f;
    bool auto_despeckle = true;
    int despeckle_size = 400;
    float refiner_scale = 1.0f;
    bool input_is_linear = false;
};

/**
 * @brief Results of a single frame inference.
 */
struct FrameResult {
    Image alpha;       // 1-channel, linear float
    Image foreground;  // 3-channel, linear float
    Image composite;   // 4-channel, premultiplied linear float
};

} // namespace corridorkey
