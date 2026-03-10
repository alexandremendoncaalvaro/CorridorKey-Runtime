#pragma once

#include <corridorkey/api_export.hpp>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <variant>
#include <vector>

#ifdef _WIN32
#include <malloc.h>
#endif

namespace corridorkey {

// SIMD Alignment requirement (64 bytes for AVX-512 / Cache lines)
inline constexpr size_t MEMORY_ALIGNMENT = 64;

/**
 * @brief Error codes for library operations.
 */
enum class ErrorCode : std::uint8_t {
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
    ErrorCode code = ErrorCode::Success;
    std::string message = "";
};

/**
 * @brief Result type for robust error handling (C++20 compatible).
 * Simple polyfill for std::expected.
 */
template <typename T>
struct Unexpected {
    T error_value;
    explicit Unexpected(T err) : error_value(std::move(err)) {}
};

template <typename T>
class Result {
   public:
    Result(T val) : m_data(std::move(val)) {}
    Result(Unexpected<Error> err) : m_data(std::move(err.error_value)) {}
    Result(Error err) : m_data(std::move(err)) {}

    [[nodiscard]] bool has_value() const {
        return std::holds_alternative<T>(m_data);
    }
    [[nodiscard]] bool has_error() const {
        return std::holds_alternative<Error>(m_data);
    }

    [[nodiscard]] const T& value() const {
        return std::get<T>(m_data);
    }
    [[nodiscard]] T& value() {
        return std::get<T>(m_data);
    }

    [[nodiscard]] const Error& error() const {
        return std::get<Error>(m_data);
    }

    T& operator*() {
        return value();
    }
    const T& operator*() const {
        return value();
    }
    T* operator->() {
        return &value();
    }
    const T* operator->() const {
        return &value();
    }

    explicit operator bool() const {
        return has_value();
    }

   private:
    std::variant<T, Error> m_data;
};

// Specialization for Result<void>
template <>
class Result<void> {
   public:
    Result() = default;
    Result(Unexpected<Error> err) : m_error(std::move(err.error_value)) {}
    Result(Error err) : m_error(std::move(err)) {}

    [[nodiscard]] bool has_value() const {
        return !m_error.has_value();
    }
    [[nodiscard]] bool has_error() const {
        return m_error.has_value();
    }

    [[nodiscard]] const Error& error() const {
        return *m_error;
    }

    explicit operator bool() const {
        return has_value();
    }

   private:
    std::optional<Error> m_error = std::nullopt;
};

/**
 * @brief Hardware backends supported by the runtime.
 */
enum class Backend : std::uint8_t { Auto, CPU, CUDA, TensorRT, CoreML, DirectML };

/**
 * @brief Information about a detected hardware device.
 */
struct DeviceInfo {
    std::string name = "";
    int64_t available_memory_mb = 0;
    Backend backend = Backend::Auto;
};

/**
 * @brief Simple rectangle for ROI operations.
 */
struct Rect {
    int x_pos = 0;
    int y_pos = 0;
    int width = 0;
    int height = 0;
};

/**
 * @brief Simple image view for passing data without copies.
 * Inspired by std::mdspan, this holds a pointer to data and dimensions.
 */
struct Image {
    int width = 0;
    int height = 0;
    int channels = 0;
    std::span<float> data = {};

    [[nodiscard]] bool empty() const {
        return data.empty();
    }

    // Multidimensional accessor: img(y, x, c)
    inline float& operator()(int y_pos, int x_pos, int channel = 0) {
        return data[(static_cast<size_t>(y_pos) * width + x_pos) * channels + channel];
    }

    inline const float& operator()(int y_pos, int x_pos, int channel = 0) const {
        return data[(static_cast<size_t>(y_pos) * width + x_pos) * channels + channel];
    }
};

/**
 * @brief Owned image data with guaranteed SIMD alignment.
 */
class ImageBuffer {
   public:
    ImageBuffer() : m_width(0), m_height(0), m_channels(0), m_ptr(nullptr), m_data({}) {}

    ImageBuffer(int width, int height, int channels)
        : m_width(width), m_height(height), m_channels(channels) {
        const size_t size = static_cast<size_t>(width) * height * channels;
        if (size == 0) {
            m_ptr = nullptr;
            m_data = {};
            return;
        }
#ifdef _WIN32
        m_ptr = static_cast<float*>(_aligned_malloc(size * sizeof(float), MEMORY_ALIGNMENT));
#else
        if (posix_memalign(reinterpret_cast<void**>(&m_ptr), MEMORY_ALIGNMENT,
                           size * sizeof(float)) != 0) {
            m_ptr = nullptr;
        }
#endif
        if (m_ptr != nullptr) {
            m_data = std::span<float>(m_ptr, size);
        }
    }

    ~ImageBuffer() {
        if (m_ptr != nullptr) {
#ifdef _WIN32
            _aligned_free(m_ptr);
#else
            free(m_ptr);
#endif
        }
    }

    ImageBuffer(const ImageBuffer&) = delete;
    ImageBuffer& operator=(const ImageBuffer&) = delete;

    ImageBuffer(ImageBuffer&& other) noexcept
        : m_width(other.m_width),
          m_height(other.m_height),
          m_channels(other.m_channels),
          m_ptr(other.m_ptr),
          m_data(other.m_data) {
        other.m_ptr = nullptr;
        other.m_data = {};
    }

    ImageBuffer& operator=(ImageBuffer&& other) noexcept {
        if (this != &other) {
            if (m_ptr != nullptr) {
#ifdef _WIN32
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

    [[nodiscard]] Image view() {
        return {m_width, m_height, m_channels, m_data};
    }
    [[nodiscard]] Image const_view() const {
        return {m_width, m_height, m_channels, m_data};
    }

   private:
    int m_width = 0;
    int m_height = 0;
    int m_channels = 0;
    float* m_ptr = nullptr;
    std::span<float> m_data = {};
};

/**
 * @brief Parameters to control the inference and post-processing.
 */
struct InferenceParams {
    int target_resolution = 0;  // 0 = Auto-detect based on hardware
    float despill_strength = 1.0F;
    bool auto_despeckle = true;
    int despeckle_size = 400;
    float refiner_scale = 1.0F;
    bool input_is_linear = false;

    // Batching (GPU efficiency)
    int batch_size = 1;

    // Tiling Inference (High-Res support)
    bool enable_tiling = false;
    int tile_padding = 32;  // Overlap in pixels to blend seams
};

/**
 * @brief Results of a single frame inference.
 * These hold buffers, but the FrameResult itself can be moved easily.
 */
struct FrameResult {
    ImageBuffer alpha;
    ImageBuffer foreground;
    ImageBuffer processed;  // Premultiplied RGBA (VFX output)
    ImageBuffer composite;  // Preview on checkerboard (PNG)
};

}  // namespace corridorkey
