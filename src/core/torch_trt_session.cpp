#include "torch_trt_session.hpp"

#include <chrono>
#include <cstdio>
#include <regex>
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

#include <torch/cuda.h>
#include <torch/script.h>

#include "common/runtime_paths.hpp"
#include "common/stage_profiler.hpp"

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
        return std::stoi(match[1].str());
    } catch (...) {
        return std::nullopt;
    }
}

#ifdef _WIN32
// Strategy C delayed-load entry. The TorchTRT runtime DLLs ship inside the
// blue model pack, not in the base Windows RTX bundle. At session creation
// we point the OS loader at the pack's runtime dir so torchtrt.dll's
// transitive deps (libtorch + cuDNN + nvinfer) resolve from there.
//
// Lookup order:
//   1. CORRIDORKEY_TORCHTRT_RUNTIME_DIR env override (dev / CI)
//   2. <ts file dir>/../torchtrt-runtime/bin (blue pack layout, PR 4)
//   3. <repo>/vendor/torchtrt-windows/bin (dev fallback when running from
//      the build tree before a pack is assembled)
std::optional<std::filesystem::path> resolve_torchtrt_runtime_bin(
    const std::filesystem::path& ts_path) {
    if (const char* env = std::getenv("CORRIDORKEY_TORCHTRT_RUNTIME_DIR")) {
        std::filesystem::path candidate{env};
        if (std::filesystem::exists(candidate / "torchtrt.dll")) {
            return candidate;
        }
    }
    // Absolutise first: relative paths (e.g. "models/foo.ts") yield an
    // empty parent_path() chain after one or two steps, breaking the walk
    // loop before it reaches the repo root or any blue-pack-shaped layout.
    std::error_code ec;
    auto absolute_ts = std::filesystem::absolute(ts_path, ec);
    if (ec) absolute_ts = ts_path;

    auto pack_relative = absolute_ts.parent_path().parent_path() / "torchtrt-runtime" / "bin";
    if (std::filesystem::exists(pack_relative / "torchtrt.dll")) {
        return pack_relative;
    }
    // Walk a few levels up looking for vendor/torchtrt-windows/bin.
    auto walk = absolute_ts.parent_path();
    for (int i = 0; i < 8 && !walk.empty() && walk != walk.root_path(); ++i) {
        auto candidate = walk / "vendor" / "torchtrt-windows" / "bin";
        if (std::filesystem::exists(candidate / "torchtrt.dll")) {
            return candidate;
        }
        walk = walk.parent_path();
    }
    // Last try at the absolute root (covers `C:\<repo>` setups where the
    // walk above stops one level early).
    auto root_candidate = walk / "vendor" / "torchtrt-windows" / "bin";
    if (std::filesystem::exists(root_candidate / "torchtrt.dll")) {
        return root_candidate;
    }
    return std::nullopt;
}

bool g_torchtrt_runtime_armed = false;

bool arm_torchtrt_runtime(const std::filesystem::path& bin_dir, std::string& out_error) {
    if (g_torchtrt_runtime_armed) return true;
    const auto absolute = std::filesystem::absolute(bin_dir);
    auto cookie = AddDllDirectory(absolute.wstring().c_str());
    if (cookie == nullptr) {
        out_error = "AddDllDirectory failed for " + absolute.string() +
                    " (GetLastError=" + std::to_string(GetLastError()) + ")";
        return false;
    }
    SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS | LOAD_LIBRARY_SEARCH_USER_DIRS);
    const auto torchtrt_path = (absolute / L"torchtrt.dll").wstring();
    HMODULE handle = LoadLibraryExW(torchtrt_path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (handle == nullptr) {
        out_error =
            "LoadLibrary torchtrt.dll failed (GetLastError=" + std::to_string(GetLastError()) + ")";
        return false;
    }
    g_torchtrt_runtime_armed = true;
    return true;
}
#endif  // _WIN32

ImageBuffer materialize_alpha(const torch::Tensor& alpha_cuda, int resolution) {
    auto alpha_cpu = alpha_cuda.detach().to(torch::kCPU).to(torch::kFloat32).contiguous();
    ImageBuffer buf(resolution, resolution, 1);
    auto v = buf.view();
    std::memcpy(v.data.data(), alpha_cpu.data_ptr<float>(), v.data.size_bytes());
    return buf;
}

ImageBuffer materialize_rgb(const torch::Tensor& fg_cuda, int resolution) {
    // fg_cuda is (1, 3, R, R) channel-first; convert to (R, R, 3) interleaved.
    auto fg_cpu = fg_cuda.detach().to(torch::kCPU).to(torch::kFloat32).contiguous();
    auto fg_hwc = fg_cpu.permute({0, 2, 3, 1}).contiguous();
    ImageBuffer buf(resolution, resolution, 3);
    auto v = buf.view();
    std::memcpy(v.data.data(), fg_hwc.data_ptr<float>(), v.data.size_bytes());
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
    const std::filesystem::path& ts_path, const DeviceInfo& device, StageTimingCallback on_stage) {
    if (!std::filesystem::exists(ts_path)) {
        return Unexpected<Error>{
            Error{ErrorCode::ModelLoadFailed, "TorchTRT engine not found: " + ts_path.string()}};
    }

#ifdef _WIN32
    auto runtime_bin = resolve_torchtrt_runtime_bin(ts_path);
    if (!runtime_bin.has_value()) {
        return Unexpected<Error>{
            Error{ErrorCode::HardwareNotSupported,
                  "TorchTRT runtime DLLs not found. Set CORRIDORKEY_TORCHTRT_RUNTIME_DIR or "
                  "stage the blue model pack runtime alongside the .ts."}};
    }
    std::string arm_error;
    if (!arm_torchtrt_runtime(*runtime_bin, arm_error)) {
        return Unexpected<Error>{
            Error{ErrorCode::HardwareNotSupported, "TorchTRT runtime arm failed: " + arm_error}};
    }
#endif

    if (!torch::cuda::is_available()) {
        return Unexpected<Error>{
            Error{ErrorCode::HardwareNotSupported,
                  "CUDA not available; TorchTRT engines require an Ampere or newer GPU."}};
    }

    auto session = std::unique_ptr<TorchTrtSession>(new TorchTrtSession());
    session->m_impl->device = device;

    auto inferred_res = resolution_from_filename(ts_path);
    if (!inferred_res.has_value()) {
        return Unexpected<Error>{Error{
            ErrorCode::ModelLoadFailed,
            "Could not infer model resolution from .ts filename: " + ts_path.filename().string()}};
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
            Error{ErrorCode::ModelLoadFailed, std::string("torch::jit::load failed: ") + e.what()}};
    } catch (const std::exception& e) {
        return Unexpected<Error>{
            Error{ErrorCode::ModelLoadFailed,
                  std::string("torch::jit::load std::exception: ") + e.what()}};
    }

    return session;
}

int TorchTrtSession::model_resolution() const {
    return m_impl ? m_impl->resolution : 0;
}

Result<FrameResult> TorchTrtSession::infer(const Image& rgb, const Image& alpha_hint,
                                           bool output_alpha_only, StageTimingCallback on_stage) {
    if (m_impl == nullptr) {
        return Unexpected<Error>{
            Error{ErrorCode::InferenceFailed, "TorchTrtSession in moved-from state"}};
    }
    const int resolution = m_impl->resolution;
    if (rgb.width != resolution || rgb.height != resolution || rgb.channels != 3) {
        return Unexpected<Error>{
            Error{ErrorCode::InvalidParameters,
                  "TorchTRT session expects RGB input at " + std::to_string(resolution) + "x" +
                      std::to_string(resolution) + "x3; got " + std::to_string(rgb.width) + "x" +
                      std::to_string(rgb.height) + "x" + std::to_string(rgb.channels)}};
    }
    if (alpha_hint.width != resolution || alpha_hint.height != resolution ||
        alpha_hint.channels != 1) {
        return Unexpected<Error>{Error{ErrorCode::InvalidParameters,
                                       "TorchTRT session expects single-channel alpha_hint at " +
                                           std::to_string(resolution) + "x" +
                                           std::to_string(resolution) + "x1"}};
    }

    try {
        // Pack RGB + hint into (1, 4, R, R) channel-first FP16 on CUDA.
        // RGB is interleaved (R,G,B,R,G,B,...) on host; we convert to planar.
        auto host_input = torch::empty({1, 4, resolution, resolution}, torch::kFloat32);
        const float* rgb_data = rgb.data.data();
        const float* hint_data = alpha_hint.data.data();
        float* dst = host_input.data_ptr<float>();
        const int64_t plane = static_cast<int64_t>(resolution) * resolution;
        for (int64_t i = 0; i < plane; ++i) {
            dst[0 * plane + i] = rgb_data[i * 3 + 0];
            dst[1 * plane + i] = rgb_data[i * 3 + 1];
            dst[2 * plane + i] = rgb_data[i * 3 + 2];
            dst[3 * plane + i] = hint_data[i];
        }
        auto cuda_input = host_input.to(torch::Device(torch::kCUDA), m_impl->input_dtype);

        torch::IValue raw_out;
        common::measure_stage(on_stage, "torchtrt_forward", [&]() {
            torch::NoGradGuard no_grad;
            raw_out = m_impl->module.forward({cuda_input});
            torch::cuda::synchronize();
        });

        torch::Tensor alpha_tensor;
        torch::Tensor fg_tensor;
        if (raw_out.isTuple()) {
            const auto elements = raw_out.toTuple()->elements();
            if (elements.empty()) {
                return Unexpected<Error>{
                    Error{ErrorCode::InferenceFailed, "TorchTRT forward returned empty tuple"}};
            }
            alpha_tensor = elements[0].toTensor();
            if (elements.size() > 1) {
                fg_tensor = elements[1].toTensor();
            }
        } else if (raw_out.isTensor()) {
            alpha_tensor = raw_out.toTensor();
        } else {
            return Unexpected<Error>{
                Error{ErrorCode::InferenceFailed, "Unexpected TorchTRT forward return type"}};
        }

        FrameResult result;
        common::measure_stage(on_stage, "torchtrt_extract_alpha", [&]() {
            result.alpha = materialize_alpha(alpha_tensor, resolution);
        });

        if (!output_alpha_only && fg_tensor.defined()) {
            common::measure_stage(on_stage, "torchtrt_extract_foreground", [&]() {
                result.foreground = materialize_rgb(fg_tensor, resolution);
            });
        }
        return result;
    } catch (const c10::Error& e) {
        return Unexpected<Error>{Error{ErrorCode::InferenceFailed,
                                       std::string("TorchTRT forward c10 error: ") + e.what()}};
    } catch (const std::exception& e) {
        return Unexpected<Error>{
            Error{ErrorCode::InferenceFailed,
                  std::string("TorchTRT forward std::exception: ") + e.what()}};
    }
}

}  // namespace corridorkey::core
