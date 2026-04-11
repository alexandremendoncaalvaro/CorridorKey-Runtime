#include "inference_session.hpp"

#include <corridorkey/detail/constants.hpp>
#include <cstdlib>
#include <exception>
#include <new>
#include <string_view>
#include <unordered_map>

#if defined(_WIN32)
#if __has_include(<onnxruntime/core/providers/nv_tensorrt_rtx/nv_provider_options.h>)
#include <onnxruntime/core/providers/nv_tensorrt_rtx/nv_provider_options.h>
#define CORRIDORKEY_HAS_NV_TENSORRT_RTX_OPTIONS 1
#endif
#if __has_include(<onnxruntime/core/providers/winml/winml_provider_factory.h>)
#include <onnxruntime/core/providers/winml/winml_provider_factory.h>
#define CORRIDORKEY_HAS_WINML_OPTIONS 1
#endif
#if __has_include(<onnxruntime/core/providers/openvino/openvino_provider_factory.h>)
#include <onnxruntime/core/providers/openvino/openvino_provider_factory.h>
#define CORRIDORKEY_HAS_OPENVINO_OPTIONS 1
#endif
#endif

#include <fstream>
#include <mutex>
#include <optional>

#include "coarse_to_fine_policy.hpp"
#include "common/parallel_for.hpp"
#include "common/runtime_paths.hpp"
#include "common/srgb_lut.hpp"
#include "common/stage_profiler.hpp"
#include "inference_output_validation.hpp"
#include "inference_session_metadata.hpp"
#include "mlx_session.hpp"
#include "ort_process_context.hpp"
#include "post_process/color_utils.hpp"
#include "post_process/despeckle.hpp"
#include "post_process/despill.hpp"
#include "post_process/source_passthrough.hpp"
#include "session_cache_policy.hpp"
#include "session_policy.hpp"
#include "tile_blend.hpp"

namespace {
void debug_log(const std::string& message) {
#ifdef _WIN32
    char* local_app_data = nullptr;
    size_t len = 0;
    if (_dupenv_s(&local_app_data, &len, "LOCALAPPDATA") == 0 && local_app_data != nullptr) {
        std::filesystem::path log_path =
            std::filesystem::path(local_app_data) / "CorridorKey" / "Logs" / "ofx.log";
        static std::mutex log_mutex;
        std::lock_guard<std::mutex> lock(log_mutex);
        std::ofstream log_file(log_path, std::ios::app);
        if (log_file.is_open()) {
            std::time_t now = std::time(nullptr);
            char buf[32];
            ctime_s(buf, sizeof(buf), &now);
            std::string ts(buf);
            if (!ts.empty() && ts.back() == '\n') ts.pop_back();
            log_file << ts << " [InferenceSession] " << message << std::endl;
        }
        free(local_app_data);
    }
#elif defined(__APPLE__)
    auto log_dir = corridorkey::common::default_logs_root();
    std::error_code ec;
    std::filesystem::create_directories(log_dir, ec);
    if (!ec) {
        auto log_path = log_dir / "ofx.log";
        static std::mutex log_mutex;
        std::lock_guard<std::mutex> lock(log_mutex);
        std::ofstream log_file(log_path, std::ios::app);
        if (log_file.is_open()) {
            std::time_t now = std::time(nullptr);
            char buf[32];
            auto* tm = std::localtime(&now);
            std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", tm);
            log_file << buf << " [InferenceSession] " << message << std::endl;
        }
    }
#else
    (void)message;
#endif
}
}  // namespace

namespace corridorkey {

namespace {

template <typename T>
class AlignedTensorBuffer {
   public:
    AlignedTensorBuffer() = default;

    explicit AlignedTensorBuffer(std::size_t element_count) {
        resize(element_count);
    }

    ~AlignedTensorBuffer() {
        release();
    }

    AlignedTensorBuffer(const AlignedTensorBuffer&) = delete;
    AlignedTensorBuffer& operator=(const AlignedTensorBuffer&) = delete;

    AlignedTensorBuffer(AlignedTensorBuffer&& other) noexcept
        : m_size(other.m_size), m_data(other.m_data) {
        other.m_size = 0;
        other.m_data = nullptr;
    }

    AlignedTensorBuffer& operator=(AlignedTensorBuffer&& other) noexcept {
        if (this == &other) {
            return *this;
        }

        release();
        m_size = other.m_size;
        m_data = other.m_data;
        other.m_size = 0;
        other.m_data = nullptr;
        return *this;
    }

    void resize(std::size_t element_count) {
        if (m_size == element_count && m_data != nullptr) {
            return;
        }

        release();
        if (element_count == 0) {
            return;
        }

#ifdef _WIN32
        m_data = static_cast<T*>(_aligned_malloc(element_count * sizeof(T), MEMORY_ALIGNMENT));
#else
        void* storage = nullptr;
        if (posix_memalign(&storage, MEMORY_ALIGNMENT, element_count * sizeof(T)) == 0) {
            m_data = static_cast<T*>(storage);
        }
#endif
        if (m_data == nullptr) {
            throw std::bad_alloc();
        }
        m_size = element_count;
    }

    [[nodiscard]] T* data() {
        return m_data;
    }

    [[nodiscard]] const T* data() const {
        return m_data;
    }

    [[nodiscard]] std::size_t size() const {
        return m_size;
    }

   private:
    void release() {
        if (m_data == nullptr) {
            return;
        }
#ifdef _WIN32
        _aligned_free(m_data);
#else
        free(m_data);
#endif
        m_data = nullptr;
        m_size = 0;
    }

    std::size_t m_size = 0;
    T* m_data = nullptr;
};

struct BoundTensorStorage {
    ONNXTensorElementDataType element_type = ONNX_TENSOR_ELEMENT_DATA_TYPE_UNDEFINED;
    std::vector<int64_t> shape = {};
    AlignedTensorBuffer<float> fp32_storage = {};
    AlignedTensorBuffer<Ort::Float16_t> fp16_storage = {};
    Ort::Value tensor{nullptr};

    [[nodiscard]] std::size_t element_count() const {
        std::size_t count = 1;
        for (const auto dim : shape) {
            count *= static_cast<std::size_t>(dim);
        }
        return count;
    }

    [[nodiscard]] bool matches(ONNXTensorElementDataType candidate_type,
                               const std::vector<int64_t>& candidate_shape) const {
        return element_type == candidate_type && shape == candidate_shape && tensor != nullptr;
    }

    void reset(const Ort::MemoryInfo& memory_info, ONNXTensorElementDataType candidate_type,
               const std::vector<int64_t>& candidate_shape) {
        element_type = candidate_type;
        shape = candidate_shape;
        const std::size_t count = element_count();
        if (candidate_type == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT16) {
            fp16_storage.resize(count);
            tensor = Ort::Value::CreateTensor<Ort::Float16_t>(
                memory_info, fp16_storage.data(), count, shape.data(), shape.size());
            return;
        }

        fp32_storage.resize(count);
        tensor = Ort::Value::CreateTensor<float>(memory_info, fp32_storage.data(), count,
                                                 shape.data(), shape.size());
    }
};

Result<std::vector<int64_t>> resolve_io_binding_shape(const std::vector<int64_t>& shape_template,
                                                      int64_t batch_size, int64_t height,
                                                      int64_t width,
                                                      std::string_view tensor_name) {
    if (shape_template.size() != 4) {
        return Unexpected(Error{
            ErrorCode::HardwareNotSupported,
            "I/O binding expects 4D tensors for " + std::string(tensor_name) + "."});
    }

    std::vector<int64_t> resolved = shape_template;
    const auto resolve_dimension = [&](std::size_t index, int64_t expected,
                                       std::string_view label) -> Result<void> {
        if (resolved[index] < 0) {
            resolved[index] = expected;
            return {};
        }
        if (resolved[index] != expected) {
            return Unexpected(Error{
                ErrorCode::HardwareNotSupported,
                "I/O binding shape mismatch for " + std::string(tensor_name) + " " +
                    std::string(label) + ": expected " + std::to_string(expected) + ", got " +
                    std::to_string(resolved[index]) + "."});
        }
        return {};
    };

    if (auto batch_res = resolve_dimension(0, batch_size, "batch"); !batch_res) {
        return Unexpected(batch_res.error());
    }
    if (resolved[1] <= 0) {
        return Unexpected(
            Error{ErrorCode::HardwareNotSupported,
                  "I/O binding channel count must be explicit for " + std::string(tensor_name) +
                      "."});
    }
    if (auto height_res = resolve_dimension(2, height, "height"); !height_res) {
        return Unexpected(height_res.error());
    }
    if (auto width_res = resolve_dimension(3, width, "width"); !width_res) {
        return Unexpected(width_res.error());
    }

    return resolved;
}

bool supports_bound_tensor_type(ONNXTensorElementDataType element_type) {
    return element_type == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT ||
           element_type == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT16;
}

Result<void> validate_output_values(std::span<const float> values, std::string_view label) {
    const auto validation = core::validate_finite_values(values, label);
    if (!validation) {
        debug_log(validation.error().message);
    }
    return validation;
}

Result<void> validate_output_image(Image image, std::string_view label) {
    const auto validation = core::validate_finite_image(image, label);
    if (!validation) {
        debug_log(validation.error().message);
    }
    return validation;
}

bool should_log_output_stats(DeviceInfo device, int recommended_resolution) {
    return device.backend == Backend::TensorRT && recommended_resolution > 1024;
}

struct MaterializedOutputTensor {
    std::vector<int64_t> shape = {};
    std::vector<float> fp32_storage = {};
    float* values = nullptr;
    std::size_t image_stride = 0;

    [[nodiscard]] std::span<const float> span(std::size_t image_count) const {
        return std::span<const float>(values, image_stride * image_count);
    }
};

Result<MaterializedOutputTensor> materialize_output_tensor(
    Ort::Value& tensor, std::size_t image_count, DeviceInfo device, int recommended_resolution,
    std::string_view raw_label, std::string_view debug_label) {
    MaterializedOutputTensor output;

    auto tensor_info = tensor.GetTensorTypeAndShapeInfo();
    auto element_type = tensor_info.GetElementType();
    output.shape = tensor_info.GetShape();
    if (output.shape.size() < 4) {
        return Unexpected(
            Error{ErrorCode::InferenceFailed,
                  "Model output tensor for " + std::string(raw_label) + " did not expose NCHW."});
    }

    debug_log(std::string(debug_label) + " output element type: " +
              std::to_string(static_cast<int>(element_type)));

    output.image_stride = static_cast<std::size_t>(output.shape[1]) *
                          static_cast<std::size_t>(output.shape[2]) *
                          static_cast<std::size_t>(output.shape[3]);

    if (element_type == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT16) {
        debug_log("Converting " + std::string(debug_label) + " output from FP16 to FP32");
        const Ort::Float16_t* fp16_values = tensor.GetTensorData<Ort::Float16_t>();
        const std::size_t total_elements = image_count * output.image_stride;
        output.fp32_storage.resize(total_elements);
        for (std::size_t index = 0; index < total_elements; ++index) {
            output.fp32_storage[index] = fp16_values[index].ToFloat();
        }
        output.values = output.fp32_storage.data();
    } else {
        output.values = tensor.GetTensorMutableData<float>();
    }

    const auto output_values = output.span(image_count);
    if (should_log_output_stats(device, recommended_resolution)) {
        const auto analysis = core::analyze_finite_values(output_values, raw_label);
        if (!analysis) {
            debug_log(analysis.error().message);
            return Unexpected(analysis.error());
        }
        debug_log(core::format_numeric_stats(raw_label, *analysis));
        return output;
    }

    const auto validation = validate_output_values(output_values, raw_label);
    if (!validation) {
        return Unexpected(validation.error());
    }

    return output;
}

void resize_model_output(const float* source, int source_width, int source_height,
                         int source_channels, Image destination, bool use_lanczos,
                         ColorUtils::State& state) {
    if (use_lanczos) {
        ColorUtils::resize_lanczos_from_planar_into(source, source_width, source_height,
                                                    source_channels, destination, state);
        return;
    }

    ColorUtils::resize_from_planar_into(source, source_width, source_height, source_channels,
                                        destination);
}

Result<void> finalize_output_image(DeviceInfo device, int recommended_resolution, Image image,
                                   std::string_view label) {
    if (should_log_output_stats(device, recommended_resolution)) {
        const auto analysis = core::analyze_finite_values(image.data, label);
        if (!analysis) {
            debug_log(analysis.error().message);
            return Unexpected(analysis.error());
        }
        debug_log(core::format_numeric_stats(label, *analysis));
        return {};
    }

    const auto validation = validate_output_image(image, label);
    if (!validation) {
        return Unexpected(validation.error());
    }

    return {};
}

constexpr const char* kDisableCpuEpFallbackConfig = "session.disable_cpu_ep_fallback";
constexpr const char* kUseEnvAllocatorsConfig = "session.use_env_allocators";

#ifdef _WIN32
constexpr const char* kTensorRtRtxExecutionProvider = "NvTensorRTRTXExecutionProvider";
using OrtDmlAppendExecutionProviderFn = OrtStatus*(ORT_API_CALL*)(OrtSessionOptions*, int);

namespace tensorrt_rtx_option_names {
#if defined(CORRIDORKEY_HAS_NV_TENSORRT_RTX_OPTIONS)
constexpr const char* kDeviceId = onnxruntime::nv::provider_option_names::kDeviceId;
constexpr const char* kDumpSubgraphs = onnxruntime::nv::provider_option_names::kDumpSubgraphs;
constexpr const char* kDetailedBuildLog = onnxruntime::nv::provider_option_names::kDetailedBuildLog;
constexpr const char* kRuntimeCacheFile = onnxruntime::nv::provider_option_names::kRuntimeCacheFile;
constexpr const char* kMaxWorkspaceSize = onnxruntime::nv::provider_option_names::kMaxWorkspaceSize;
#else
constexpr const char* kDeviceId = "device_id";
constexpr const char* kDumpSubgraphs = "nv_dump_subgraphs";
constexpr const char* kDetailedBuildLog = "nv_detailed_build_log";
constexpr const char* kRuntimeCacheFile = "nv_runtime_cache_path";
constexpr const char* kMaxWorkspaceSize = "nv_max_workspace_size";
#endif
}  // namespace tensorrt_rtx_option_names

OrtDmlAppendExecutionProviderFn resolve_directml_append_function() {
    HMODULE runtime_module = GetModuleHandleW(L"onnxruntime.dll");
    if (runtime_module == nullptr) {
        runtime_module = LoadLibraryW(L"onnxruntime.dll");
    }
    if (runtime_module == nullptr) {
        return nullptr;
    }

    auto symbol = GetProcAddress(runtime_module, "OrtSessionOptionsAppendExecutionProvider_DML");
    return reinterpret_cast<OrtDmlAppendExecutionProviderFn>(symbol);
}

void append_directml_execution_provider(Ort::SessionOptions& session_options, int device_index) {
    // Microsoft officially recommends disabling the memory pattern for DirectML
    // to allow the DML EP to handle memory allocation and alignment natively.
    session_options.DisableMemPattern();
    session_options.SetExecutionMode(ExecutionMode::ORT_SEQUENTIAL);

    if (auto append = resolve_directml_append_function(); append != nullptr) {
        debug_log("Adding DirectML execution provider via exported ORT DML API for device index: " +
                  std::to_string(device_index));
        Ort::ThrowOnError(append(session_options, device_index));
        return;
    }

    debug_log("Adding DirectML execution provider via generic provider options for device index: " +
              std::to_string(device_index));
    std::unordered_map<std::string, std::string> dml_options = {
        {std::string(corridorkey::detail::session_options::DEVICE_ID),
         std::to_string(device_index)}};
    session_options.AppendExecutionProvider(std::string(corridorkey::detail::providers::DIRECTML),
                                            dml_options);
}

void override_windows_universal_free_dimensions(Ort::SessionOptions& session_options,
                                                Backend backend) {
    if (backend != Backend::DirectML && backend != Backend::WindowsML) {
        return;
    }

    // The DirectML guidance recommends overriding free dimensions during session creation so the
    // provider can optimize and specialize the graph for the actual runtime shape.
    debug_log("Overriding free dimension 'batch_size' to 1 for Windows universal backend");
    Ort::ThrowOnError(
        Ort::GetApi().AddFreeDimensionOverrideByName(session_options, "batch_size", 1));
}
#endif

#ifdef __APPLE__
void append_coreml_execution_provider(Ort::SessionOptions& session_options) {
#if ORT_API_VERSION >= 24
    std::unordered_map<std::string, std::string> provider_options = {
        {kCoremlProviderOption_MLComputeUnits, "ALL"},
        {kCoremlProviderOption_RequireStaticInputShapes, "1"},
    };

    if (auto cache_root = common::coreml_model_cache_root(); cache_root.has_value()) {
        std::error_code error;
        std::filesystem::create_directories(*cache_root, error);
        if (!error) {
            provider_options.emplace(kCoremlProviderOption_ModelCacheDirectory,
                                     cache_root->string());
        }
    }

    session_options.AppendExecutionProvider(std::string(corridorkey::detail::providers::COREML_API),
                                            provider_options);
#else
    uint32_t coreml_flags = COREML_FLAG_ONLY_ALLOW_STATIC_INPUT_SHAPES;
    Ort::ThrowOnError(
        OrtSessionOptionsAppendExecutionProvider_CoreML(session_options, coreml_flags));
#endif
}
#endif

#ifdef _WIN32
int extract_model_resolution(const std::filesystem::path& model_path) {
    return core::infer_model_resolution_from_path(model_path).value_or(1024);
}

void append_tensorrt_rtx_execution_provider(Ort::SessionOptions& session_options,
                                            const std::filesystem::path& model_path) {
    debug_log("Configuring TensorRT RTX execution provider");

    int model_res = extract_model_resolution(model_path);
    debug_log("Detected model resolution: " + std::to_string(model_res));

    // Workspace scales proportionally with spatial resolution. TensorRT uses workspace as an upper
    // bound for tactic discovery during engine build; it only allocates what each tactic actually
    // needs at runtime.
    constexpr const char* kWorkspace2GB = "2147483648";  // 512, 768, 1024
    constexpr const char* kWorkspace4GB = "4294967296";  // 1536
    constexpr const char* kWorkspace8GB = "8589934592";  // 2048
    constexpr const char* kProfileMinShapes = "nv_profile_min_shapes";
    constexpr const char* kProfileOptShapes = "nv_profile_opt_shapes";
    constexpr const char* kProfileMaxShapes = "nv_profile_max_shapes";
    const char* workspace_size = kWorkspace2GB;
    if (model_res >= 2048) {
        workspace_size = kWorkspace8GB;
    } else if (model_res >= 1536) {
        workspace_size = kWorkspace4GB;
    }

    std::unordered_map<std::string, std::string> provider_options = {
        {tensorrt_rtx_option_names::kDeviceId, "0"},
        {tensorrt_rtx_option_names::kMaxWorkspaceSize, workspace_size},
    };

    const std::string profile_shapes =
        "input_rgb_hint:1x4x" + std::to_string(model_res) + "x" + std::to_string(model_res);
    provider_options.emplace(kProfileMinShapes, profile_shapes);
    provider_options.emplace(kProfileOptShapes, profile_shapes);
    provider_options.emplace(kProfileMaxShapes, profile_shapes);
    debug_log("TensorRT RTX profile shapes: " + profile_shapes);

    debug_log("Setting up runtime cache");
    if (auto runtime_cache_dir = common::tensorrt_rtx_runtime_cache_path(model_path);
        runtime_cache_dir.has_value()) {
        debug_log("Runtime cache dir: " + runtime_cache_dir->string());
        std::error_code error;
        std::filesystem::create_directories(*runtime_cache_dir, error);
        if (!error) {
            provider_options.emplace(tensorrt_rtx_option_names::kRuntimeCacheFile,
                                     runtime_cache_dir->string());
            debug_log("Runtime cache configured successfully");
        } else {
            debug_log("Failed to create runtime cache dir: " + error.message());
        }
    }

    if (auto dump_subgraphs =
            common::environment_variable_copy("CORRIDORKEY_TENSORRT_RTX_DUMP_SUBGRAPHS");
        dump_subgraphs.has_value() && std::string_view(*dump_subgraphs) == "1") {
        provider_options.emplace(tensorrt_rtx_option_names::kDumpSubgraphs, "1");
        debug_log("Subgraph dumping enabled");
    }

    if (auto build_log = common::environment_variable_copy("CORRIDORKEY_TENSORRT_RTX_DETAILED_LOG");
        build_log.has_value() && std::string_view(*build_log) == "1") {
        provider_options.emplace(tensorrt_rtx_option_names::kDetailedBuildLog, "1");
        debug_log("Detailed build logging enabled");
    }

    debug_log("Appending execution provider to session options");
    session_options.AppendExecutionProvider(kTensorRtRtxExecutionProvider, provider_options);
    debug_log("Execution provider appended successfully");
}
#endif

void remove_cached_model(const std::filesystem::path& cache_path) {
    std::error_code error;
    std::filesystem::remove(cache_path, error);
}

void extract_tile_rows(const Image& source_rgb, const Image& source_hint, Image rgb_tile,
                       Image hint_tile, int y_start, int x_start, int y_begin, int y_end) {
    for (int y = y_begin; y < y_end; ++y) {
        for (int x = 0; x < rgb_tile.width; ++x) {
            for (int channel = 0; channel < rgb_tile.channels; ++channel) {
                rgb_tile(y, x, channel) = source_rgb(y_start + y, x_start + x, channel);
            }
            hint_tile(y, x) = source_hint(y_start + y, x_start + x);
        }
    }
}

void accumulate_tile_rows(const Image& mask, const FrameResult& tile_result, Image acc_alpha,
                          Image acc_fg, Image acc_weight, int y_start, int x_start,
                          int image_height, int image_width, int overlap, int y_begin, int y_end) {
    Image tile_alpha = tile_result.alpha.const_view();
    Image tile_foreground = tile_result.foreground.const_view();
    const bool include_foreground = !acc_fg.empty() && !tile_foreground.empty();
    const bool touches_left = x_start == 0;
    const bool touches_top = y_start == 0;
    const bool touches_right = x_start + mask.width >= image_width;
    const bool touches_bottom = y_start + mask.height >= image_height;
    const bool needs_edge_aware_weights =
        touches_left || touches_top || touches_right || touches_bottom;

    for (int y = y_begin; y < y_end; ++y) {
        int global_y = y_start + y;
        if (global_y >= image_height) {
            break;
        }

        for (int x = 0; x < mask.width; ++x) {
            int global_x = x_start + x;
            if (global_x >= image_width) {
                break;
            }

            float weight = mask(y, x);
            if (needs_edge_aware_weights) {
                weight = core::edge_aware_tile_weight(x, y, mask.width, overlap, touches_left,
                                                      touches_right, touches_top, touches_bottom);
            }
            acc_weight(global_y, global_x) += weight;
            acc_alpha(global_y, global_x) += tile_alpha(y, x) * weight;
            if (include_foreground) {
                for (int channel = 0; channel < 3; ++channel) {
                    acc_fg(global_y, global_x, channel) += tile_foreground(y, x, channel) * weight;
                }
            }
        }
    }
}

void normalize_accumulators(Image acc_alpha, Image acc_fg, const Image& acc_weight, int y_begin,
                            int y_end) {
    const bool include_foreground = !acc_fg.empty();
    for (int y = y_begin; y < y_end; ++y) {
        size_t row_offset = static_cast<size_t>(y) * static_cast<size_t>(acc_alpha.width);
        for (int x = 0; x < acc_alpha.width; ++x) {
            size_t pixel_index = row_offset + static_cast<size_t>(x);
            float weight = acc_weight.data[pixel_index];
            if (weight <= 0.0001f) {
                continue;
            }

            acc_alpha.data[pixel_index] /= weight;
            if (include_foreground) {
                size_t fg_index = pixel_index * 3;
                acc_fg.data[fg_index] /= weight;
                acc_fg.data[fg_index + 1] /= weight;
                acc_fg.data[fg_index + 2] /= weight;
            }
        }
    }
}

}  // namespace

struct InferenceSession::BoundIoState {
    explicit BoundIoState(Ort::Session& session) : binding(session) {}

    Ort::IoBinding binding{nullptr};
    BoundTensorStorage alpha_output = {};
    std::optional<BoundTensorStorage> fg_output = std::nullopt;
    std::vector<int64_t> input_shape = {};
};

InferenceSession::InferenceSession(DeviceInfo device) : m_device(std::move(device)) {
    // Default recommended resolution. High-level layers (App)
    // will typically override this via InferenceParams.
    m_recommended_resolution = 512;
}

InferenceSession::~InferenceSession() = default;

Result<Ort::Value> InferenceSession::create_input_tensor(float* planar_data,
                                                         std::size_t element_count,
                                                         const std::vector<int64_t>& shape) {
    auto memory_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);

    if (m_input_element_type == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT16) {
        m_fp16_pool.resize(element_count);
        for (std::size_t index = 0; index < element_count; ++index) {
            m_fp16_pool[index] = Ort::Float16_t(planar_data[index]);
        }
        return Ort::Value::CreateTensor<Ort::Float16_t>(memory_info, m_fp16_pool.data(),
                                                        element_count, shape.data(), shape.size());
    }

    if (m_input_element_type != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) {
        return Unexpected(Error{
            ErrorCode::HardwareNotSupported,
            "Unsupported model input type for I/O binding path: " +
                std::to_string(m_input_element_type)});
    }

    return Ort::Value::CreateTensor<float>(memory_info, planar_data, element_count, shape.data(),
                                           shape.size());
}

Result<InferenceSession::BoundIoState*> InferenceSession::ensure_bound_io_state(
    const std::vector<int64_t>& input_shape) {
    if (input_shape.size() != 4) {
        return Unexpected(
            Error{ErrorCode::HardwareNotSupported, "I/O binding requires a 4D input tensor."});
    }

    if (m_output_node_dims.empty() || m_output_element_types.empty()) {
        return Unexpected(Error{ErrorCode::HardwareNotSupported,
                                "I/O binding requires output metadata to be available."});
    }

    const bool has_foreground_output = m_output_node_names_ptr.size() > 1 &&
                                       m_output_node_dims.size() > 1 &&
                                       m_output_element_types.size() > 1;

    if (m_bound_io_state != nullptr && m_bound_io_state->input_shape == input_shape &&
        m_bound_io_state->fg_output.has_value() == has_foreground_output) {
        return m_bound_io_state.get();
    }

    if (!supports_bound_tensor_type(m_output_element_types[0])) {
        return Unexpected(Error{
            ErrorCode::HardwareNotSupported,
            "Unsupported alpha output type for I/O binding: " +
                std::to_string(m_output_element_types[0])});
    }
    if (has_foreground_output && !supports_bound_tensor_type(m_output_element_types[1])) {
        return Unexpected(Error{
            ErrorCode::HardwareNotSupported,
            "Unsupported foreground output type for I/O binding: " +
                std::to_string(m_output_element_types[1])});
    }

    auto state = std::make_unique<BoundIoState>(m_session);
    auto memory_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);

    const auto batch_size = input_shape[0];
    const auto model_height = input_shape[2];
    const auto model_width = input_shape[3];

    const auto alpha_shape_res = resolve_io_binding_shape(
        m_output_node_dims[0], batch_size, model_height, model_width, m_output_node_names[0]);
    if (!alpha_shape_res) {
        return Unexpected(alpha_shape_res.error());
    }

    state->alpha_output.reset(memory_info, m_output_element_types[0], *alpha_shape_res);
    state->binding.BindOutput(m_output_node_names_ptr[0], state->alpha_output.tensor);

    if (has_foreground_output) {
        const auto fg_shape_res = resolve_io_binding_shape(
            m_output_node_dims[1], batch_size, model_height, model_width, m_output_node_names[1]);
        if (!fg_shape_res) {
            return Unexpected(fg_shape_res.error());
        }

        state->fg_output.emplace();
        state->fg_output->reset(memory_info, m_output_element_types[1], *fg_shape_res);
        state->binding.BindOutput(m_output_node_names_ptr[1], state->fg_output->tensor);
    }

    state->input_shape = input_shape;
    m_bound_io_state = std::move(state);
    return m_bound_io_state.get();
}

void InferenceSession::configure_session_options(bool use_optimized_model_cache,
                                                 const SessionCreateOptions& options,
                                                 const std::filesystem::path& model_path) {
#ifndef _WIN32
    (void)model_path;
#endif
    debug_log("Configuring shared ORT thread pools and env allocators");
    // Shared thread pools require per-session thread pools to be disabled. Pair that with
    // `session.use_env_allocators=1` so every ORT session in the process reuses the env allocator.
    m_session_options.DisablePerSessionThreads();
    m_session_options.AddConfigEntry(kUseEnvAllocatorsConfig, "1");

    debug_log("Setting graph optimization level");
    if (use_optimized_model_cache) {
        m_session_options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_DISABLE_ALL);
    } else if (m_device.backend == Backend::DirectML) {
        // Microsoft strongly recommends avoiding ORT_ENABLE_ALL (level 3) for DirectML
        // because it enables CPU-specific memory layout optimizations that crash DML execution.
        m_session_options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_EXTENDED);
    } else {
        m_session_options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
    }

    debug_log("Setting log severity level");
    m_session_options.SetLogSeverityLevel(options.log_severity);

#ifdef _WIN32
    override_windows_universal_free_dimensions(m_session_options, m_device.backend);
#endif

    if (m_device.backend != Backend::CPU) {
        if (options.disable_cpu_ep_fallback) {
            debug_log("Disabling CPU EP fallback");
            m_session_options.AddConfigEntry(kDisableCpuEpFallbackConfig, "1");
        } else {
            // Explicitly enable CPU EP fallback to avoid issues in some DirectML environments
            // where ORT might default to disabling it when an EP is added.
            m_session_options.AddConfigEntry(kDisableCpuEpFallbackConfig, "0");
        }
    }

    debug_log("Configuring execution provider for backend: " +
              std::to_string(static_cast<int>(m_device.backend)));

    switch (m_device.backend) {
        case Backend::CoreML: {
#ifdef __APPLE__
            debug_log("Adding CoreML execution provider");
            append_coreml_execution_provider(m_session_options);
#endif
            break;
        }
        case Backend::CUDA: {
            debug_log("Adding CUDA execution provider");
            OrtCUDAProviderOptions cuda_options;
            cuda_options.device_id = 0;
            m_session_options.AppendExecutionProvider_CUDA(cuda_options);
            break;
        }
        case Backend::TensorRT: {
#ifdef _WIN32
            debug_log("Adding TensorRT RTX execution provider");
            append_tensorrt_rtx_execution_provider(m_session_options, model_path);
            debug_log("TensorRT RTX execution provider added");
#else
            debug_log("Adding TensorRT execution provider");
            OrtTensorRTProviderOptions trt_options;
            trt_options.device_id = 0;
            m_session_options.AppendExecutionProvider_TensorRT(trt_options);
#endif
            break;
        }
#ifdef _WIN32
        case Backend::DirectML: {
            append_directml_execution_provider(m_session_options, m_device.device_index);
            break;
        }
        case Backend::WindowsML: {
            debug_log("Adding WindowsML execution provider");
            // In March 2026, WindowsML EP or adapter handles NPU/GPU auto-selection
            std::unordered_map<std::string, std::string> winml_options = {};
            m_session_options.AppendExecutionProvider("WinML", winml_options);
            break;
        }
        case Backend::OpenVINO: {
            debug_log("Adding OpenVINO execution provider");
            // Intel specific acceleration
            std::unordered_map<std::string, std::string> ov_options = {{"device_type", "AUTO"}};
            m_session_options.AppendExecutionProvider("OpenVINO", ov_options);
            break;
        }
#endif
        case Backend::MLX:
        default:
            debug_log("Using default CPU execution provider");
            break;
    }
}

void InferenceSession::extract_metadata(const std::filesystem::path& model_path) {
    debug_log("Extracting model metadata");
    Ort::AllocatorWithDefaultOptions allocator;

    size_t num_input_nodes = m_session.GetInputCount();
    debug_log("Model has " + std::to_string(num_input_nodes) + " inputs");

    for (size_t i = 0; i < num_input_nodes; i++) {
        auto input_name_ptr = m_session.GetInputNameAllocated(i, allocator);
        m_input_node_names.push_back(input_name_ptr.get());

        auto type_info = m_session.GetInputTypeInfo(i);
        auto tensor_info = type_info.GetTensorTypeAndShapeInfo();
        m_input_node_dims.push_back(tensor_info.GetShape());

        // Capture input element type for FP16 model support
        if (i == 0) {
            m_input_element_type = tensor_info.GetElementType();
            if (auto inferred_resolution = core::infer_model_resolution(m_input_node_dims.back());
                inferred_resolution.has_value()) {
                m_recommended_resolution = *inferred_resolution;
                debug_log("Inferred model resolution from input shape: " +
                          std::to_string(*inferred_resolution));
            } else if (auto filename_resolution =
                           core::infer_model_resolution_from_path(model_path);
                       filename_resolution.has_value()) {
                m_recommended_resolution = *filename_resolution;
                debug_log("Falling back to model filename resolution: " +
                          std::to_string(*filename_resolution));
            }
            debug_log("Input 0 element type: " + std::to_string(m_input_element_type) +
                      " (FLOAT16 is 10, FLOAT is 1)");
        }
    }
    for (const auto& name : m_input_node_names) {
        m_input_node_names_ptr.push_back(name.c_str());
    }

    const bool use_packaged_output_contract = core::should_use_packaged_corridorkey_output_contract(
        model_path, m_device.backend,
        m_input_element_type == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT16);
    size_t num_output_nodes = m_session.GetOutputCount();
    for (size_t i = 0; i < num_output_nodes; ++i) {
        auto output_type_info = m_session.GetOutputTypeInfo(i);
        auto output_tensor_info = output_type_info.GetTensorTypeAndShapeInfo();
        m_output_node_dims.push_back(output_tensor_info.GetShape());
        m_output_element_types.push_back(output_tensor_info.GetElementType());
    }

    if (use_packaged_output_contract) {
        debug_log("Using packaged CorridorKey output contract");
        m_output_node_names.emplace_back(core::k_corridorkey_alpha_output_name);
        m_output_node_names.emplace_back(core::k_corridorkey_fg_output_name);
    } else {
        for (size_t i = 0; i < num_output_nodes; i++) {
            auto output_name_ptr = m_session.GetOutputNameAllocated(i, allocator);
            m_output_node_names.push_back(output_name_ptr.get());
        }
    }
    for (const auto& name : m_output_node_names) {
        m_output_node_names_ptr.push_back(name.c_str());
    }

    m_io_binding_enabled = core::should_enable_io_binding(model_path, m_device.backend) &&
                           m_input_node_dims.size() == 1 && !m_output_node_names_ptr.empty() &&
                           m_output_node_dims.size() >= m_output_node_names_ptr.size() &&
                           m_output_element_types.size() >= m_output_node_names_ptr.size();
    if (m_io_binding_enabled) {
        debug_log("I/O binding enabled for this session");
    }
}

Result<std::unique_ptr<InferenceSession>> InferenceSession::create(
    const std::filesystem::path& model_path, DeviceInfo device, SessionCreateOptions options,
    StageTimingCallback on_stage) {
    fprintf(stderr, "[InferenceSession] Creating session for model: %s\n",
            model_path.string().c_str());

    const auto artifact = common::inspect_model_artifact(model_path);
    if (!artifact.found) {
        return Unexpected(
            Error{ErrorCode::ModelLoadFailed, "Model file not found: " + model_path.string()});
    }
    if (!artifact.usable) {
        return Unexpected(Error{ErrorCode::ModelLoadFailed, artifact.detail});
    }

    DeviceInfo requested_device = device;
    auto ort_process_context = options.ort_process_context;

    try {
        fprintf(stderr, "[InferenceSession] Allocating InferenceSession object\n");
        auto session_ptr =
            std::unique_ptr<InferenceSession>(new InferenceSession(std::move(device)));
        if (requested_device.backend == Backend::MLX) {
            auto mlx_session_res = core::MlxSession::create(model_path, requested_device);
            if (!mlx_session_res) {
                return Unexpected(mlx_session_res.error());
            }
            session_ptr->m_recommended_resolution = (*mlx_session_res)->model_resolution();
            session_ptr->m_mlx_session = std::move(*mlx_session_res);
            return session_ptr;
        }

        if (!ort_process_context) {
            ort_process_context = std::make_shared<core::OrtProcessContext>();
        }
        session_ptr->m_ort_process_context = ort_process_context;

        std::filesystem::path session_model_path = model_path;
        std::optional<std::filesystem::path> optimized_model_path;
        bool using_optimized_model_cache = false;

#ifdef _WIN32
        if (session_ptr->m_device.backend == Backend::TensorRT) {
            if (auto compiled_context_path =
                    common::existing_tensorrt_rtx_compiled_context_model_path(model_path);
                compiled_context_path.has_value()) {
                session_model_path = *compiled_context_path;
            }
        }
#endif

        if (core::use_optimized_model_cache_for_backend(session_ptr->m_device.backend)) {
            optimized_model_path =
                common::optimized_model_cache_path(model_path, session_ptr->m_device.backend);
            if (optimized_model_path.has_value()) {
                std::error_code error;
                std::filesystem::create_directories(optimized_model_path->parent_path(), error);
                if (!error && std::filesystem::exists(*optimized_model_path, error)) {
                    session_model_path = *optimized_model_path;
                    using_optimized_model_cache = true;
                }
            }
        }

        auto* env = common::measure_stage(
            on_stage, "ort_env_acquire",
            [&]() { return &session_ptr->m_ort_process_context->acquire_env(options.log_severity); });

        auto configure_session_for_model =
            [&](InferenceSession& session, const std::filesystem::path& runtime_model_path,
                bool use_cached_model,
                const std::optional<std::filesystem::path>& optimized_output_path) {
                common::measure_stage(
                    on_stage, "ort_session_options",
                    [&]() {
                        session.configure_session_options(use_cached_model, options, model_path);
                        if (!use_cached_model && optimized_output_path.has_value()) {
#ifdef _WIN32
                            session.m_session_options.SetOptimizedModelFilePath(
                                optimized_output_path->wstring().c_str());
#else
                            session.m_session_options.SetOptimizedModelFilePath(
                                optimized_output_path->c_str());
#endif
                        }
                    });

                common::measure_stage(on_stage, "ort_session_create", [&]() {
#ifdef _WIN32
                    session.m_session =
                        Ort::Session(*env, runtime_model_path.wstring().c_str(),
                                     session.m_session_options);
#else
                    session.m_session =
                        Ort::Session(*env, runtime_model_path.c_str(), session.m_session_options);
#endif
                });

                common::measure_stage(on_stage, "ort_metadata_extract",
                                      [&]() { session.extract_metadata(model_path); });
            };

        configure_session_for_model(*session_ptr, session_model_path, using_optimized_model_cache,
                                    optimized_model_path);
        fprintf(stderr, "[InferenceSession] Session created successfully\n");
        return session_ptr;
    } catch (const Ort::Exception& e) {
        if (core::use_optimized_model_cache_for_backend(requested_device.backend)) {
            auto optimized_model_path =
                common::optimized_model_cache_path(model_path, requested_device.backend);
            if (optimized_model_path.has_value() &&
                std::filesystem::exists(*optimized_model_path)) {
                remove_cached_model(*optimized_model_path);
                try {
                    auto session_ptr =
                        std::unique_ptr<InferenceSession>(new InferenceSession(requested_device));
                    session_ptr->m_ort_process_context = ort_process_context;
                    auto* env = common::measure_stage(
                        on_stage, "ort_env_acquire", [&]() {
                            return &session_ptr->m_ort_process_context->acquire_env(
                                options.log_severity);
                        });
                    common::measure_stage(
                        on_stage, "ort_session_options",
                        [&]() {
                            session_ptr->configure_session_options(false, options, model_path);
#ifdef _WIN32
                            session_ptr->m_session_options.SetOptimizedModelFilePath(
                                optimized_model_path->wstring().c_str());
#else
                            session_ptr->m_session_options.SetOptimizedModelFilePath(
                                optimized_model_path->c_str());
#endif
                        });
                    common::measure_stage(on_stage, "ort_session_create", [&]() {
#ifdef _WIN32
                        session_ptr->m_session =
                            Ort::Session(*env, model_path.wstring().c_str(),
                                         session_ptr->m_session_options);
#else
                        session_ptr->m_session =
                            Ort::Session(*env, model_path.c_str(), session_ptr->m_session_options);
#endif
                    });
                    common::measure_stage(on_stage, "ort_metadata_extract",
                                          [&]() { session_ptr->extract_metadata(model_path); });
                    return session_ptr;
                } catch (const Ort::Exception&) {
                    remove_cached_model(*optimized_model_path);
                }
            }
        }
        return Unexpected(Error{ErrorCode::ModelLoadFailed,
                                std::string("ONNX Runtime session creation failed: ") + e.what()});
    } catch (const std::exception& e) {
        return Unexpected(Error{ErrorCode::ModelLoadFailed,
                                std::string("Failed to initialize session: ") + e.what()});
    }
}

Result<FrameResult> InferenceSession::run_tiled(const Image& rgb, const Image& alpha_hint,
                                                const InferenceParams& params, int model_res,
                                                StageTimingCallback on_stage) {
    if (model_res <= 0) {
        return Unexpected(
            Error{ErrorCode::InvalidParameters, "Invalid model resolution for tiled inference."});
    }
    if (rgb.width <= 0 || rgb.height <= 0) {
        return Unexpected(
            Error{ErrorCode::InvalidParameters, "Invalid input size for tiled inference."});
    }

    struct PendingTile {
        int x_start = 0;
        int y_start = 0;
        Image rgb_view = {};
        Image hint_view = {};
    };

    try {
        int w = rgb.width;
        int h = rgb.height;
        int tile_size = model_res;
        int overlap = std::clamp(params.tile_padding, 0, tile_size - 1);
        int stride = core::tile_stride(tile_size, overlap);
        int tile_batch_size = m_device.backend == Backend::CPU ? std::max(1, params.batch_size) : 1;
        const bool include_foreground = !params.output_alpha_only;

        auto validate_buffer = [&](const ImageBuffer& buffer, int width, int height, int channels,
                                   const char* label) -> Result<void> {
            const size_t expected = static_cast<size_t>(width) * static_cast<size_t>(height) *
                                    static_cast<size_t>(channels);
            if (expected == 0) {
                return {};
            }
            if (buffer.const_view().data.size() != expected) {
                return Unexpected(
                    Error{ErrorCode::InferenceFailed,
                          std::string("Tiled inference failed to allocate ") + label + " buffer."});
            }
            return {};
        };

        // Allocate accumulators
        ImageBuffer acc_alpha(w, h, 1);
        ImageBuffer acc_fg = include_foreground ? ImageBuffer(w, h, 3) : ImageBuffer{};
        ImageBuffer acc_weight(w, h, 1);

        auto acc_alpha_ok = validate_buffer(acc_alpha, w, h, 1, "alpha accumulator");
        if (!acc_alpha_ok) return Unexpected(acc_alpha_ok.error());
        if (include_foreground) {
            auto acc_fg_ok = validate_buffer(acc_fg, w, h, 3, "foreground accumulator");
            if (!acc_fg_ok) return Unexpected(acc_fg_ok.error());
        }
        auto acc_weight_ok = validate_buffer(acc_weight, w, h, 1, "weight accumulator");
        if (!acc_weight_ok) return Unexpected(acc_weight_ok.error());

        std::fill(acc_alpha.view().data.begin(), acc_alpha.view().data.end(), 0.0f);
        if (include_foreground) {
            std::fill(acc_fg.view().data.begin(), acc_fg.view().data.end(), 0.0f);
        }
        std::fill(acc_weight.view().data.begin(), acc_weight.view().data.end(), 0.0f);

        if (m_tiled_weight_mask.view().width != tile_size || m_tiled_weight_padding != overlap) {
            m_tiled_weight_mask = ImageBuffer(tile_size, tile_size, 1);
            Image mask_view = m_tiled_weight_mask.view();
            common::measure_stage(
                on_stage, "tile_prepare_weights",
                [&]() {
                    common::parallel_for_rows(tile_size, [&](int y_begin, int y_end) {
                        for (int y = y_begin; y < y_end; ++y) {
                            float wy = 1.0f;
                            if (overlap > 0 && y < overlap) {
                                wy = static_cast<float>(y) / static_cast<float>(overlap);
                            } else if (overlap > 0 && y >= tile_size - overlap) {
                                wy = static_cast<float>(tile_size - 1 - y) /
                                     static_cast<float>(overlap);
                            }

                            for (int x = 0; x < tile_size; ++x) {
                                float wx = 1.0f;
                                if (overlap > 0 && x < overlap) {
                                    wx = static_cast<float>(x) / static_cast<float>(overlap);
                                } else if (overlap > 0 && x >= tile_size - overlap) {
                                    wx = static_cast<float>(tile_size - 1 - x) /
                                         static_cast<float>(overlap);
                                }
                                mask_view(y, x) = std::min(wx, wy);
                            }
                        }
                    });
                },
                1);
            m_tiled_weight_padding = overlap;
        }

        auto mask_ok =
            validate_buffer(m_tiled_weight_mask, tile_size, tile_size, 1, "tile weight mask");
        if (!mask_ok) return Unexpected(mask_ok.error());

        if (m_tiled_buffer_size != tile_size || m_tiled_pool_capacity != tile_batch_size) {
            m_tiled_rgb_pool.clear();
            m_tiled_hint_pool.clear();
            m_tiled_rgb_pool.reserve(static_cast<size_t>(tile_batch_size));
            m_tiled_hint_pool.reserve(static_cast<size_t>(tile_batch_size));
            for (int i = 0; i < tile_batch_size; ++i) {
                m_tiled_rgb_pool.emplace_back(tile_size, tile_size, 3);
                m_tiled_hint_pool.emplace_back(tile_size, tile_size, 1);
            }
            m_tiled_buffer_size = tile_size;
            m_tiled_pool_capacity = tile_batch_size;
        }

        for (const auto& tile : m_tiled_rgb_pool) {
            auto pool_ok = validate_buffer(tile, tile_size, tile_size, 3, "tile rgb pool");
            if (!pool_ok) return Unexpected(pool_ok.error());
        }
        for (const auto& tile : m_tiled_hint_pool) {
            auto pool_ok = validate_buffer(tile, tile_size, tile_size, 1, "tile hint pool");
            if (!pool_ok) return Unexpected(pool_ok.error());
        }

        Image mask = m_tiled_weight_mask.view();

        // Iterate tiles
        int nx = (w + stride - 1) / stride;
        int ny = (h + stride - 1) / stride;
        std::vector<PendingTile> pending_tiles;
        pending_tiles.reserve(static_cast<size_t>(tile_batch_size));

        auto flush_pending_tiles = [&]() -> Result<void> {
            if (pending_tiles.empty()) {
                return {};
            }

            std::vector<Image> rgb_tiles;
            std::vector<Image> hint_tiles;
            rgb_tiles.reserve(pending_tiles.size());
            hint_tiles.reserve(pending_tiles.size());
            for (const auto& tile : pending_tiles) {
                rgb_tiles.push_back(tile.rgb_view);
                hint_tiles.push_back(tile.hint_view);
            }

            auto batch_results = common::measure_stage(
                on_stage, "tile_infer",
                [&]() { return infer_batch_raw(rgb_tiles, hint_tiles, params, on_stage); },
                pending_tiles.size());
            if (!batch_results) {
                return Unexpected(batch_results.error());
            }

            for (size_t tile_index = 0; tile_index < pending_tiles.size(); ++tile_index) {
                auto& tile = pending_tiles[tile_index];
                common::measure_stage(
                    on_stage, "tile_accumulate",
                    [&]() {
                        common::parallel_for_rows(tile_size, [&](int y_begin, int y_end) {
                            accumulate_tile_rows(mask, (*batch_results)[tile_index],
                                                 acc_alpha.view(), acc_fg.view(), acc_weight.view(),
                                                 tile.y_start, tile.x_start, h, w, overlap, y_begin,
                                                 y_end);
                        });
                    },
                    1);
            }

            pending_tiles.clear();
            return {};
        };

        for (int ty = 0; ty < ny; ++ty) {
            for (int tx = 0; tx < nx; ++tx) {
                int x_start = tx * stride;
                int y_start = ty * stride;

                // Center crop if last tile goes over edge
                if (x_start + tile_size > w) x_start = std::max(0, w - tile_size);
                if (y_start + tile_size > h) y_start = std::max(0, h - tile_size);

                size_t tile_slot = pending_tiles.size();
                Image rgb_tile = m_tiled_rgb_pool[tile_slot].view();
                Image hint_tile = m_tiled_hint_pool[tile_slot].view();

                common::measure_stage(
                    on_stage, "tile_extract",
                    [&]() {
                        common::parallel_for_rows(tile_size, [&](int y_begin, int y_end) {
                            extract_tile_rows(rgb, alpha_hint, rgb_tile, hint_tile, y_start,
                                              x_start, y_begin, y_end);
                        });
                    },
                    1);

                pending_tiles.push_back(PendingTile{x_start, y_start, rgb_tile, hint_tile});
                if (pending_tiles.size() == static_cast<size_t>(tile_batch_size)) {
                    auto flush_res = flush_pending_tiles();
                    if (!flush_res) {
                        return Unexpected(flush_res.error());
                    }
                }
            }
        }

        auto flush_res = flush_pending_tiles();
        if (!flush_res) {
            return Unexpected(flush_res.error());
        }

        common::measure_stage(
            on_stage, "tile_normalize",
            [&]() {
                common::parallel_for_rows(h, [&](int y_begin, int y_end) {
                    normalize_accumulators(acc_alpha.view(), acc_fg.view(), acc_weight.view(),
                                           y_begin, y_end);
                });
            },
            1);

        FrameResult result;
        result.alpha = std::move(acc_alpha);
        if (include_foreground) {
            result.foreground = std::move(acc_fg);
        }
        return result;
    } catch (const std::bad_alloc&) {
        return Unexpected(
            Error{ErrorCode::InferenceFailed, "Tiled inference failed: out of memory."});
    } catch (const std::exception& e) {
        return Unexpected(
            Error{ErrorCode::InferenceFailed, std::string("Tiled inference failed: ") + e.what()});
    } catch (...) {
        return Unexpected(
            Error{ErrorCode::InferenceFailed, "Tiled inference failed due to an unknown error."});
    }
}

void InferenceSession::apply_post_process(FrameResult& result, const InferenceParams& params,
                                          Image source_rgb, StageTimingCallback on_stage) {
    if (result.alpha.view().empty() || result.foreground.view().empty()) return;

    int w = result.foreground.view().width;
    int h = result.foreground.view().height;

    // 1. Source passthrough: blend original source into opaque regions (before despill
    //    so that despill can clean green spill from both model and source pixels)
    if (params.source_passthrough && !source_rgb.empty()) {
        common::measure_stage(
            on_stage, "post_source_passthrough",
            [&]() {
                corridorkey::source_passthrough(source_rgb, result.foreground.view(),
                                                result.alpha.view(), params.sp_erode_px,
                                                params.sp_blur_px, m_color_utils_state);
            },
            1);
    }

    // 2. Despeckle alpha
    if (params.auto_despeckle) {
        common::measure_stage(
            on_stage, "post_despeckle",
            [&]() { despeckle(result.alpha.view(), params.despeckle_size, m_despeckle_state); }, 1);
    }

    // 3. Despill foreground (operates on combined fg after source passthrough)
    common::measure_stage(
        on_stage, "post_despill",
        [&]() {
            despill(result.foreground.view(), params.despill_strength,
                    static_cast<SpillMethod>(params.spill_method));
        },
        1);

    // 3. Generate processed: sRGB FG -> linear -> premultiply -> RGBA
    const auto& lut = SrgbLut::instance();
    Image fg = result.foreground.const_view();
    Image alpha_view = result.alpha.const_view();

    result.processed = ImageBuffer(w, h, 4);
    Image proc = result.processed.view();

    common::measure_stage(
        on_stage, "post_premultiply",
        [&]() {
            common::parallel_for_rows(h, [&](int y_begin, int y_end) {
                for (int y = y_begin; y < y_end; ++y) {
                    for (int x = 0; x < w; ++x) {
                        float a = alpha_view(y, x);
                        proc(y, x, 0) = lut.to_linear(fg(y, x, 0)) * a;
                        proc(y, x, 1) = lut.to_linear(fg(y, x, 1)) * a;
                        proc(y, x, 2) = lut.to_linear(fg(y, x, 2)) * a;
                        proc(y, x, 3) = a;
                    }
                }
            });
        },
        1);

    // 5. Composite on checker (linear space), then convert to sRGB
    result.composite = ImageBuffer(w, h, 4);
    Image comp = result.composite.view();
    common::measure_stage(
        on_stage, "post_composite",
        [&]() {
            std::copy(proc.data.begin(), proc.data.end(), comp.data.begin());
            ColorUtils::composite_over_checker(comp);
            ColorUtils::linear_to_srgb(comp);
        },
        1);
}

Result<FrameResult> InferenceSession::run_direct(const Image& rgb, const Image& alpha_hint,
                                                 const InferenceParams& params,
                                                 StageTimingCallback on_stage) {
    const int model_resolution = m_recommended_resolution > 0 ? m_recommended_resolution : 512;

    if (params.enable_tiling && (rgb.width > model_resolution || rgb.height > model_resolution)) {
        auto tiled_result = run_tiled(rgb, alpha_hint, params, model_resolution, on_stage);
        if (!tiled_result) {
            return Unexpected(tiled_result.error());
        }
        apply_post_process(*tiled_result, params, rgb, on_stage);
        return tiled_result;
    }

    auto result = infer_raw(rgb, alpha_hint, params, on_stage);
    if (!result) {
        return Unexpected(result.error());
    }
    apply_post_process(*result, params, rgb, on_stage);
    return result;
}

Result<FrameResult> InferenceSession::run_coarse_to_fine(const Image& rgb, const Image& alpha_hint,
                                                         const InferenceParams& params,
                                                         StageTimingCallback on_stage) {
    const int coarse_resolution = m_recommended_resolution > 0 ? m_recommended_resolution : 512;
    InferenceParams coarse_params = core::coarse_inference_params(params, coarse_resolution);

    debug_log("event=quality_path mode=coarse_to_fine requested_resolution=" +
              std::to_string(core::requested_quality_resolution(params, coarse_resolution)) +
              " coarse_resolution=" + std::to_string(coarse_resolution) +
              " strategy=artifact_fallback_only");

    auto raw_result = infer_raw(rgb, alpha_hint, coarse_params, on_stage);
    if (!raw_result) {
        return Unexpected(raw_result.error());
    }
    apply_post_process(*raw_result, params, rgb, on_stage);
    return raw_result;
}

Result<std::vector<FrameResult>> InferenceSession::run_batch(const std::vector<Image>& rgbs,
                                                             const std::vector<Image>& alpha_hints,
                                                             const InferenceParams& params,
                                                             StageTimingCallback on_stage) {
    if (rgbs.empty()) return std::vector<FrameResult>{};

    if (core::should_use_coarse_to_fine_path(params, m_recommended_resolution)) {
        const int coarse_resolution = m_recommended_resolution > 0 ? m_recommended_resolution : 512;
        InferenceParams coarse_params = core::coarse_inference_params(params, coarse_resolution);
        auto results_res = infer_batch_raw(rgbs, alpha_hints, coarse_params, on_stage);
        if (!results_res) {
            return Unexpected(results_res.error());
        }
        for (size_t index = 0; index < results_res->size(); ++index) {
            apply_post_process((*results_res)[index], params, rgbs[index], on_stage);
        }
        return results_res;
    }

    // Tiling logic for batch (simplified: if first image needs tiling, we don't batch but run tiled
    // one by one) Actually, tiling should be handled at a higher level if we want to batch tiles.
    // For now, let's keep it simple: tiling disables batching.
    const int model_resolution = m_recommended_resolution > 0 ? m_recommended_resolution : 512;
    if (params.enable_tiling &&
        (rgbs[0].width > model_resolution || rgbs[0].height > model_resolution)) {
        std::vector<FrameResult> results;
        for (size_t i = 0; i < rgbs.size(); ++i) {
            auto res = run_tiled(rgbs[i], alpha_hints[i], params, model_resolution, on_stage);
            if (!res) return Unexpected(res.error());
            apply_post_process(*res, params, rgbs[i], on_stage);
            results.push_back(std::move(*res));
        }
        return results;
    }

    auto results_res = infer_batch_raw(rgbs, alpha_hints, params, on_stage);
    if (!results_res) return results_res;

    for (size_t i = 0; i < results_res->size(); ++i) {
        apply_post_process((*results_res)[i], params, rgbs[i], on_stage);
    }
    return results_res;
}

Result<std::vector<FrameResult>> InferenceSession::infer_batch_raw(
    const std::vector<Image>& rgbs, const std::vector<Image>& alpha_hints,
    const InferenceParams& params, StageTimingCallback on_stage) {
    if (rgbs.empty()) return std::vector<FrameResult>{};
    if (m_mlx_session != nullptr) {
        if (params.target_resolution > 0 && params.target_resolution != m_recommended_resolution) {
            return Unexpected<Error>{Error{
                ErrorCode::InvalidParameters,
                "The current MLX bridge has a fixed resolution. Use --resolution 0 or prepare a "
                "matching bridge artifact."}};
        }

        int model_res = m_mlx_session->model_resolution();
        std::vector<FrameResult> results;
        results.reserve(rgbs.size());
        for (size_t index = 0; index < rgbs.size(); ++index) {
            bool is_tile = rgbs[index].width == model_res && rgbs[index].height == model_res;
            if (is_tile) {
                auto result = m_mlx_session->infer_tile(rgbs[index], alpha_hints[index],
                                                        params.output_alpha_only, on_stage);
                if (!result) return Unexpected(result.error());
                results.push_back(std::move(*result));
            } else {
                auto result =
                    m_mlx_session->infer(rgbs[index], alpha_hints[index], params.output_alpha_only,
                                         params.upscale_method, on_stage);
                if (!result) return Unexpected(result.error());
                results.push_back(std::move(*result));
            }
        }
        return results;
    }

    try {
        int target_res =
            params.target_resolution > 0 ? params.target_resolution : m_recommended_resolution;
        size_t batch_size = rgbs.size();

        bool is_concatenated = (m_input_node_dims.size() == 1 && m_input_node_dims[0][1] == 4);
        Ort::Value input_tensor{nullptr};
        std::vector<Ort::Value> output_tensors;
        BoundIoState* bound_io_state = nullptr;

        if (is_concatenated) {
            auto shape = m_input_node_dims[0];
            int64_t model_h = shape[2] < 0 ? target_res : shape[2];
            int64_t model_w = shape[3] < 0 ? target_res : shape[3];

            size_t total_planar_size = batch_size * 4 * model_h * model_w;
            m_planar_pool.resize(1);
            if (m_planar_pool[0].view().data.size() != total_planar_size) {
                m_planar_pool[0] = ImageBuffer(static_cast<int>(total_planar_size), 1, 1);
            }

            float* dst_base = m_planar_pool[0].view().data.data();
            size_t image_stride = 4 * model_h * model_w;
            size_t channel_stride = model_h * model_w;

            common::measure_stage(
                on_stage, "batch_prepare_inputs",
                [&]() {
                    for (size_t b = 0; b < batch_size; ++b) {
                        if (m_resize_pool.size() <= b * 2 + 1) {
                            m_resize_pool.resize(b * 2 + 2);
                        }
                        Image cur_rgb = m_resize_pool[b * 2].view();
                        if (cur_rgb.width != model_w || cur_rgb.height != model_h ||
                            cur_rgb.channels != 3) {
                            m_resize_pool[b * 2] = ImageBuffer(static_cast<int>(model_w),
                                                               static_cast<int>(model_h), 3);
                            cur_rgb = m_resize_pool[b * 2].view();
                        }
                        ColorUtils::resize_area_into(rgbs[b], cur_rgb, m_color_utils_state);

                        Image cur_hint = m_resize_pool[b * 2 + 1].view();
                        if (cur_hint.width != model_w || cur_hint.height != model_h ||
                            cur_hint.channels != 1) {
                            m_resize_pool[b * 2 + 1] = ImageBuffer(static_cast<int>(model_w),
                                                                   static_cast<int>(model_h), 1);
                            cur_hint = m_resize_pool[b * 2 + 1].view();
                        }
                        ColorUtils::resize_area_into(alpha_hints[b], cur_hint, m_color_utils_state);
                        float* dst = dst_base + (b * image_stride);

                        for (int y = 0; y < model_h; ++y) {
                            for (int x = 0; x < model_w; ++x) {
                                size_t idx = y * model_w + x;
                                dst[0 * channel_stride + idx] =
                                    (cur_rgb(y, x, 0) - 0.485f) / 0.229f;
                                dst[1 * channel_stride + idx] =
                                    (cur_rgb(y, x, 1) - 0.456f) / 0.224f;
                                dst[2 * channel_stride + idx] =
                                    (cur_rgb(y, x, 2) - 0.406f) / 0.225f;
                                dst[3 * channel_stride + idx] = cur_hint(y, x, 0);
                            }
                        }
                    }
                },
                batch_size);

            std::vector<int64_t> effective_shape = {(int64_t)batch_size, 4, model_h, model_w};
            auto input_tensor_res =
                create_input_tensor(dst_base, total_planar_size, effective_shape);
            if (!input_tensor_res) {
                return Unexpected(input_tensor_res.error());
            }
            input_tensor = std::move(*input_tensor_res);

            if (m_io_binding_enabled) {
                auto bound_state_res = ensure_bound_io_state(effective_shape);
                if (!bound_state_res) {
                    debug_log("I/O binding disabled after setup failure: " +
                              bound_state_res.error().message);
                    m_io_binding_enabled = false;
                    m_bound_io_state.reset();
                } else {
                    bound_io_state = *bound_state_res;
                }
            }
        } else {
            return Unexpected(Error{ErrorCode::HardwareNotSupported,
                                    "Non-concatenated models not yet supported with batching"});
        }

        if (bound_io_state != nullptr) {
            common::measure_stage(
                on_stage, "ort_run",
                [&]() {
                    bound_io_state->binding.ClearBoundInputs();
                    bound_io_state->binding.BindInput(m_input_node_names_ptr[0], input_tensor);
                    bound_io_state->binding.SynchronizeInputs();
                    m_session.Run(Ort::RunOptions{nullptr}, bound_io_state->binding);
                    bound_io_state->binding.SynchronizeOutputs();
                },
                batch_size);
        } else {
            output_tensors = common::measure_stage(
                on_stage, "ort_run",
                [&]() {
                    return m_session.Run(Ort::RunOptions{nullptr}, m_input_node_names_ptr.data(),
                                         &input_tensor, 1, m_output_node_names_ptr.data(),
                                         m_output_node_names_ptr.size());
                },
                batch_size);
        }

        if (bound_io_state == nullptr && output_tensors.empty()) {
            debug_log("Model produced no output tensors");
            return Unexpected(
                Error{ErrorCode::InferenceFailed, "Model produced no output tensors"});
        }

        std::vector<FrameResult> batch_results(batch_size);

        MaterializedOutputTensor alpha_output;
        std::optional<MaterializedOutputTensor> fg_output;
        const bool include_foreground = !params.output_alpha_only;

        debug_log("Extracting batch outputs...");
        auto batch_extract_res = common::measure_stage(
            on_stage, "batch_extract_outputs",
            [&]() -> Result<void> {
                auto materialize_res = common::measure_stage(
                    on_stage, "batch_extract_outputs_tensor_materialize",
                    [&]() -> Result<void> {
                        Ort::Value& alpha_tensor = bound_io_state != nullptr
                                                      ? bound_io_state->alpha_output.tensor
                                                      : output_tensors[0];
                        auto alpha_res = materialize_output_tensor(
                            alpha_tensor, batch_size, m_device, m_recommended_resolution,
                            "alpha_raw_output", "Alpha");
                        if (!alpha_res) {
                            return Unexpected(alpha_res.error());
                        }
                        alpha_output = std::move(*alpha_res);

                        Ort::Value* fg_tensor = nullptr;
                        if (bound_io_state != nullptr && bound_io_state->fg_output.has_value()) {
                            fg_tensor = &bound_io_state->fg_output->tensor;
                        } else if (output_tensors.size() > 1) {
                            fg_tensor = &output_tensors[1];
                        }

                        if (include_foreground && fg_tensor != nullptr) {
                            auto fg_res = materialize_output_tensor(
                                *fg_tensor, batch_size, m_device, m_recommended_resolution,
                                "fg_raw_output", "FG");
                            if (!fg_res) {
                                return Unexpected(fg_res.error());
                            }
                            fg_output = std::move(*fg_res);
                        }

                        return {};
                    },
                    batch_size);
                if (!materialize_res) {
                    return Unexpected(materialize_res.error());
                }

                const bool use_lanczos = params.upscale_method == UpscaleMethod::Lanczos4;
                auto resize_res = common::measure_stage(
                    on_stage, "batch_extract_outputs_resize",
                    [&]() -> Result<void> {
                        const auto alpha_width = static_cast<int>(alpha_output.shape[3]);
                        const auto alpha_height = static_cast<int>(alpha_output.shape[2]);
                        const auto alpha_channels = static_cast<int>(alpha_output.shape[1]);
                        const bool include_batch_foreground = fg_output.has_value();
                        const auto fg_width =
                            include_batch_foreground ? static_cast<int>(fg_output->shape[3]) : 0;
                        const auto fg_height =
                            include_batch_foreground ? static_cast<int>(fg_output->shape[2]) : 0;
                        const auto fg_channels =
                            include_batch_foreground ? static_cast<int>(fg_output->shape[1]) : 0;

                        for (std::size_t batch_index = 0; batch_index < batch_size; ++batch_index) {
                            FrameResult& result = batch_results[batch_index];
                            result.alpha =
                                ImageBuffer(rgbs[batch_index].width, rgbs[batch_index].height, 1);
                            resize_model_output(
                                alpha_output.values + (batch_index * alpha_output.image_stride),
                                alpha_width, alpha_height, alpha_channels, result.alpha.view(),
                                use_lanczos, m_color_utils_state);

                            if (include_batch_foreground) {
                                result.foreground = ImageBuffer(rgbs[batch_index].width,
                                                                rgbs[batch_index].height, 3);
                                resize_model_output(
                                    fg_output->values + (batch_index * fg_output->image_stride),
                                    fg_width, fg_height, fg_channels,
                                    result.foreground.view(), use_lanczos,
                                    m_color_utils_state);
                            }
                        }

                        return {};
                    },
                    batch_size);
                if (!resize_res) {
                    return Unexpected(resize_res.error());
                }

                auto finalize_res = common::measure_stage(
                    on_stage, "batch_extract_outputs_finalize",
                    [&]() -> Result<void> {
                        for (std::size_t batch_index = 0; batch_index < batch_results.size();
                             ++batch_index) {
                            ColorUtils::clamp_image(batch_results[batch_index].alpha.view(), 0.0F,
                                                    1.0F);

                            auto alpha_final_res =
                                finalize_output_image(m_device, m_recommended_resolution,
                                                      batch_results[batch_index].alpha.view(),
                                                      "alpha_resized_output");
                            if (!alpha_final_res) {
                                return Unexpected(alpha_final_res.error());
                            }

                            if (!batch_results[batch_index].foreground.view().empty()) {
                                auto fg_final_res = finalize_output_image(
                                    m_device, m_recommended_resolution,
                                    batch_results[batch_index].foreground.view(),
                                    "fg_resized_output");
                                if (!fg_final_res) {
                                    return Unexpected(fg_final_res.error());
                                }
                            }
                        }

                        return {};
                    },
                    batch_size);
                if (!finalize_res) {
                    return Unexpected(finalize_res.error());
                }

                return {};
            },
            batch_size);
        if (!batch_extract_res) {
            return Unexpected(batch_extract_res.error());
        }

        return batch_results;

    } catch (const Ort::Exception& e) {
        return Unexpected(Error{ErrorCode::InferenceFailed,
                                std::string("ONNX Runtime execution failed: ") + e.what()});
    }
}

Result<FrameResult> InferenceSession::infer_raw(const Image& rgb, const Image& alpha_hint,
                                                const InferenceParams& params,
                                                StageTimingCallback on_stage) {
    if (m_mlx_session != nullptr) {
        auto batch_res = infer_batch_raw({rgb}, {alpha_hint}, params, on_stage);
        if (!batch_res) {
            return Unexpected(batch_res.error());
        }
        return std::move((*batch_res)[0]);
    }

    try {
        int target_res =
            params.target_resolution > 0 ? params.target_resolution : m_recommended_resolution;

        bool is_concatenated = (m_input_node_dims.size() == 1 && m_input_node_dims[0][1] == 4);
        if (!is_concatenated) {
            return Unexpected(Error{ErrorCode::HardwareNotSupported,
                                    "Non-concatenated models are not yet supported."});
        }

        auto shape = m_input_node_dims[0];
        int64_t model_h = shape[2] < 0 ? target_res : shape[2];
        int64_t model_w = shape[3] < 0 ? target_res : shape[3];
        size_t channel_stride = static_cast<size_t>(model_h) * static_cast<size_t>(model_w);
        size_t total_planar_size = 4 * channel_stride;

        auto ensure_resize_buffer = [&](std::size_t index, int width, int height,
                                        int channels) -> Image {
            if (m_resize_pool.size() <= index) {
                m_resize_pool.resize(index + 1);
            }
            Image current = m_resize_pool[index].view();
            if (current.width != width || current.height != height ||
                current.channels != channels) {
                m_resize_pool[index] = ImageBuffer(width, height, channels);
                current = m_resize_pool[index].view();
            }
            return current;
        };

        if (m_planar_pool.empty() || m_planar_pool[0].view().data.size() != total_planar_size) {
            if (m_planar_pool.empty()) {
                m_planar_pool.emplace_back(static_cast<int>(total_planar_size), 1, 1);
            } else {
                m_planar_pool[0] = ImageBuffer(static_cast<int>(total_planar_size), 1, 1);
            }
        }

        Image prepared_rgb =
            ensure_resize_buffer(0, static_cast<int>(model_w), static_cast<int>(model_h), 3);
        Image prepared_hint =
            ensure_resize_buffer(1, static_cast<int>(model_w), static_cast<int>(model_h), 1);
        float* planar_ptr = m_planar_pool[0].view().data.data();

        common::measure_stage(
            on_stage, "frame_prepare_inputs",
            [&]() {
                ColorUtils::resize_area_into(rgb, prepared_rgb, m_color_utils_state);
                ColorUtils::resize_area_into(alpha_hint, prepared_hint, m_color_utils_state);

                for (int y = 0; y < model_h; ++y) {
                    for (int x = 0; x < model_w; ++x) {
                        size_t idx = static_cast<size_t>(y) * static_cast<size_t>(model_w) + x;
                        planar_ptr[0 * channel_stride + idx] =
                            (prepared_rgb(y, x, 0) - 0.485f) / 0.229f;
                        planar_ptr[1 * channel_stride + idx] =
                            (prepared_rgb(y, x, 1) - 0.456f) / 0.224f;
                        planar_ptr[2 * channel_stride + idx] =
                            (prepared_rgb(y, x, 2) - 0.406f) / 0.225f;
                        planar_ptr[3 * channel_stride + idx] = prepared_hint(y, x, 0);
                    }
                }
            },
            1);

        std::vector<int64_t> effective_shape = {1, 4, model_h, model_w};
        auto input_tensor_res = create_input_tensor(planar_ptr, total_planar_size, effective_shape);
        if (!input_tensor_res) {
            return Unexpected(input_tensor_res.error());
        }
        Ort::Value input_tensor = std::move(*input_tensor_res);
        std::vector<Ort::Value> output_tensors;
        BoundIoState* bound_io_state = nullptr;

        if (m_io_binding_enabled) {
            auto bound_state_res = ensure_bound_io_state(effective_shape);
            if (!bound_state_res) {
                debug_log("I/O binding disabled after setup failure: " +
                          bound_state_res.error().message);
                m_io_binding_enabled = false;
                m_bound_io_state.reset();
            } else {
                bound_io_state = *bound_state_res;
            }
        }

        if (bound_io_state != nullptr) {
            common::measure_stage(
                on_stage, "ort_run",
                [&]() {
                    bound_io_state->binding.ClearBoundInputs();
                    bound_io_state->binding.BindInput(m_input_node_names_ptr[0], input_tensor);
                    bound_io_state->binding.SynchronizeInputs();
                    m_session.Run(Ort::RunOptions{nullptr}, bound_io_state->binding);
                    bound_io_state->binding.SynchronizeOutputs();
                },
                1);
        } else {
            output_tensors = common::measure_stage(
                on_stage, "ort_run",
                [&]() {
                    return m_session.Run(Ort::RunOptions{nullptr}, m_input_node_names_ptr.data(),
                                         &input_tensor, 1, m_output_node_names_ptr.data(),
                                         m_output_node_names_ptr.size());
                },
                1);
        }

        if (bound_io_state == nullptr && output_tensors.empty()) {
            return Unexpected(
                Error{ErrorCode::InferenceFailed, "Model produced no output tensors"});
        }

        MaterializedOutputTensor alpha_output;
        std::optional<MaterializedOutputTensor> fg_output;
        const bool include_foreground = !params.output_alpha_only;

        FrameResult result;
        result.alpha = ImageBuffer(rgb.width, rgb.height, 1);
        if (include_foreground && output_tensors.size() > 1) {
            result.foreground = ImageBuffer(rgb.width, rgb.height, 3);
        }

        bool use_lanczos = params.upscale_method == UpscaleMethod::Lanczos4;
        auto extract_res = common::measure_stage(
            on_stage, "frame_extract_outputs",
            [&]() -> Result<void> {
                auto materialize_res = common::measure_stage(
                    on_stage, "frame_extract_outputs_tensor_materialize",
                    [&]() -> Result<void> {
                        Ort::Value& alpha_tensor = bound_io_state != nullptr
                                                      ? bound_io_state->alpha_output.tensor
                                                      : output_tensors[0];
                        auto alpha_res = materialize_output_tensor(
                            alpha_tensor, 1, m_device, m_recommended_resolution, "alpha_raw_output",
                            "Alpha");
                        if (!alpha_res) {
                            return Unexpected(alpha_res.error());
                        }
                        alpha_output = std::move(*alpha_res);

                        Ort::Value* fg_tensor = nullptr;
                        if (bound_io_state != nullptr && bound_io_state->fg_output.has_value()) {
                            fg_tensor = &bound_io_state->fg_output->tensor;
                        } else if (output_tensors.size() > 1) {
                            fg_tensor = &output_tensors[1];
                        }

                        if (include_foreground && fg_tensor != nullptr) {
                            auto fg_res = materialize_output_tensor(
                                *fg_tensor, 1, m_device, m_recommended_resolution,
                                "fg_raw_output", "FG");
                            if (!fg_res) {
                                return Unexpected(fg_res.error());
                            }
                            fg_output = std::move(*fg_res);
                        }

                        return {};
                    },
                    1);
                if (!materialize_res) {
                    return Unexpected(materialize_res.error());
                }

                auto resize_res = common::measure_stage(
                    on_stage, "frame_extract_outputs_resize",
                    [&]() -> Result<void> {
                        resize_model_output(alpha_output.values,
                                            static_cast<int>(alpha_output.shape[3]),
                                            static_cast<int>(alpha_output.shape[2]),
                                            static_cast<int>(alpha_output.shape[1]),
                                            result.alpha.view(), use_lanczos,
                                            m_color_utils_state);

                        if (fg_output.has_value()) {
                            resize_model_output(
                                fg_output->values, static_cast<int>(fg_output->shape[3]),
                                static_cast<int>(fg_output->shape[2]),
                                static_cast<int>(fg_output->shape[1]), result.foreground.view(),
                                use_lanczos, m_color_utils_state);
                        }

                        return {};
                    },
                    1);
                if (!resize_res) {
                    return Unexpected(resize_res.error());
                }

                auto finalize_res = common::measure_stage(
                    on_stage, "frame_extract_outputs_finalize",
                    [&]() -> Result<void> {
                        ColorUtils::clamp_image(result.alpha.view(), 0.0F, 1.0F);
                        auto alpha_final_res =
                            finalize_output_image(m_device, m_recommended_resolution,
                                                  result.alpha.view(), "alpha_resized_output");
                        if (!alpha_final_res) {
                            return Unexpected(alpha_final_res.error());
                        }

                        if (!result.foreground.view().empty()) {
                            auto fg_final_res =
                                finalize_output_image(m_device, m_recommended_resolution,
                                                      result.foreground.view(),
                                                      "fg_resized_output");
                            if (!fg_final_res) {
                                return Unexpected(fg_final_res.error());
                            }
                        }

                        return {};
                    },
                    1);
                if (!finalize_res) {
                    return Unexpected(finalize_res.error());
                }

                return {};
            },
            1);
        if (!extract_res) {
            return Unexpected(extract_res.error());
        }

        return result;
    } catch (const Ort::Exception& e) {
        return Unexpected(Error{ErrorCode::InferenceFailed,
                                std::string("ONNX Runtime execution failed: ") + e.what()});
    }
}

Result<FrameResult> InferenceSession::run(const Image& rgb, const Image& alpha_hint,
                                          const InferenceParams& params,
                                          StageTimingCallback on_stage) {
    if (core::should_use_coarse_to_fine_path(params, m_recommended_resolution)) {
        return run_coarse_to_fine(rgb, alpha_hint, params, on_stage);
    }

    return run_direct(rgb, alpha_hint, params, on_stage);
}

}  // namespace corridorkey
