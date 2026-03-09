#pragma once

#include <corridorkey/api_export.hpp>
#include <string>
#include <vector>
#include <cstdint>
#include <memory>
#include <variant>
#include <optional>

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
 * @brief Result type for robust error handling (C++20 compatible).
 * Simple polyfill for std::expected.
 */
template<typename E>
struct unexpected {
    E error;
    explicit unexpected(E e) : error(std::move(e)) {}
};

template<typename T>
class Result {
public:
    Result(T val) : m_data(std::move(val)) {}
    Result(unexpected<Error> err) : m_data(std::move(err.error)) {}
    Result(Error err) : m_data(std::move(err)) {}

    bool has_value() const { return std::holds_alternative<T>(m_data); }
    bool has_error() const { return std::holds_alternative<Error>(m_data); }

    const T& value() const { return std::get<T>(m_data); }
    T& value() { return std::get<T>(m_data); }

    const Error& error() const { return std::get<Error>(m_data); }

    T& operator*() { return value(); }
    const T& operator*() const { return value(); }
    T* operator->() { return &value(); }
    const T* operator->() const { return &value(); }

    operator bool() const { return has_value(); }

private:
    std::variant<T, Error> m_data;
};

// Specialization for Result<void>
template<>
class Result<void> {
public:
    Result() : m_error(std::nullopt) {}
    Result(unexpected<Error> err) : m_error(std::move(err.error)) {}
    Result(Error err) : m_error(std::move(err)) {}

    bool has_value() const { return !m_error.has_value(); }
    bool has_error() const { return m_error.has_value(); }

    const Error& error() const { return *m_error; }

    operator bool() const { return has_value(); }

private:
    std::optional<Error> m_error;
};

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
