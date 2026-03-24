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

#include "common/parallel_for.hpp"
#include "common/runtime_paths.hpp"
#include "common/srgb_lut.hpp"
#include "common/stage_profiler.hpp"
#include "mlx_session.hpp"
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

constexpr const char* kDisableCpuEpFallbackConfig = "session.disable_cpu_ep_fallback";

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
void append_tensorrt_rtx_execution_provider(Ort::SessionOptions& session_options,
                                            const std::filesystem::path& model_path) {
    debug_log("Configuring TensorRT RTX execution provider");
    // Use 8 GB workspace for large models (1536 / 2048) to allow TensorRT to fuse kernels that
    // fail at the 2 GB limit. Smaller models keep 2 GB to reduce compile-time overhead.
    constexpr const char* kWorkspaceDefault = "2147483647";  // 2 GB
    constexpr const char* kWorkspaceLarge = "8589934592";    // 8 GB
    auto filename = model_path.filename().string();
    const bool is_large_model =
        filename.find("_1536") != std::string::npos || filename.find("_2048") != std::string::npos;
    const std::string workspace_size = is_large_model ? kWorkspaceLarge : kWorkspaceDefault;
    std::unordered_map<std::string, std::string> provider_options = {
        {tensorrt_rtx_option_names::kDeviceId, "0"},
        {tensorrt_rtx_option_names::kMaxWorkspaceSize, workspace_size},
    };

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
            for (int channel = 0; channel < 3; ++channel) {
                acc_fg(global_y, global_x, channel) += tile_foreground(y, x, channel) * weight;
            }
        }
    }
}

void normalize_accumulators(Image acc_alpha, Image acc_fg, const Image& acc_weight, int y_begin,
                            int y_end) {
    for (int y = y_begin; y < y_end; ++y) {
        size_t row_offset = static_cast<size_t>(y) * static_cast<size_t>(acc_alpha.width);
        for (int x = 0; x < acc_alpha.width; ++x) {
            size_t pixel_index = row_offset + static_cast<size_t>(x);
            float weight = acc_weight.data[pixel_index];
            if (weight <= 0.0001f) {
                continue;
            }

            acc_alpha.data[pixel_index] /= weight;
            size_t fg_index = pixel_index * 3;
            acc_fg.data[fg_index] /= weight;
            acc_fg.data[fg_index + 1] /= weight;
            acc_fg.data[fg_index + 2] /= weight;
        }
    }
}

}  // namespace

InferenceSession::InferenceSession(DeviceInfo device) : m_device(std::move(device)) {
    // Default recommended resolution. High-level layers (App)
    // will typically override this via InferenceParams.
    m_recommended_resolution = 512;
}

InferenceSession::~InferenceSession() = default;

void InferenceSession::configure_session_options(bool use_optimized_model_cache,
                                                 const SessionCreateOptions& options,
                                                 const std::filesystem::path& model_path) {
#ifndef _WIN32
    (void)model_path;
#endif
    debug_log("Setting intra-op threads");
    m_session_options.SetIntraOpNumThreads(core::intra_op_threads_for_backend(m_device.backend));

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

void InferenceSession::extract_metadata() {
    debug_log("Extracting model metadata [BUILD 2026-03-18-V1]");
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
            debug_log("Input 0 element type: " + std::to_string(m_input_element_type) +
                      " (FLOAT16 is 10, FLOAT is 1)");
        }
    }
    for (const auto& name : m_input_node_names) {
        m_input_node_names_ptr.push_back(name.c_str());
    }

    fprintf(stderr, "[InferenceSession] Getting output count\n");
    size_t num_output_nodes = m_session.GetOutputCount();
    fprintf(stderr, "[InferenceSession] Model has %zu outputs\n", num_output_nodes);

    for (size_t i = 0; i < num_output_nodes; i++) {
        fprintf(stderr, "[InferenceSession] Processing output %zu\n", i);
        auto output_name_ptr = m_session.GetOutputNameAllocated(i, allocator);
        m_output_node_names.push_back(output_name_ptr.get());
    }
    for (const auto& name : m_output_node_names) {
        m_output_node_names_ptr.push_back(name.c_str());
    }
    fprintf(stderr, "[InferenceSession] Metadata extraction complete\n");
}

Result<std::unique_ptr<InferenceSession>> InferenceSession::create(
    const std::filesystem::path& model_path, DeviceInfo device, SessionCreateOptions options) {
    fprintf(stderr, "[InferenceSession] Creating session for model: %s\n",
            model_path.string().c_str());

    if (!std::filesystem::exists(model_path)) {
        return Unexpected(
            Error{ErrorCode::ModelLoadFailed, "Model file not found: " + model_path.string()});
    }

    DeviceInfo requested_device = device;

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

        fprintf(stderr, "[InferenceSession] Creating ORT environment\n");
        session_ptr->m_env = Ort::Env(options.log_severity, "CorridorKey");
        fprintf(stderr, "[InferenceSession] ORT environment created\n");
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

        fprintf(stderr, "[InferenceSession] Configuring session options\n");
        session_ptr->configure_session_options(using_optimized_model_cache, options,
                                               session_model_path);
        fprintf(stderr, "[InferenceSession] Session options configured\n");

        if (!using_optimized_model_cache && optimized_model_path.has_value()) {
            fprintf(stderr, "[InferenceSession] Setting optimized model path\n");
#ifdef _WIN32
            session_ptr->m_session_options.SetOptimizedModelFilePath(
                optimized_model_path->wstring().c_str());
#else
            session_ptr->m_session_options.SetOptimizedModelFilePath(optimized_model_path->c_str());
#endif
        }

        fprintf(stderr, "[InferenceSession] Creating ONNX Runtime session from model: %s\n",
                session_model_path.string().c_str());
#ifdef _WIN32
        session_ptr->m_session =
            Ort::Session(session_ptr->m_env, session_model_path.wstring().c_str(),
                         session_ptr->m_session_options);
#else
        session_ptr->m_session = Ort::Session(session_ptr->m_env, session_model_path.c_str(),
                                              session_ptr->m_session_options);
#endif
        fprintf(stderr, "[InferenceSession] ONNX Runtime session created successfully\n");

        session_ptr->extract_metadata();
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
                    session_ptr->m_env = Ort::Env(options.log_severity, "CorridorKey");
                    session_ptr->configure_session_options(false, options, model_path);
#ifdef _WIN32
                    session_ptr->m_session_options.SetOptimizedModelFilePath(
                        optimized_model_path->wstring().c_str());
#else
                    session_ptr->m_session_options.SetOptimizedModelFilePath(
                        optimized_model_path->c_str());
#endif
#ifdef _WIN32
                    session_ptr->m_session =
                        Ort::Session(session_ptr->m_env, model_path.wstring().c_str(),
                                     session_ptr->m_session_options);
#else
                    session_ptr->m_session = Ort::Session(session_ptr->m_env, model_path.c_str(),
                                                          session_ptr->m_session_options);
#endif
                    session_ptr->extract_metadata();
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
        ImageBuffer acc_fg(w, h, 3);
        ImageBuffer acc_weight(w, h, 1);

        auto acc_alpha_ok = validate_buffer(acc_alpha, w, h, 1, "alpha accumulator");
        if (!acc_alpha_ok) return Unexpected(acc_alpha_ok.error());
        auto acc_fg_ok = validate_buffer(acc_fg, w, h, 3, "foreground accumulator");
        if (!acc_fg_ok) return Unexpected(acc_fg_ok.error());
        auto acc_weight_ok = validate_buffer(acc_weight, w, h, 1, "weight accumulator");
        if (!acc_weight_ok) return Unexpected(acc_weight_ok.error());

        std::fill(acc_alpha.view().data.begin(), acc_alpha.view().data.end(), 0.0f);
        std::fill(acc_fg.view().data.begin(), acc_fg.view().data.end(), 0.0f);
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
        result.foreground = std::move(acc_fg);

        common::measure_stage(
            on_stage, "tile_post_process",
            [&]() { apply_post_process(result, params, rgb, on_stage); }, 1);

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

Result<std::vector<FrameResult>> InferenceSession::run_batch(const std::vector<Image>& rgbs,
                                                             const std::vector<Image>& alpha_hints,
                                                             const InferenceParams& params,
                                                             StageTimingCallback on_stage) {
    if (rgbs.empty()) return std::vector<FrameResult>{};

    // Tiling logic for batch (simplified: if first image needs tiling, we don't batch but run tiled
    // one by one) Actually, tiling should be handled at a higher level if we want to batch tiles.
    // For now, let's keep it simple: tiling disables batching.
    int target_res =
        params.target_resolution > 0 ? params.target_resolution : m_recommended_resolution;
    if (params.enable_tiling && (rgbs[0].width > target_res || rgbs[0].height > target_res)) {
        std::vector<FrameResult> results;
        for (size_t i = 0; i < rgbs.size(); ++i) {
            auto res = run_tiled(rgbs[i], alpha_hints[i], params, target_res, on_stage);
            if (!res) return Unexpected(res.error());
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
                auto result = m_mlx_session->infer_tile(rgbs[index], alpha_hints[index], on_stage);
                if (!result) return Unexpected(result.error());
                results.push_back(std::move(*result));
            } else {
                auto result = m_mlx_session->infer(rgbs[index], alpha_hints[index],
                                                   params.upscale_method, on_stage);
                if (!result) return Unexpected(result.error());
                results.push_back(std::move(*result));
            }
        }
        return results;
    }

    try {
        Ort::MemoryInfo memory_info =
            Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        int target_res =
            params.target_resolution > 0 ? params.target_resolution : m_recommended_resolution;
        size_t batch_size = rgbs.size();

        bool is_concatenated = (m_input_node_dims.size() == 1 && m_input_node_dims[0][1] == 4);
        std::vector<Ort::Value> input_tensors;

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

            // Check if model expects FP16 input (required for TensorRT on Windows)
            if (m_input_element_type == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT16) {
                debug_log("Creating FP16 input tensor [BATCH SIZE=" + std::to_string(batch_size) +
                          "]");
                m_fp16_pool.resize(total_planar_size);
                for (size_t i = 0; i < total_planar_size; ++i) {
                    m_fp16_pool[i] = Ort::Float16_t(dst_base[i]);
                }
                input_tensors.push_back(Ort::Value::CreateTensor<Ort::Float16_t>(
                    memory_info, m_fp16_pool.data(), total_planar_size, effective_shape.data(),
                    effective_shape.size()));
            } else {
                debug_log(
                    "Creating FP32 input tensor (type: " + std::to_string(m_input_element_type) +
                    ") [BATCH SIZE=" + std::to_string(batch_size) + "]");
                input_tensors.push_back(Ort::Value::CreateTensor<float>(
                    memory_info, dst_base, total_planar_size, effective_shape.data(),
                    effective_shape.size()));
            }
        } else {
            return Unexpected(Error{ErrorCode::HardwareNotSupported,
                                    "Non-concatenated models not yet supported with batching"});
        }

        auto output_tensors = common::measure_stage(
            on_stage, "ort_run",
            [&]() {
                return m_session.Run(Ort::RunOptions{nullptr}, m_input_node_names_ptr.data(),
                                     input_tensors.data(), input_tensors.size(),
                                     m_output_node_names_ptr.data(),
                                     m_output_node_names_ptr.size());
            },
            batch_size);

        if (output_tensors.empty()) {
            debug_log("Model produced no output tensors");
            return Unexpected(
                Error{ErrorCode::InferenceFailed, "Model produced no output tensors"});
        }

        std::vector<FrameResult> batch_results(batch_size);

        auto alpha_info = output_tensors[0].GetTensorTypeAndShapeInfo();
        auto alpha_type = alpha_info.GetElementType();
        auto alpha_shape = alpha_info.GetShape();
        debug_log("Alpha output element type: " + std::to_string(alpha_type) +
                  " (FLOAT16 is 10, FLOAT is 1)");

        size_t alpha_image_stride = alpha_shape[1] * alpha_shape[2] * alpha_shape[3];

        float* alpha_ptr = nullptr;
        std::vector<float> alpha_fp32_conv;

        if (alpha_type == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT16) {
            debug_log("Converting alpha output from FP16 to FP32");
            const Ort::Float16_t* alpha_fp16 = output_tensors[0].GetTensorData<Ort::Float16_t>();
            size_t total_alpha_elements = batch_size * alpha_image_stride;
            alpha_fp32_conv.resize(total_alpha_elements);
            for (size_t i = 0; i < total_alpha_elements; ++i) {
                alpha_fp32_conv[i] = alpha_fp16[i].ToFloat();
            }
            alpha_ptr = alpha_fp32_conv.data();
        } else {
            alpha_ptr = output_tensors[0].GetTensorMutableData<float>();
        }

        float* fg_ptr = nullptr;
        std::vector<float> fg_fp32_conv;
        std::vector<int64_t> fg_shape;
        size_t fg_image_stride = 0;

        if (output_tensors.size() > 1) {
            auto fg_info = output_tensors[1].GetTensorTypeAndShapeInfo();
            auto fg_type = fg_info.GetElementType();
            fg_shape = fg_info.GetShape();
            fg_image_stride = fg_shape[1] * fg_shape[2] * fg_shape[3];
            debug_log("FG output element type: " + std::to_string(fg_type));

            if (fg_type == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT16) {
                debug_log("Converting FG output from FP16 to FP32");
                const Ort::Float16_t* fg_fp16 = output_tensors[1].GetTensorData<Ort::Float16_t>();
                size_t total_fg_elements = batch_size * fg_image_stride;
                fg_fp32_conv.resize(total_fg_elements);
                for (size_t i = 0; i < total_fg_elements; ++i) {
                    fg_fp32_conv[i] = fg_fp16[i].ToFloat();
                }
                fg_ptr = fg_fp32_conv.data();
            } else {
                fg_ptr = output_tensors[1].GetTensorMutableData<float>();
            }
        }

        debug_log("Extracting batch outputs...");
        common::measure_stage(
            on_stage, "batch_extract_outputs",
            [&]() {
                for (size_t b = 0; b < batch_size; ++b) {
                    FrameResult& result = batch_results[b];
                    bool use_lanczos = params.upscale_method == UpscaleMethod::Lanczos4;

                    ImageBuffer model_alpha((int)alpha_shape[3], (int)alpha_shape[2],
                                            (int)alpha_shape[1]);
                    ColorUtils::from_planar(alpha_ptr + (b * alpha_image_stride),
                                            model_alpha.view());

                    result.alpha =
                        use_lanczos
                            ? ColorUtils::resize_lanczos(model_alpha.view(), rgbs[b].width,
                                                         rgbs[b].height, m_color_utils_state)
                            : ColorUtils::resize(model_alpha.view(), rgbs[b].width, rgbs[b].height);
                    ColorUtils::clamp_image(result.alpha.view(), 0.0F, 1.0F);

                    if (fg_ptr != nullptr) {
                        ImageBuffer model_fg((int)fg_shape[3], (int)fg_shape[2], (int)fg_shape[1]);
                        ColorUtils::from_planar(fg_ptr + (b * fg_image_stride), model_fg.view());

                        result.foreground =
                            use_lanczos
                                ? ColorUtils::resize_lanczos(model_fg.view(), rgbs[b].width,
                                                             rgbs[b].height, m_color_utils_state)
                                : ColorUtils::resize(model_fg.view(), rgbs[b].width,
                                                     rgbs[b].height);
                    }
                }
            },
            batch_size);

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
        Ort::MemoryInfo memory_info =
            Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
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

        std::vector<Ort::Value> input_tensors;
        std::vector<int64_t> effective_shape = {1, 4, model_h, model_w};
        if (m_input_element_type == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT16) {
            m_fp16_pool.resize(total_planar_size);
            for (size_t index = 0; index < total_planar_size; ++index) {
                m_fp16_pool[index] = Ort::Float16_t(planar_ptr[index]);
            }
            input_tensors.push_back(Ort::Value::CreateTensor<Ort::Float16_t>(
                memory_info, m_fp16_pool.data(), total_planar_size, effective_shape.data(),
                effective_shape.size()));
        } else {
            input_tensors.push_back(
                Ort::Value::CreateTensor<float>(memory_info, planar_ptr, total_planar_size,
                                                effective_shape.data(), effective_shape.size()));
        }

        auto output_tensors = common::measure_stage(
            on_stage, "ort_run",
            [&]() {
                return m_session.Run(Ort::RunOptions{nullptr}, m_input_node_names_ptr.data(),
                                     input_tensors.data(), input_tensors.size(),
                                     m_output_node_names_ptr.data(),
                                     m_output_node_names_ptr.size());
            },
            1);

        if (output_tensors.empty()) {
            return Unexpected(
                Error{ErrorCode::InferenceFailed, "Model produced no output tensors"});
        }

        auto alpha_info = output_tensors[0].GetTensorTypeAndShapeInfo();
        auto alpha_type = alpha_info.GetElementType();
        auto alpha_shape = alpha_info.GetShape();
        size_t alpha_image_stride = static_cast<size_t>(alpha_shape[1]) *
                                    static_cast<size_t>(alpha_shape[2]) *
                                    static_cast<size_t>(alpha_shape[3]);

        float* alpha_ptr = nullptr;
        std::vector<float> alpha_fp32_conv;
        if (alpha_type == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT16) {
            const Ort::Float16_t* alpha_fp16 = output_tensors[0].GetTensorData<Ort::Float16_t>();
            alpha_fp32_conv.resize(alpha_image_stride);
            for (size_t index = 0; index < alpha_image_stride; ++index) {
                alpha_fp32_conv[index] = alpha_fp16[index].ToFloat();
            }
            alpha_ptr = alpha_fp32_conv.data();
        } else {
            alpha_ptr = output_tensors[0].GetTensorMutableData<float>();
        }

        float* fg_ptr = nullptr;
        std::vector<float> fg_fp32_conv;
        std::vector<int64_t> fg_shape;
        size_t fg_image_stride = 0;
        if (output_tensors.size() > 1) {
            auto fg_info = output_tensors[1].GetTensorTypeAndShapeInfo();
            auto fg_type = fg_info.GetElementType();
            fg_shape = fg_info.GetShape();
            fg_image_stride = static_cast<size_t>(fg_shape[1]) * static_cast<size_t>(fg_shape[2]) *
                              static_cast<size_t>(fg_shape[3]);
            if (fg_type == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT16) {
                const Ort::Float16_t* fg_fp16 = output_tensors[1].GetTensorData<Ort::Float16_t>();
                fg_fp32_conv.resize(fg_image_stride);
                for (size_t index = 0; index < fg_image_stride; ++index) {
                    fg_fp32_conv[index] = fg_fp16[index].ToFloat();
                }
                fg_ptr = fg_fp32_conv.data();
            } else {
                fg_ptr = output_tensors[1].GetTensorMutableData<float>();
            }
        }

        FrameResult result;
        result.alpha = ImageBuffer(rgb.width, rgb.height, 1);
        if (fg_ptr != nullptr) {
            result.foreground = ImageBuffer(rgb.width, rgb.height, 3);
        }

        bool use_lanczos = params.upscale_method == UpscaleMethod::Lanczos4;
        Image model_alpha = ensure_resize_buffer(2, static_cast<int>(alpha_shape[3]),
                                                 static_cast<int>(alpha_shape[2]),
                                                 static_cast<int>(alpha_shape[1]));
        common::measure_stage(
            on_stage, "frame_extract_outputs",
            [&]() {
                ColorUtils::from_planar(alpha_ptr, model_alpha);
                if (use_lanczos) {
                    ColorUtils::resize_lanczos_into(model_alpha, result.alpha.view(),
                                                    m_color_utils_state);
                } else {
                    ColorUtils::resize_into(model_alpha, result.alpha.view());
                }
                ColorUtils::clamp_image(result.alpha.view(), 0.0F, 1.0F);

                if (fg_ptr != nullptr) {
                    Image model_fg = ensure_resize_buffer(3, static_cast<int>(fg_shape[3]),
                                                          static_cast<int>(fg_shape[2]),
                                                          static_cast<int>(fg_shape[1]));
                    ColorUtils::from_planar(fg_ptr, model_fg);
                    if (use_lanczos) {
                        ColorUtils::resize_lanczos_into(model_fg, result.foreground.view(),
                                                        m_color_utils_state);
                    } else {
                        ColorUtils::resize_into(model_fg, result.foreground.view());
                    }
                }
            },
            1);

        return result;
    } catch (const Ort::Exception& e) {
        return Unexpected(Error{ErrorCode::InferenceFailed,
                                std::string("ONNX Runtime execution failed: ") + e.what()});
    }
}

Result<FrameResult> InferenceSession::run(const Image& rgb, const Image& alpha_hint,
                                          const InferenceParams& params,
                                          StageTimingCallback on_stage) {
    // Check for tiling request
    int target_res =
        params.target_resolution > 0 ? params.target_resolution : m_recommended_resolution;

    // Only tile if requested and input is significantly larger than model
    if (params.enable_tiling && (rgb.width > target_res || rgb.height > target_res)) {
        return run_tiled(rgb, alpha_hint, params, target_res, on_stage);
    }

    auto result_res = infer_raw(rgb, alpha_hint, params, on_stage);
    if (!result_res) return result_res;

    apply_post_process(*result_res, params, rgb, on_stage);
    return result_res;
}

}  // namespace corridorkey
