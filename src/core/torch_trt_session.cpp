#include "torch_trt_session.hpp"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <memory>
#include <optional>
#include <regex>
#include <string>
#include <utility>
#include <vector>

// Pulling individual c10/torch headers piecemeal triggers CUDA include
// resolution that the vendored torchtrt-windows tree intentionally
// stubs out (the .ts is loaded by name and TensorRT plugins register
// themselves at LoadLibrary time, so the application never directly
// touches cuda_runtime_api.h). Stick to torch/script.h + torch/cuda.h:
// the umbrella headers provide every torch:: + c10:: + at:: symbol we
// reference (jit::load, jit::script::Module, IValue, NoGradGuard,
// Tensor, Device, kCUDA, kFloat16/32, cuda::is_available,
// cuda::synchronize, c10::Error) and pull only the slice of CUDA
// declarations libtorch already proxies internally.
#include <torch/cuda.h>
#include <torch/script.h>

#include "common/stage_profiler.hpp"
#include "core/model_input_normalization.hpp"
// Strategy C, Sprint 1 PR 1 follow-up: the runtime DLL arming
// (AddDllDirectory + LoadLibraryEx of torchtrt.dll +
// corridorkey_torchtrt.dll) lives in a torch-free TU compiled into
// corridorkey_core, so the base runtime can prepare the loader before
// triggering the delay-load of this DLL. Calling arm_torchtrt_runtime
// from inside this TU would defeat the indirection because reaching
// any symbol here implies the DLL is already resolved.

namespace corridorkey::core {

namespace {

std::optional<int> resolution_from_filename(const std::filesystem::path& path) {
    // Fixed TorchTRT engines carry a trailing resolution token; dynamic
    // TorchScript artifacts intentionally do not.
    static const std::regex pattern(R"(.*_(\d+)\.ts$)");
    std::smatch match;
    auto filename = path.filename().string();
    if (!std::regex_match(filename, match, pattern) || match.size() != 2) {
        return std::nullopt;
    }
    try {
        return std::stoi(match.str(1));
    } catch (...) {
        return std::nullopt;
    }
}

// Splits the IValue returned by torch::jit::Module::forward into the
// (alpha, foreground) tensor pair our pipeline expects. Returns an empty
// pair on error so the caller can pick the right Error message; both
// tensors stay default-constructed (.defined() == false) on failure.
struct AlphaFgTensors {
    torch::Tensor alpha;
    torch::Tensor foreground;
};

std::optional<AlphaFgTensors> split_forward_output(const torch::IValue& raw_out) {
    if (raw_out.isTuple()) {
        const auto& elements = raw_out.toTuple()->elements();
        if (elements.empty()) {
            return std::nullopt;
        }
        AlphaFgTensors result;
        result.alpha = elements.at(0).toTensor();
        if (elements.size() > 1) {
            result.foreground = elements.at(1).toTensor();
        }
        return result;
    }
    if (raw_out.isTensor()) {
        return AlphaFgTensors{.alpha = raw_out.toTensor(), .foreground = {}};
    }
    return std::nullopt;
}

bool tensor_has_shape(const torch::Tensor& tensor, int channels, int width, int height) {
    return tensor.defined() && tensor.dim() == 4 && tensor.size(0) == 1 &&
           tensor.size(1) == channels && tensor.size(2) == height && tensor.size(3) == width;
}

constexpr int kDynamicInputAlignment = 32;

int round_up_to_multiple(int value, int multiple) {
    return ((value + multiple - 1) / multiple) * multiple;
}

struct DynamicPadding {
    int top = 0;
    int left = 0;
    int height = 0;
    int width = 0;
};

DynamicPadding dynamic_padding_for_input(int width, int height, bool dynamic_resolution) {
    if (!dynamic_resolution) {
        return {.height = height, .width = width};
    }
    const int padded_width = round_up_to_multiple(width, kDynamicInputAlignment);
    const int padded_height = round_up_to_multiple(height, kDynamicInputAlignment);
    return {
        .top = (padded_height - height) / 2,
        .left = (padded_width - width) / 2,
        .height = padded_height,
        .width = padded_width,
    };
}

torch::Tensor allocate_host_tensor(const std::vector<int64_t>& shape) {
    const auto base_options = torch::TensorOptions().dtype(torch::kFloat32).device(torch::kCPU);
    try {
        return torch::empty(shape, base_options.pinned_memory(true));
    } catch (const c10::Error&) {
        return torch::empty(shape, base_options);
    }
}

torch::Tensor allocate_host_input_tensor(int height, int width) {
    return allocate_host_tensor({1, 4, height, width});
}

// NOLINTBEGIN(bugprone-easily-swappable-parameters) - index and size match the reflection formula.
int reflect_index(int index, int size) {
    if (size <= 1) {
        return 0;
    }
    const int period = (2 * size) - 2;
    int reflected = index % period;
    if (reflected < 0) {
        reflected += period;
    }
    if (reflected >= size) {
        reflected = period - reflected;
    }
    return reflected;
}
// NOLINTEND(bugprone-easily-swappable-parameters)

torch::Tensor flatten_cropped_hwc(const torch::Tensor& tensor_cuda, int output_width,
                                  int output_height, int pad_top, int pad_left) {
    auto cropped = tensor_cuda.detach()
                       .narrow(2, pad_top, output_height)
                       .narrow(3, pad_left, output_width)
                       .to(torch::kFloat32);
    return cropped.permute({0, 2, 3, 1}).contiguous().view({-1});
}

struct MaterializedOutputTensors {
    ImageBuffer alpha;
    ImageBuffer foreground;
};

MaterializedOutputTensors materialize_outputs(const torch::Tensor& alpha_cuda,
                                              const torch::Tensor& fg_cuda, int output_width,
                                              int output_height, int pad_top, int pad_left,
                                              bool include_foreground) {
    auto alpha_flat =
        flatten_cropped_hwc(alpha_cuda, output_width, output_height, pad_top, pad_left);
    torch::Tensor bulk_cuda;
    if (include_foreground) {
        auto fg_flat = flatten_cropped_hwc(fg_cuda, output_width, output_height, pad_top, pad_left);
        bulk_cuda = torch::cat({alpha_flat, fg_flat}, 0).contiguous();
    } else {
        bulk_cuda = alpha_flat.contiguous();
    }

    auto host_bulk = allocate_host_tensor({bulk_cuda.numel()});
    host_bulk.copy_(bulk_cuda, true);
    torch::cuda::synchronize();

    MaterializedOutputTensors result;
    const auto alpha_count =
        static_cast<std::size_t>(output_width) * static_cast<std::size_t>(output_height);
    const auto* src = host_bulk.data_ptr<float>();
    result.alpha = ImageBuffer(output_width, output_height, 1);
    std::memcpy(result.alpha.view().data.data(), src, alpha_count * sizeof(float));

    if (include_foreground) {
        const auto foreground_count = alpha_count * 3U;
        result.foreground = ImageBuffer(output_width, output_height, 3);
        std::memcpy(result.foreground.view().data.data(), src + alpha_count,
                    foreground_count * sizeof(float));
    }
    return result;
}

// NOLINTBEGIN(bugprone-easily-swappable-parameters) - these dimensions mirror tensor shape order.
Result<FrameResult> forward_and_materialize(torch::jit::script::Module& module,
                                            const torch::Tensor& cuda_input, int output_width,
                                            int output_height, int inference_width,
                                            int inference_height, int pad_top, int pad_left,
                                            bool output_alpha_only,
                                            const StageTimingCallback& on_stage) {
    torch::IValue raw_out;
    common::measure_stage(on_stage, "torchtrt_forward", [&]() {
        const torch::NoGradGuard no_grad;
        raw_out = module.forward({cuda_input});
        torch::cuda::synchronize();
    });

    auto split = split_forward_output(raw_out);
    if (!split.has_value() || !split->alpha.defined()) {
        return Unexpected<Error>{
            Error{.code = ErrorCode::InferenceFailed,
                  .message = "TorchTRT forward returned no usable alpha tensor"}};
    }
    if (!tensor_has_shape(split->alpha, 1, inference_width, inference_height)) {
        return Unexpected<Error>{Error{
            .code = ErrorCode::InferenceFailed,
            .message = "TorchScript RTX alpha output shape did not match input " +
                       std::to_string(inference_width) + "x" + std::to_string(inference_height)}};
    }
    if (!output_alpha_only && split->foreground.defined() &&
        !tensor_has_shape(split->foreground, 3, inference_width, inference_height)) {
        return Unexpected<Error>{Error{
            .code = ErrorCode::InferenceFailed,
            .message = "TorchScript RTX foreground output shape did not match input " +
                       std::to_string(inference_width) + "x" + std::to_string(inference_height)}};
    }

    FrameResult result;
    common::measure_stage(on_stage, "torchtrt_extract_outputs", [&]() {
        const bool include_foreground = !output_alpha_only && split->foreground.defined();
        auto materialized =
            materialize_outputs(split->alpha, split->foreground, output_width, output_height,
                                pad_top, pad_left, include_foreground);
        result.alpha = std::move(materialized.alpha);
        result.foreground = std::move(materialized.foreground);
    });
    return result;
}
// NOLINTEND(bugprone-easily-swappable-parameters)

}  // namespace

class TorchTrtSession::Impl {
   public:
    Impl() = default;

    torch::jit::script::Module module;
    int resolution = 0;
    // Engine input dtype - inferred from filename (corridorkey_*_fp32_<res>.ts
    // vs corridorkey_*_fp16_<res>.ts). Sprint 0 found blue 1536+ needs FP32
    // because FP16 NaNs in LayerNorm/Softmax for the blue checkpoint; green
    // is stable at FP16 across the full ladder.
    torch::Dtype input_dtype = torch::kFloat16;
    DeviceInfo device;
};

namespace {
torch::Dtype infer_input_dtype(const std::filesystem::path& path) {
    auto stem = path.stem().string();
    if (stem.find("fp32") != std::string::npos) {
        return torch::kFloat32;
    }
    return torch::kFloat16;
}
}  // namespace

TorchTrtSession::TorchTrtSession() : m_impl(std::make_unique<Impl>()) {}
TorchTrtSession::~TorchTrtSession() = default;
TorchTrtSession::TorchTrtSession(TorchTrtSession&&) noexcept = default;
TorchTrtSession& TorchTrtSession::operator=(TorchTrtSession&&) noexcept = default;

Result<std::unique_ptr<TorchTrtSession>> TorchTrtSession::create(
    const std::filesystem::path& ts_path, const DeviceInfo& device,
    StageTimingCallback
        on_stage) {  // NOLINT(performance-unnecessary-value-param) — matches MlxSession signature.
    if (!std::filesystem::exists(ts_path)) {
        return Unexpected<Error>{
            Error{.code = ErrorCode::ModelLoadFailed,
                  .message = "TorchTRT engine not found: " + ts_path.string()}};
    }

    // Strategy C, Sprint 1 PR 1 follow-up: the caller is responsible for
    // arming the runtime via torch_trt_loader::arm_torchtrt_runtime
    // BEFORE invoking any symbol from this DLL. By the time control
    // reaches this function, the OS has already resolved every torch /
    // torchtrt / cuda dependency through the delay-loaded
    // corridorkey_torchtrt.dll, which means AddDllDirectory inside this
    // TU is too late.

    if (!torch::cuda::is_available()) {
        return Unexpected<Error>{Error{
            .code = ErrorCode::HardwareNotSupported,
            .message = "CUDA not available; TorchTRT engines require an Ampere or newer GPU."}};
    }

    auto session = std::unique_ptr<TorchTrtSession>(new TorchTrtSession());
    session->m_impl->device = device;

    auto inferred_res = resolution_from_filename(ts_path);
    session->m_impl->resolution = inferred_res.value_or(0);
    session->m_impl->input_dtype = infer_input_dtype(ts_path);

    try {
        common::measure_stage(on_stage, "torchtrt_jit_load", [&]() {
            session->m_impl->module =
                torch::jit::load(ts_path.string(), torch::Device(torch::kCUDA));
            session->m_impl->module.eval();
        });
    } catch (const c10::Error& e) {
        return Unexpected<Error>{
            Error{.code = ErrorCode::ModelLoadFailed,
                  .message = std::string("torch::jit::load failed: ") + e.what()}};
    } catch (const std::exception& e) {
        return Unexpected<Error>{
            Error{.code = ErrorCode::ModelLoadFailed,
                  .message = std::string("torch::jit::load std::exception: ") + e.what()}};
    }

    return session;
}

int TorchTrtSession::model_resolution() const {
    return m_impl ? m_impl->resolution : 0;
}

Result<FrameResult> TorchTrtSession::infer(const Image& rgb, const Image& alpha_hint,
                                           bool output_alpha_only,
                                           // NOLINTNEXTLINE(performance-unnecessary-value-param)
                                           StageTimingCallback on_stage) {
    if (m_impl == nullptr) {
        return Unexpected<Error>{Error{.code = ErrorCode::InferenceFailed,
                                       .message = "TorchTrtSession in moved-from state"}};
    }
    const int fixed_resolution = m_impl->resolution;
    const bool dynamic_resolution = fixed_resolution == 0;
    const int width = rgb.width;
    const int height = rgb.height;
    if (rgb.channels != 3 || width <= 0 || height <= 0) {
        return Unexpected<Error>{Error{
            .code = ErrorCode::InvalidParameters,
            .message = "TorchScript RTX session expects RGB input with positive width/height and "
                       "3 channels; got " +
                       std::to_string(rgb.width) + "x" + std::to_string(rgb.height) + "x" +
                       std::to_string(rgb.channels)}};
    }
    if (!dynamic_resolution && (width != fixed_resolution || height != fixed_resolution)) {
        return Unexpected<Error>{Error{
            .code = ErrorCode::InvalidParameters,
            .message = "TorchTRT session expects input at " + std::to_string(fixed_resolution) +
                       "x" + std::to_string(fixed_resolution) + "; got " + std::to_string(width) +
                       "x" + std::to_string(height)}};
    }
    if (alpha_hint.width != width || alpha_hint.height != height || alpha_hint.channels != 1) {
        return Unexpected<Error>{Error{.code = ErrorCode::InvalidParameters,
                                       .message = "TorchScript RTX session expects alpha_hint at " +
                                                  std::to_string(width) + "x" +
                                                  std::to_string(height) + "x1"}};
    }

    try {
        const DynamicPadding padding = dynamic_padding_for_input(width, height, dynamic_resolution);
        const int inference_width = padding.width;
        const int inference_height = padding.height;
        torch::Tensor host_input;
        common::measure_stage(on_stage, "torchtrt_prepare_pack", [&]() {
            host_input = allocate_host_input_tensor(inference_height, inference_width);
            const float* rgb_data = rgb.data.data();
            const float* hint_data = alpha_hint.data.data();
            auto* dst = host_input.data_ptr<float>();
            const auto inference_plane =
                static_cast<std::ptrdiff_t>(inference_width) * inference_height;
            for (int row = 0; row < inference_height; ++row) {
                const int src_y = reflect_index(row - padding.top, height);
                for (int column = 0; column < inference_width; ++column) {
                    const int src_x = reflect_index(column - padding.left, width);
                    const auto dst_index =
                        (static_cast<std::ptrdiff_t>(row) * inference_width) + column;
                    const auto reflected_index =
                        (static_cast<std::ptrdiff_t>(src_y) * width) + src_x;
                    dst[(0 * inference_plane) + dst_index] = normalize_corridorkey_rgb(
                        rgb_data[(reflected_index * 3) + 0], ModelRgbChannel::Red);
                    dst[(1 * inference_plane) + dst_index] = normalize_corridorkey_rgb(
                        rgb_data[(reflected_index * 3) + 1], ModelRgbChannel::Green);
                    dst[(2 * inference_plane) + dst_index] = normalize_corridorkey_rgb(
                        rgb_data[(reflected_index * 3) + 2], ModelRgbChannel::Blue);
                    dst[(3 * inference_plane) + dst_index] = hint_data[reflected_index];
                }
            }
        });
        torch::Tensor cuda_input;
        common::measure_stage(on_stage, "torchtrt_prepare_upload", [&]() {
            constexpr bool kNonBlockingCopy = true;
            cuda_input =
                host_input.to(torch::Device(torch::kCUDA), m_impl->input_dtype, kNonBlockingCopy);
        });

        return forward_and_materialize(m_impl->module, cuda_input, width, height, inference_width,
                                       inference_height, padding.top, padding.left,
                                       output_alpha_only, on_stage);
    } catch (const c10::Error& e) {
        return Unexpected<Error>{
            Error{.code = ErrorCode::InferenceFailed,
                  .message = std::string("TorchTRT forward c10 error: ") + e.what()}};
    } catch (const std::exception& e) {
        return Unexpected<Error>{
            Error{.code = ErrorCode::InferenceFailed,
                  .message = std::string("TorchTRT forward std::exception: ") + e.what()}};
    }
}

Result<FrameResult> TorchTrtSession::infer_prepared_planar(
    const float* planar_input, int input_width, int input_height, bool output_alpha_only,
    // NOLINTNEXTLINE(performance-unnecessary-value-param)
    StageTimingCallback on_stage) {
    if (m_impl == nullptr) {
        return Unexpected<Error>{Error{.code = ErrorCode::InferenceFailed,
                                       .message = "TorchTrtSession in moved-from state"}};
    }
    if (planar_input == nullptr || input_width <= 0 || input_height <= 0) {
        return Unexpected<Error>{
            Error{.code = ErrorCode::InvalidParameters,
                  .message = "Prepared TorchTRT input must be a non-empty planar tensor"}};
    }

    try {
        torch::Tensor host_input;
        common::measure_stage(on_stage, "torchtrt_prepare_planar_copy", [&]() {
            host_input = allocate_host_input_tensor(input_height, input_width);
            const auto input_count = static_cast<std::size_t>(4) *
                                     static_cast<std::size_t>(input_width) *
                                     static_cast<std::size_t>(input_height);
            std::memcpy(host_input.data_ptr<float>(), planar_input, input_count * sizeof(float));
        });

        torch::Tensor cuda_input;
        common::measure_stage(on_stage, "torchtrt_prepare_upload", [&]() {
            constexpr bool kNonBlockingCopy = true;
            cuda_input =
                host_input.to(torch::Device(torch::kCUDA), m_impl->input_dtype, kNonBlockingCopy);
        });

        return forward_and_materialize(m_impl->module, cuda_input, input_width, input_height,
                                       input_width, input_height, 0, 0, output_alpha_only,
                                       on_stage);
    } catch (const c10::Error& e) {
        return Unexpected<Error>{
            Error{.code = ErrorCode::InferenceFailed,
                  .message = std::string("TorchTRT prepared forward c10 error: ") + e.what()}};
    } catch (const std::exception& e) {
        return Unexpected<Error>{
            Error{.code = ErrorCode::InferenceFailed,
                  .message = std::string("TorchTRT prepared forward std::exception: ") + e.what()}};
    }
}

Result<FrameResult> TorchTrtSession::infer_prepared_cuda_planar(
    void* planar_device_input, int input_width, int input_height, bool output_alpha_only,
    // NOLINTNEXTLINE(performance-unnecessary-value-param)
    StageTimingCallback on_stage) {
    if (m_impl == nullptr) {
        return Unexpected<Error>{Error{.code = ErrorCode::InferenceFailed,
                                       .message = "TorchTrtSession in moved-from state"}};
    }
    if (planar_device_input == nullptr || input_width <= 0 || input_height <= 0) {
        return Unexpected<Error>{
            Error{.code = ErrorCode::InvalidParameters,
                  .message = "Prepared CUDA TorchTRT input must be a non-empty planar tensor"}};
    }

    try {
        torch::Tensor cuda_input;
        common::measure_stage(on_stage, "torchtrt_prepare_device_wrap", [&]() {
            auto options = torch::TensorOptions().dtype(torch::kFloat32).device(torch::kCUDA);
            cuda_input =
                torch::from_blob(planar_device_input, {1, 4, input_height, input_width}, options);
        });

        if (m_impl->input_dtype != torch::kFloat32) {
            common::measure_stage(on_stage, "torchtrt_prepare_device_cast",
                                  [&]() { cuda_input = cuda_input.to(m_impl->input_dtype); });
        }

        return forward_and_materialize(m_impl->module, cuda_input, input_width, input_height,
                                       input_width, input_height, 0, 0, output_alpha_only,
                                       on_stage);
    } catch (const c10::Error& e) {
        return Unexpected<Error>{
            Error{.code = ErrorCode::InferenceFailed,
                  .message = std::string("TorchTRT prepared CUDA forward c10 error: ") + e.what()}};
    } catch (const std::exception& e) {
        return Unexpected<Error>{Error{
            .code = ErrorCode::InferenceFailed,
            .message = std::string("TorchTRT prepared CUDA forward std::exception: ") + e.what()}};
    }
}

}  // namespace corridorkey::core
