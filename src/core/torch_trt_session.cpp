#include "torch_trt_session.hpp"

#include <array>
#include <corridorkey/detail/constants.hpp>
#include <cstring>
#include <ctime>
#include <fstream>
#include <mutex>
#include <string>
#include <utility>

#include "common/parallel_for.hpp"
#include "common/runtime_paths.hpp"
#include "common/stage_profiler.hpp"
#include "inference_output_validation.hpp"
#include "inference_session_metadata.hpp"
#include "post_process/color_utils.hpp"

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4244 4267 4702)
#endif

#if defined(CORRIDORKEY_WITH_TORCH_TENSORRT) && CORRIDORKEY_WITH_TORCH_TENSORRT
#include <c10/core/DeviceGuard.h>
#include <c10/util/Half.h>
#include <torch/nn/functional/upsampling.h>
#include <torch/script.h>
#include <windows.h>
#endif

namespace corridorkey::core {

namespace {

#if defined(_WIN32)
constexpr std::array<std::string_view, 8> kRequiredTorchRuntimeFiles = {
    "torchtrt.dll", "torch.dll",    "torch_cpu.dll",  "torch_cuda.dll",
    "c10.dll",      "c10_cuda.dll", "nvinfer_10.dll", "nvinfer_plugin_10.dll",
};

std::filesystem::path runtime_library_dir() {
    if (auto executable_path = common::current_executable_path(); executable_path.has_value()) {
        return executable_path->parent_path();
    }
    return {};
}
#endif

}  // namespace

#if defined(CORRIDORKEY_WITH_TORCH_TENSORRT) && CORRIDORKEY_WITH_TORCH_TENSORRT

namespace {

void debug_log(const std::string& message) {
#ifdef _WIN32
    static const bool enabled = []() {
        char* value = nullptr;
        size_t len = 0;
        const bool result = _dupenv_s(&value, &len, "CORRIDORKEY_TORCHTRT_DEBUG") == 0 &&
                            value != nullptr && std::string_view(value) == "1";
        free(value);
        return result;
    }();
    if (!enabled) {
        return;
    }
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
            if (!ts.empty() && ts.back() == '\n') {
                ts.pop_back();
            }
            log_file << ts << " [TorchTrtSession] " << message << std::endl;
        }
        free(local_app_data);
    }
#else
    (void)message;
#endif
}

c10::DeviceIndex torch_device_index(int device_index) {
    return static_cast<c10::DeviceIndex>(device_index);
}

Result<void> validate_output_values(std::span<const float> values, std::string_view label) {
    return core::validate_finite_values(values, label);
}

Result<void> validate_output_image(Image image, std::string_view label) {
    return core::validate_finite_image(image, label);
}

std::string format_windows_error_message(DWORD error_code) {
    if (error_code == 0) {
        return {};
    }

    LPWSTR buffer = nullptr;
    const DWORD length = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, error_code, 0, reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);
    if (length == 0 || buffer == nullptr) {
        return "Windows error " + std::to_string(error_code);
    }

    std::wstring message(buffer, length);
    LocalFree(buffer);
    while (!message.empty() &&
           (message.back() == L'\r' || message.back() == L'\n' || message.back() == L' ')) {
        message.pop_back();
    }

    if (message.empty()) {
        return "Windows error " + std::to_string(error_code);
    }

    return std::string(message.begin(), message.end()) + " (code " + std::to_string(error_code) +
           ")";
}

HMODULE load_required_library(const std::filesystem::path& library_path) {
    const auto absolute_library_path = std::filesystem::absolute(library_path);
    const auto library_dir = absolute_library_path.parent_path();
    DLL_DIRECTORY_COOKIE directory_cookie = nullptr;
    if (!library_dir.empty()) {
        directory_cookie = AddDllDirectory(library_dir.wstring().c_str());
    }

    HMODULE module =
        LoadLibraryExW(absolute_library_path.wstring().c_str(), nullptr,
                       LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS |
                           LOAD_LIBRARY_SEARCH_USER_DIRS);
    if (directory_cookie != nullptr) {
        RemoveDllDirectory(directory_cookie);
    }
    if (module != nullptr) {
        return module;
    }

    return LoadLibraryW(absolute_library_path.wstring().c_str());
}

Result<HMODULE> load_torchtrt_runtime_library() {
    static HMODULE loaded_module = nullptr;
    if (loaded_module != nullptr) {
        return loaded_module;
    }

    const auto library_dir = runtime_library_dir();
    if (library_dir.empty()) {
        return Unexpected<Error>{Error{
            ErrorCode::HardwareNotSupported,
            "Torch-TensorRT runtime could not resolve the executable directory.",
        }};
    }

    const auto torchtrt_path = library_dir / "torchtrt.dll";
    if (!std::filesystem::exists(torchtrt_path)) {
        return Unexpected<Error>{Error{
            ErrorCode::HardwareNotSupported,
            "Torch-TensorRT runtime DLL is missing from the packaged runtime: " +
                torchtrt_path.string(),
        }};
    }

    HMODULE module = load_required_library(torchtrt_path);
    if (module == nullptr) {
        const auto error_message = format_windows_error_message(GetLastError());
        return Unexpected<Error>{Error{
            ErrorCode::HardwareNotSupported,
            "Torch-TensorRT runtime DLL could not be loaded: " + torchtrt_path.string() +
                (error_message.empty() ? std::string() : " (" + error_message + ")"),
        }};
    }

    loaded_module = module;
    return loaded_module;
}

}  // namespace

class TorchTrtSession::Impl {
   public:
    DeviceInfo device = {};
    int model_resolution = 0;
    torch::jit::Module module;
    ImageBuffer prepared_rgb = {};
    ImageBuffer prepared_hint = {};
    ImageBuffer model_alpha = {};
    ImageBuffer model_fg = {};
    ColorUtils::State color_utils_state = {};
    torch::Tensor host_input;
    torch::Tensor device_input;
    torch::Tensor host_alpha;
    torch::Tensor host_fg;
    torch::Tensor host_alpha_float;
    torch::Tensor host_fg_hwc_float;
    HMODULE torchtrt_runtime = nullptr;

    explicit Impl(DeviceInfo requested_device) : device(std::move(requested_device)) {}

    ~Impl() = default;

    [[nodiscard]] Result<void> ensure_buffers(int resolution) {
        if (resolution <= 0) {
            return Unexpected<Error>{Error{
                ErrorCode::InvalidParameters,
                "Torch-TensorRT requires a positive fixed model resolution.",
            }};
        }

        if (prepared_rgb.view().width != resolution || prepared_rgb.view().height != resolution ||
            prepared_rgb.view().channels != 3) {
            prepared_rgb = ImageBuffer(resolution, resolution, 3);
        }
        if (prepared_hint.view().width != resolution || prepared_hint.view().height != resolution ||
            prepared_hint.view().channels != 1) {
            prepared_hint = ImageBuffer(resolution, resolution, 1);
        }
        if (model_alpha.view().width != resolution || model_alpha.view().height != resolution ||
            model_alpha.view().channels != 1) {
            model_alpha = ImageBuffer(resolution, resolution, 1);
        }
        if (model_fg.view().width != resolution || model_fg.view().height != resolution ||
            model_fg.view().channels != 3) {
            model_fg = ImageBuffer(resolution, resolution, 3);
        }

        auto pinned_half = torch::TensorOptions()
                               .dtype(torch::kFloat16)
                               .layout(torch::kStrided)
                               .device(torch::kCPU)
                               .pinned_memory(true);
        auto cuda_half = torch::TensorOptions()
                             .dtype(torch::kFloat16)
                             .layout(torch::kStrided)
                             .device(torch::kCUDA, device.device_index);

        if (!host_input.defined() || host_input.size(2) != resolution ||
            host_input.size(3) != resolution) {
            host_input = torch::zeros({1, 4, resolution, resolution}, pinned_half);
            device_input = torch::zeros({1, 4, resolution, resolution}, cuda_half);
            host_alpha = torch::zeros({1, 1, resolution, resolution}, pinned_half);
            host_fg = torch::zeros({1, 3, resolution, resolution}, pinned_half);
            host_alpha_float = torch::zeros({resolution, resolution}, torch::TensorOptions()
                                                                          .dtype(torch::kFloat32)
                                                                          .layout(torch::kStrided)
                                                                          .device(torch::kCPU)
                                                                          .pinned_memory(true));
            host_fg_hwc_float =
                torch::zeros({resolution, resolution, 3}, torch::TensorOptions()
                                                              .dtype(torch::kFloat32)
                                                              .layout(torch::kStrided)
                                                              .device(torch::kCPU)
                                                              .pinned_memory(true));
        }

        return {};
    }
};

TorchTrtSession::TorchTrtSession(std::unique_ptr<Impl> impl) noexcept : m_impl(std::move(impl)) {}

TorchTrtSession::~TorchTrtSession() = default;
TorchTrtSession::TorchTrtSession(TorchTrtSession&&) noexcept = default;
TorchTrtSession& TorchTrtSession::operator=(TorchTrtSession&&) noexcept = default;

Result<std::unique_ptr<TorchTrtSession>> TorchTrtSession::create(
    const std::filesystem::path& model_path, DeviceInfo device) {
    debug_log("create.begin model=" + model_path.string() +
              " backend=" + std::to_string(static_cast<int>(device.backend)) +
              " device_index=" + std::to_string(device.device_index));
    if (device.backend != Backend::TensorRT) {
        return Unexpected<Error>{Error{
            ErrorCode::HardwareNotSupported,
            "Torch-TensorRT requires the Windows RTX TensorRT backend.",
        }};
    }
    if (!std::filesystem::exists(model_path)) {
        return Unexpected<Error>{Error{
            ErrorCode::ModelLoadFailed,
            "Torch-TensorRT artifact not found: " + model_path.string(),
        }};
    }

    auto impl = std::make_unique<Impl>(device);
    impl->model_resolution = infer_model_resolution_from_path(model_path).value_or(0);
    if (impl->model_resolution <= 0) {
        return Unexpected<Error>{Error{
            ErrorCode::InvalidParameters,
            "Torch-TensorRT artifact name must encode a fixed resolution rung.",
        }};
    }

    auto runtime_library = load_torchtrt_runtime_library();
    if (!runtime_library) {
        debug_log("create.runtime_library_error " + runtime_library.error().message);
        return Unexpected<Error>(runtime_library.error());
    }
    impl->torchtrt_runtime = *runtime_library;
    debug_log("create.runtime_library_loaded");

    auto buffers = impl->ensure_buffers(impl->model_resolution);
    if (!buffers) {
        debug_log("create.ensure_buffers_error " + buffers.error().message);
        return Unexpected<Error>(buffers.error());
    }
    debug_log("create.ensure_buffers_ready resolution=" + std::to_string(impl->model_resolution));

    try {
        debug_log("create.before_inference_mode");
        c10::InferenceMode inference_guard;
        debug_log("create.after_inference_mode");
        c10::DeviceGuard device_guard(
            c10::Device(c10::DeviceType::CUDA, torch_device_index(device.device_index)));
        debug_log("create.after_device_guard");
        std::ifstream model_stream(model_path, std::ios::binary);
        if (!model_stream.is_open()) {
            return Unexpected<Error>{Error{
                ErrorCode::ModelLoadFailed,
                "Torch-TensorRT artifact could not be opened: " +
                    std::filesystem::absolute(model_path).string(),
            }};
        }
        impl->module = torch::jit::load(
            model_stream, c10::Device(c10::kCUDA, torch_device_index(device.device_index)));
        debug_log("create.after_jit_load");
        impl->module.eval();
        debug_log("create.after_eval");

        // Warm the engine once during prepare so the frame hot path only measures steady-state
        // work.
        auto warm_input =
            torch::zeros({1, 4, impl->model_resolution, impl->model_resolution},
                         torch::TensorOptions()
                             .dtype(torch::kFloat16)
                             .device(torch::kCUDA, torch_device_index(device.device_index)));
        debug_log("create.before_warmup_forward");
        auto warm_output = impl->module.forward({warm_input});
        debug_log("create.after_warmup_forward");
        auto warm_elements = warm_output.toTuple()->elements();
        debug_log("create.after_warmup_tuple size=" + std::to_string(warm_elements.size()));
        if (warm_elements.size() >= 2) {
            (void)warm_elements[0].toTensor().cpu();
            (void)warm_elements[1].toTensor().cpu();
            debug_log("create.after_warmup_d2h");
        }
    } catch (const c10::Error& error) {
        debug_log(std::string("create.c10_error ") + error.what());
        return Unexpected<Error>{Error{
            ErrorCode::ModelLoadFailed,
            std::string("Torch-TensorRT C++ load failed: ") + error.what(),
        }};
    }

    debug_log("create.ready");

    return std::unique_ptr<TorchTrtSession>(new TorchTrtSession(std::move(impl)));
}

Result<FrameResult> TorchTrtSession::infer(const Image& rgb, const Image& alpha_hint,
                                           const InferenceParams& params,
                                           StageTimingCallback on_stage) {
    namespace F = torch::nn::functional;

    if (params.target_resolution > 0 && params.target_resolution != m_impl->model_resolution) {
        return Unexpected<Error>{Error{
            ErrorCode::InvalidParameters,
            "Torch-TensorRT artifacts are fixed-shape. Select a matching quality rung.",
        }};
    }

    auto buffers = m_impl->ensure_buffers(m_impl->model_resolution);
    if (!buffers) {
        return Unexpected<Error>(buffers.error());
    }

    const int model_resolution = m_impl->model_resolution;
    const size_t channel_stride =
        static_cast<size_t>(model_resolution) * static_cast<size_t>(model_resolution);

    common::measure_stage(
        on_stage, "frame_prepare_inputs",
        [&]() {
            ColorUtils::resize_area_into(rgb, m_impl->prepared_rgb.view(),
                                         m_impl->color_utils_state);
            ColorUtils::resize_area_into(alpha_hint, m_impl->prepared_hint.view(),
                                         m_impl->color_utils_state);

            auto* host_ptr = m_impl->host_input.data_ptr<c10::Half>();
            common::parallel_for_rows(model_resolution, [&](int y_begin, int y_end) {
                for (int y = y_begin; y < y_end; ++y) {
                    for (int x = 0; x < model_resolution; ++x) {
                        const size_t index =
                            static_cast<size_t>(y) * static_cast<size_t>(model_resolution) + x;
                        host_ptr[0 * channel_stride + index] =
                            c10::Half((m_impl->prepared_rgb.view()(y, x, 0) - 0.485f) / 0.229f);
                        host_ptr[1 * channel_stride + index] =
                            c10::Half((m_impl->prepared_rgb.view()(y, x, 1) - 0.456f) / 0.224f);
                        host_ptr[2 * channel_stride + index] =
                            c10::Half((m_impl->prepared_rgb.view()(y, x, 2) - 0.406f) / 0.225f);
                        host_ptr[3 * channel_stride + index] =
                            c10::Half(m_impl->prepared_hint.view()(y, x, 0));
                    }
                }
            });
        },
        1);

    torch::Tensor alpha_tensor;
    torch::Tensor fg_tensor;
    try {
        c10::InferenceMode inference_guard;
        c10::DeviceGuard device_guard(
            c10::Device(c10::DeviceType::CUDA, torch_device_index(m_impl->device.device_index)));
        auto output = common::measure_stage(
            on_stage, "torchtrt_run",
            [&]() -> torch::IValue {
                m_impl->device_input.copy_(m_impl->host_input, true);
                return m_impl->module.forward({m_impl->device_input});
            },
            1);

        auto elements = output.toTuple()->elements();
        if (elements.size() < 2) {
            return Unexpected<Error>{Error{
                ErrorCode::InferenceFailed,
                "Torch-TensorRT module returned an unexpected output contract.",
            }};
        }
        alpha_tensor = elements[0].toTensor();
        fg_tensor = elements[1].toTensor();
        m_impl->host_alpha.copy_(alpha_tensor);
        m_impl->host_fg.copy_(fg_tensor);
    } catch (const c10::Error& error) {
        return Unexpected<Error>{Error{
            ErrorCode::InferenceFailed,
            std::string("Torch-TensorRT execution failed: ") + error.what(),
        }};
    }

    FrameResult result;
    result.alpha = ImageBuffer(rgb.width, rgb.height, 1);
    result.foreground = ImageBuffer(rgb.width, rgb.height, 3);

    common::measure_stage(
        on_stage, "frame_extract_outputs",
        [&]() {
            auto model_alpha_view = m_impl->model_alpha.view();
            auto model_fg_view = m_impl->model_fg.view();
            const bool exact_output_size =
                rgb.width == model_resolution && rgb.height == model_resolution;

            if (exact_output_size) {
                auto alpha_hw = alpha_tensor.squeeze(0).squeeze(0).to(torch::kFloat32).contiguous();
                auto fg_hwc =
                    fg_tensor.squeeze(0).permute({1, 2, 0}).contiguous().to(torch::kFloat32);
                m_impl->host_alpha_float.copy_(alpha_hw);
                m_impl->host_fg_hwc_float.copy_(fg_hwc);

                std::memcpy(result.alpha.view().data.data(),
                            m_impl->host_alpha_float.data_ptr<float>(),
                            channel_stride * sizeof(float));
                std::memcpy(result.foreground.view().data.data(),
                            m_impl->host_fg_hwc_float.data_ptr<float>(),
                            channel_stride * static_cast<size_t>(3) * sizeof(float));
            } else {
                const auto* alpha_ptr = m_impl->host_alpha.data_ptr<c10::Half>();
                const auto* fg_ptr = m_impl->host_fg.data_ptr<c10::Half>();
                common::parallel_for_rows(model_resolution, [&](int y_begin, int y_end) {
                    for (int y = y_begin; y < y_end; ++y) {
                        for (int x = 0; x < model_resolution; ++x) {
                            const size_t index =
                                static_cast<size_t>(y) * static_cast<size_t>(model_resolution) + x;
                            model_alpha_view(y, x, 0) = static_cast<float>(alpha_ptr[index]);
                            model_fg_view(y, x, 0) =
                                static_cast<float>(fg_ptr[0 * channel_stride + index]);
                            model_fg_view(y, x, 1) =
                                static_cast<float>(fg_ptr[1 * channel_stride + index]);
                            model_fg_view(y, x, 2) =
                                static_cast<float>(fg_ptr[2 * channel_stride + index]);
                        }
                    }
                });

                const bool use_lanczos = params.upscale_method == UpscaleMethod::Lanczos4;
                if (use_lanczos) {
                    ColorUtils::resize_lanczos_into(model_alpha_view, result.alpha.view(),
                                                    m_impl->color_utils_state);
                    ColorUtils::resize_lanczos_into(model_fg_view, result.foreground.view(),
                                                    m_impl->color_utils_state);
                } else {
                    auto alpha_resized =
                        F::interpolate(alpha_tensor.to(torch::kFloat32),
                                       F::InterpolateFuncOptions()
                                           .size(std::vector<int64_t>{rgb.height, rgb.width})
                                           .mode(torch::kBilinear)
                                           .align_corners(false));
                    auto fg_resized =
                        F::interpolate(fg_tensor.to(torch::kFloat32),
                                       F::InterpolateFuncOptions()
                                           .size(std::vector<int64_t>{rgb.height, rgb.width})
                                           .mode(torch::kBilinear)
                                           .align_corners(false));
                    auto alpha_host = alpha_resized.squeeze(0).squeeze(0).contiguous().cpu();
                    auto fg_host = fg_resized.squeeze(0).permute({1, 2, 0}).contiguous().cpu();
                    std::memcpy(result.alpha.view().data.data(), alpha_host.data_ptr<float>(),
                                static_cast<size_t>(rgb.width) * static_cast<size_t>(rgb.height) *
                                    sizeof(float));
                    std::memcpy(result.foreground.view().data.data(), fg_host.data_ptr<float>(),
                                static_cast<size_t>(rgb.width) * static_cast<size_t>(rgb.height) *
                                    static_cast<size_t>(3) * sizeof(float));
                }
            }
            ColorUtils::clamp_image(result.alpha.view(), 0.0F, 1.0F);
        },
        1);

    auto alpha_validation = validate_output_image(result.alpha.view(), "torchtrt_alpha_output");
    if (!alpha_validation) {
        return Unexpected(alpha_validation.error());
    }
    auto fg_validation = validate_output_image(result.foreground.view(), "torchtrt_fg_output");
    if (!fg_validation) {
        return Unexpected(fg_validation.error());
    }
    auto alpha_values_validation =
        validate_output_values(result.alpha.const_view().data, "torchtrt_alpha_output");
    if (!alpha_values_validation) {
        return Unexpected(alpha_values_validation.error());
    }

    return result;
}

int TorchTrtSession::model_resolution() const {
    return m_impl->model_resolution;
}

DeviceInfo TorchTrtSession::device() const {
    return m_impl->device;
}


#else

class TorchTrtSession::Impl {};

TorchTrtSession::TorchTrtSession(std::unique_ptr<Impl> impl) noexcept : m_impl(std::move(impl)) {}
TorchTrtSession::~TorchTrtSession() = default;
TorchTrtSession::TorchTrtSession(TorchTrtSession&&) noexcept = default;
TorchTrtSession& TorchTrtSession::operator=(TorchTrtSession&&) noexcept = default;

Result<std::unique_ptr<TorchTrtSession>> TorchTrtSession::create(
    const std::filesystem::path& model_path, DeviceInfo device) {
    (void)model_path;
    (void)device;
    return Unexpected<Error>{Error{
        ErrorCode::HardwareNotSupported,
        "Torch-TensorRT runtime support was not compiled into this build.",
    }};
}

Result<FrameResult> TorchTrtSession::infer(const Image& rgb, const Image& alpha_hint,
                                           const InferenceParams& params,
                                           StageTimingCallback on_stage) {
    (void)rgb;
    (void)alpha_hint;
    (void)params;
    (void)on_stage;
    return Unexpected<Error>{Error{
        ErrorCode::HardwareNotSupported,
        "Torch-TensorRT runtime support was not compiled into this build.",
    }};
}

int TorchTrtSession::model_resolution() const {
    return 0;
}

DeviceInfo TorchTrtSession::device() const {
    return {};
}

#endif

bool torch_tensorrt_runtime_available() {
#if defined(_WIN32)
    const auto library_dir = runtime_library_dir();
    if (library_dir.empty()) {
        return false;
    }

    for (const auto filename : kRequiredTorchRuntimeFiles) {
        std::error_code error;
        if (!std::filesystem::exists(library_dir / std::filesystem::path(filename), error) ||
            error) {
            return false;
        }
    }

    return true;
#else
    return false;
#endif
}

}  // namespace corridorkey::core

#ifdef _MSC_VER
#pragma warning(pop)
#endif
