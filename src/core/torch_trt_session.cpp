#include "torch_trt_session.hpp"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <memory>
#include <optional>
#include <regex>
#include <string>
#include <utility>

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

ImageBuffer materialize_alpha(const torch::Tensor& alpha_cuda, int output_width, int output_height,
                              int tensor_width, int pad_top, int pad_left) {
    auto alpha_cpu = alpha_cuda.detach().to(torch::kCPU).to(torch::kFloat32).contiguous();
    ImageBuffer buf(output_width, output_height, 1);
    auto view = buf.view();
    const auto* src = alpha_cpu.data_ptr<float>();
    auto* dst = view.data.data();
    const auto row_bytes = static_cast<std::size_t>(output_width) * sizeof(float);
    for (int y = 0; y < output_height; ++y) {
        std::memcpy(dst + (static_cast<std::ptrdiff_t>(y) * output_width),
                    src + (static_cast<std::ptrdiff_t>(y + pad_top) * tensor_width) + pad_left,
                    row_bytes);
    }
    return buf;
}

ImageBuffer materialize_rgb(const torch::Tensor& fg_cuda, int output_width, int output_height,
                            int tensor_width, int tensor_height, int pad_top, int pad_left) {
    auto fg_cpu = fg_cuda.detach().to(torch::kCPU).to(torch::kFloat32).contiguous();
    ImageBuffer buf(output_width, output_height, 3);
    auto view = buf.view();
    const auto* src = fg_cpu.data_ptr<float>();
    const auto plane = static_cast<std::ptrdiff_t>(tensor_width) * tensor_height;
    auto* dst = view.data.data();
    for (int y = 0; y < output_height; ++y) {
        for (int x = 0; x < output_width; ++x) {
            const auto src_index =
                (static_cast<std::ptrdiff_t>(y + pad_top) * tensor_width) + x + pad_left;
            const auto dst_index =
                ((static_cast<std::ptrdiff_t>(y) * output_width) + x) * 3;
            dst[dst_index + 0] = src[src_index];
            dst[dst_index + 1] = src[plane + src_index];
            dst[dst_index + 2] = src[(2 * plane) + src_index];
        }
    }
    return buf;
}

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
    StageTimingCallback on_stage) {  // NOLINT(performance-unnecessary-value-param) — matches MlxSession signature.
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
                       std::to_string(rgb.width) + "x" +
                       std::to_string(rgb.height) + "x" + std::to_string(rgb.channels)}};
    }
    if (!dynamic_resolution && (width != fixed_resolution || height != fixed_resolution)) {
        return Unexpected<Error>{
            Error{.code = ErrorCode::InvalidParameters,
                  .message = "TorchTRT session expects input at " +
                             std::to_string(fixed_resolution) + "x" +
                             std::to_string(fixed_resolution) + "; got " +
                             std::to_string(width) + "x" + std::to_string(height)}};
    }
    if (alpha_hint.width != width || alpha_hint.height != height || alpha_hint.channels != 1) {
        return Unexpected<Error>{
            Error{.code = ErrorCode::InvalidParameters,
                  .message = "TorchScript RTX session expects alpha_hint at " +
                             std::to_string(width) + "x" + std::to_string(height) + "x1"}};
    }

    try {
        const DynamicPadding padding = dynamic_padding_for_input(width, height, dynamic_resolution);
        const int inference_width = padding.width;
        const int inference_height = padding.height;
        // Pack RGB + hint into (1, 4, H, W) channel-first before CUDA upload.
        // RGB is interleaved (R,G,B,R,G,B,...) on host; we convert to planar.
        auto host_input = torch::empty({1, 4, inference_height, inference_width}, torch::kFloat32);
        const float* rgb_data = rgb.data.data();
        const float* hint_data = alpha_hint.data.data();
        auto* dst = host_input.data_ptr<float>();
        const auto inference_plane =
            static_cast<std::ptrdiff_t>(inference_width) * inference_height;
        for (int y = 0; y < inference_height; ++y) {
            const int src_y = reflect_index(y - padding.top, height);
            for (int x = 0; x < inference_width; ++x) {
                const int src_x = reflect_index(x - padding.left, width);
                const auto dst_index = (static_cast<std::ptrdiff_t>(y) * inference_width) + x;
                const auto reflected_index = (static_cast<std::ptrdiff_t>(src_y) * width) + src_x;
                dst[(0 * inference_plane) + dst_index] = rgb_data[(reflected_index * 3) + 0];
                dst[(1 * inference_plane) + dst_index] = rgb_data[(reflected_index * 3) + 1];
                dst[(2 * inference_plane) + dst_index] = rgb_data[(reflected_index * 3) + 2];
                dst[(3 * inference_plane) + dst_index] = hint_data[reflected_index];
            }
        }
        auto cuda_input = host_input.to(torch::Device(torch::kCUDA), m_impl->input_dtype);

        torch::IValue raw_out;
        common::measure_stage(on_stage, "torchtrt_forward", [&]() {
            const torch::NoGradGuard no_grad;
            raw_out = m_impl->module.forward({cuda_input});
            torch::cuda::synchronize();
        });

        auto split = split_forward_output(raw_out);
        if (!split.has_value() || !split->alpha.defined()) {
            return Unexpected<Error>{
                Error{.code = ErrorCode::InferenceFailed,
                      .message = "TorchTRT forward returned no usable alpha tensor"}};
        }
        if (!tensor_has_shape(split->alpha, 1, inference_width, inference_height)) {
            return Unexpected<Error>{
                Error{.code = ErrorCode::InferenceFailed,
                      .message = "TorchScript RTX alpha output shape did not match input " +
                                 std::to_string(inference_width) + "x" +
                                 std::to_string(inference_height)}};
        }
        if (!output_alpha_only && split->foreground.defined() &&
            !tensor_has_shape(split->foreground, 3, inference_width, inference_height)) {
            return Unexpected<Error>{
                Error{.code = ErrorCode::InferenceFailed,
                      .message = "TorchScript RTX foreground output shape did not match input " +
                                 std::to_string(inference_width) + "x" +
                                 std::to_string(inference_height)}};
        }

        FrameResult result;
        common::measure_stage(on_stage, "torchtrt_extract_alpha", [&]() {
            result.alpha = materialize_alpha(split->alpha, width, height, inference_width,
                                             padding.top, padding.left);
        });

        if (!output_alpha_only && split->foreground.defined()) {
            common::measure_stage(on_stage, "torchtrt_extract_foreground", [&]() {
                result.foreground =
                    materialize_rgb(split->foreground, width, height, inference_width,
                                    inference_height, padding.top, padding.left);
            });
        }
        return result;
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

}  // namespace corridorkey::core
