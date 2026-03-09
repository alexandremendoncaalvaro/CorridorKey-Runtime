#pragma once

#include <corridorkey/api_export.hpp>
#include <string>
#include <vector>
#include <cstdint>
#include <memory>
#include <variant>
#include <optional>
#include <span>

#if defined(_WIN32)
#include <malloc.h>
#endif

namespace corridorkey {

// SIMD Alignment requirement (64 bytes for AVX-512 / Cache lines)
inline constexpr size_t MEMORY_ALIGNMENT = 64;

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
 * @brief Simple image view for passing data without copies.
 * Inspired by std::mdspan, this holds a pointer to data and dimensions.
 */
struct Image {
    int width = 0;
    int height = 0;
    int channels = 0;
    std::span<float> data; 

    bool empty() const { return data.empty(); }
};

/**
 * @brief Owned image data with guaranteed SIMD alignment.
 */
class ImageBuffer {
public:
    ImageBuffer() : m_width(0), m_height(0), m_channels(0), m_ptr(nullptr) {}

    ImageBuffer(int w, int h, int c) 
        : m_width(w), m_height(h), m_channels(c) {
        size_t size = static_cast<size_t>(w) * h * c;
        if (size == 0) {
            m_ptr = nullptr;
            return;
        }
#if defined(_WIN32)
        m_ptr = static_cast<float*>(_aligned_malloc(size * sizeof(float), MEMORY_ALIGNMENT));
#else
        if (posix_memalign(reinterpret_cast<void**>(&m_ptr), MEMORY_ALIGNMENT, size * sizeof(float)) != 0) {
            m_ptr = nullptr;
        }
#endif
        m_data = std::span<float>(m_ptr, size);
    }

    ~ImageBuffer() {
        if (m_ptr) {
#if defined(_WIN32)
            _aligned_free(m_ptr);
#else
            free(m_ptr);
#endif
        }
    }

    ImageBuffer(const ImageBuffer&) = delete;
    ImageBuffer& operator=(const ImageBuffer&) = delete;

    ImageBuffer(ImageBuffer&& other) noexcept 
        : m_width(other.m_width), m_height(other.m_height), m_channels(other.m_channels),
          m_ptr(other.m_ptr), m_data(other.m_data) {
        other.m_ptr = nullptr;
        other.m_data = {};
    }

    ImageBuffer& operator=(ImageBuffer&& other) noexcept {
        if (this != &other) {
            if (m_ptr) {
#if defined(_WIN32)
                _aligned_free(m_ptr);
#else
                free(m_ptr);
#endif
            }
            m_width = other.m_width;
            m_height = other.m_height;
            m_channels = other.m_channels;
            m_ptr = other.m_ptr;
            m_data = other.m_data;
            other.m_ptr = nullptr;
            other.m_data = {};
        }
        return *this;
    }

    Image view() { return { m_width, m_height, m_channels, m_data }; }
    Image const_view() const { return { m_width, m_height, m_channels, m_data }; }

private:
    int m_width, m_height, m_channels;
    float* m_ptr;
    std::span<float> m_data;
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
 * These hold buffers, but the FrameResult itself can be moved easily.
 */
struct FrameResult {
    ImageBuffer alpha;       
    ImageBuffer foreground;  
    ImageBuffer composite;   
};

} // namespace corridorkey
