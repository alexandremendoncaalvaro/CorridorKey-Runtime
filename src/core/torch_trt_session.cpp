#include "torch_trt_session.hpp"

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
    // corridorkey_torchtrt_fp16_<res>.ts | corridorkey_blue_torchtrt_fp16_<res>.ts
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

ImageBuffer materialize_alpha(const torch::Tensor& alpha_cuda, int resolution) {
    auto alpha_cpu = alpha_cuda.detach().to(torch::kCPU).to(torch::kFloat32).contiguous();
    ImageBuffer buf(resolution, resolution, 1);
    auto view = buf.view();
    std::memcpy(view.data.data(), alpha_cpu.data_ptr<float>(), view.data.size_bytes());
    return buf;
}

ImageBuffer materialize_rgb(const torch::Tensor& fg_cuda, int resolution) {
    // fg_cuda is (1, 3, R, R) channel-first; convert to (R, R, 3) interleaved.
    auto fg_cpu = fg_cuda.detach().to(torch::kCPU).to(torch::kFloat32).contiguous();
    auto fg_hwc = fg_cpu.permute({0, 2, 3, 1}).contiguous();
    ImageBuffer buf(resolution, resolution, 3);
    auto view = buf.view();
    std::memcpy(view.data.data(), fg_hwc.data_ptr<float>(), view.data.size_bytes());
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
    if (!inferred_res.has_value()) {
        return Unexpected<Error>{
            Error{.code = ErrorCode::ModelLoadFailed,
                  .message = "Could not infer model resolution from .ts filename: " +
                             ts_path.filename().string()}};
    }
    session->m_impl->resolution = *inferred_res;
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
    const int resolution = m_impl->resolution;
    if (rgb.width != resolution || rgb.height != resolution || rgb.channels != 3) {
        return Unexpected<Error>{Error{
            .code = ErrorCode::InvalidParameters,
            .message = "TorchTRT session expects RGB input at " + std::to_string(resolution) + "x" +
                       std::to_string(resolution) + "x3; got " + std::to_string(rgb.width) + "x" +
                       std::to_string(rgb.height) + "x" + std::to_string(rgb.channels)}};
    }
    if (alpha_hint.width != resolution || alpha_hint.height != resolution ||
        alpha_hint.channels != 1) {
        return Unexpected<Error>{
            Error{.code = ErrorCode::InvalidParameters,
                  .message = "TorchTRT session expects single-channel alpha_hint at " +
                             std::to_string(resolution) + "x" + std::to_string(resolution) + "x1"}};
    }

    try {
        // Pack RGB + hint into (1, 4, R, R) channel-first FP16 on CUDA.
        // RGB is interleaved (R,G,B,R,G,B,...) on host; we convert to planar.
        auto host_input = torch::empty({1, 4, resolution, resolution}, torch::kFloat32);
        const float* rgb_data = rgb.data.data();
        const float* hint_data = alpha_hint.data.data();
        auto* dst = host_input.data_ptr<float>();
        const auto plane = static_cast<int64_t>(resolution) * resolution;
        for (int64_t i = 0; i < plane; ++i) {
            dst[(0 * plane) + i] = rgb_data[(i * 3) + 0];
            dst[(1 * plane) + i] = rgb_data[(i * 3) + 1];
            dst[(2 * plane) + i] = rgb_data[(i * 3) + 2];
            dst[(3 * plane) + i] = hint_data[i];
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

        FrameResult result;
        common::measure_stage(on_stage, "torchtrt_extract_alpha", [&]() {
            result.alpha = materialize_alpha(split->alpha, resolution);
        });

        if (!output_alpha_only && split->foreground.defined()) {
            common::measure_stage(on_stage, "torchtrt_extract_foreground", [&]() {
                result.foreground = materialize_rgb(split->foreground, resolution);
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
