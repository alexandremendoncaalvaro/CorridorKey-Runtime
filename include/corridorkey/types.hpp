#pragma once

#include <corridorkey/api_export.hpp>
#include <cstdint>
#include <functional>
#include <memory>
#include <nlohmann/json.hpp>
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
enum class Backend : std::uint8_t { Auto, CPU, CUDA, TensorRT, CoreML, DirectML, MLX };
/**
 * @brief Output encoding policy for video exports.
 */
enum class VideoOutputMode : std::uint8_t { Lossless, Balanced };

/**
 * @brief Information about a detected hardware device.
 */
struct DeviceInfo {
    std::string name = "";
    int64_t available_memory_mb = 0;
    Backend backend = Backend::Auto;
};

/**
 * @brief Structured information about an automatic backend fallback.
 */
struct BackendFallbackInfo {
    Backend requested_backend = Backend::Auto;
    Backend selected_backend = Backend::Auto;
    std::string reason = "";
};

/**
 * @brief Runtime capabilities exposed to the CLI, future GUI, and diagnostics.
 */
struct RuntimeCapabilities {
    std::string platform = "";
    bool apple_silicon = false;
    bool coreml_available = false;
    bool mlx_probe_available = false;
    bool cpu_fallback_available = false;
    bool videotoolbox_available = false;
    bool tiling_supported = true;
    bool batching_supported = true;
    std::vector<Backend> supported_backends = {};
    VideoOutputMode default_video_mode = VideoOutputMode::Lossless;
    std::string default_video_container = "";
    std::string default_video_encoder = "";
    bool lossless_video_available = false;
    std::string lossless_video_unavailable_reason = "";
};

/**
 * @brief Aggregated timing data for a named stage in the runtime pipeline.
 */
struct StageTiming {
    std::string name = "";
    double total_ms = 0.0;
    std::uint64_t sample_count = 0;
    std::uint64_t work_units = 0;
};

/**
 * @brief Callback used by diagnostics to collect per-stage timing samples.
 */
using StageTimingCallback = std::function<void(const StageTiming& timing)>;

/**
 * @brief Structured events emitted by long-running jobs.
 */
enum class JobEventType : std::uint8_t {
    JobStarted,
    BackendSelected,
    Progress,
    Warning,
    ArtifactWritten,
    Completed,
    Failed,
    Cancelled
};

/**
 * @brief Structured event payload for CLI NDJSON and future GUI bridges.
 */
struct JobEvent {
    JobEventType type = JobEventType::Progress;
    std::string phase = "";
    float progress = 0.0F;
    Backend backend = Backend::Auto;
    std::string message = "";
    std::string artifact_path = "";
    std::optional<Error> error = std::nullopt;
    std::optional<BackendFallbackInfo> fallback = std::nullopt;
    std::vector<StageTiming> timings = {};
    nlohmann::json metrics = nlohmann::json::object();
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
    bool auto_despeckle = false;
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
 * @brief Parameters to control video output encoding policy.
 */
struct VideoOutputOptions {
    VideoOutputMode mode = VideoOutputMode::Lossless;
    bool allow_lossy_fallback = false;
    std::string requested_container = "";
};

/**
 * @brief Built-in model catalog entry shared by CLI diagnostics and future GUIs.
 */
struct ModelCatalogEntry {
    std::string variant = "";
    int resolution = 0;
    std::string filename = "";
    std::string artifact_family = "";
    std::string recommended_backend = "";
    std::string description = "";
    std::string download_url = "";
    std::string intended_use = "";
    bool validated_for_macos = false;
    bool packaged_for_macos = false;
    bool packaged_for_windows = false;
    std::vector<std::string> validated_platforms = {};
    std::vector<std::string> intended_platforms = {};
    std::vector<std::string> validated_hardware_tiers = {};
};

/**
 * @brief Built-in processing preset shared by CLI diagnostics and future GUIs.
 */
struct PresetDefinition {
    std::string id = "";
    std::string name = "";
    std::string description = "";
    InferenceParams params = {};
    std::string recommended_model = "";
    std::string intended_use = "";
    bool default_for_macos = false;
    bool default_for_windows = false;
    std::vector<std::string> validated_platforms = {};
    std::vector<std::string> intended_platforms = {};
    std::vector<std::string> validated_hardware_tiers = {};
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
