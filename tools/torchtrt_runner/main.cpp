// Standalone smoke runner for vendor/torchtrt-windows.
//
// Validates the AddDllDirectory + LoadLibrary + torch::jit::load chain
// that PR 3 will adopt inside src/core/torch_trt_session.cpp. Intentionally
// not linked into the main runtime - this is tools/ scratch (per
// docs/ARCHITECTURE.md section "tools/") that proves the C++ path works
// on the operator's machine before the heavier integration lands.
//
// Build: gated on CORRIDORKEY_HAS_TORCHTRT in the root CMakeLists.txt.
// Run  : corridorkey-torchtrt-runner --ts <path-to-corridorkey_torchtrt_fp16_<res>.ts>
//        corridorkey-torchtrt-runner --ts <path-to-corridorkey_torchtrt_fp32_<res>.ts>
//        [--resolution <n>] [--bin-dir <path>] [--iterations <n>]
//
// On success prints:
//   [OK] forward avg=<ms>ms alpha=[<min>,<max>]  no NaN/Inf
// Non-zero exit on any failure.

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <functional>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

#include <ATen/ops/upsample_bicubic2d.h>
#include <torch/cuda.h>
#include <torch/script.h>

#include <nlohmann/json.hpp>

namespace {

constexpr int kDefaultIterations = 5;
constexpr int kDefaultWarmup = 2;
constexpr int kSyntheticInputChannels = 4;
constexpr int kRgbChannels = 3;
constexpr int kRandomSeedBase = 42;
constexpr int kExitArgs = 2;
constexpr int kExitDllStaging = 3;
constexpr int kExitTsMissing = 4;
constexpr int kExitNoCuda = 5;
constexpr int kExitJitLoad = 6;
constexpr int kExitWarmup = 7;
constexpr int kExitForward = 8;
constexpr int kExitForwardOutput = 9;
constexpr int kExitNanOrInf = 10;
constexpr std::string_view kExternalPosMetaName = "corridorkey.external_pos.v1.json";
constexpr std::string_view kExternalPosDataName = "corridorkey.external_pos.v1.fp32";

struct Options {
    std::filesystem::path ts_path;
    std::filesystem::path bin_dir;
    int resolution = 0;  // auto-detect from filename if 0
    int iterations = kDefaultIterations;
    int warmup = kDefaultWarmup;
};

bool parse_int(std::string_view text, int& out) {
    try {
        out = std::stoi(std::string{text});
        return true;
    } catch (...) {
        return false;
    }
}

void log_error(std::string_view message) {
    (void)std::fputs("[ERR] ", stderr);
    (void)std::fputs(std::string{message}.c_str(), stderr);
    (void)std::fputc('\n', stderr);
}

void log_ok(std::string_view message) {
    (void)std::fputs("[ok] ", stdout);
    (void)std::fputs(std::string{message}.c_str(), stdout);
    (void)std::fputc('\n', stdout);
}

struct ExternalPosState {
    bool enabled = false;
    int channels = 0;
    int source_grid = 0;
    int patch_stride_height = 0;
    int patch_stride_width = 0;
    torch::Tensor base_cuda_float;
};

bool json_int_at(const nlohmann::json& array, std::size_t index, int& value) {
    if (!array.is_array() || index >= array.size() || !array.at(index).is_number_integer()) {
        return false;
    }
    value = array.at(index).get<int>();
    return true;
}

bool parse_external_pos_state(const std::unordered_map<std::string, std::string>& extra_files,
                              ExternalPosState& state) {
    const auto meta_iter = extra_files.find(std::string{kExternalPosMetaName});
    const auto data_iter = extra_files.find(std::string{kExternalPosDataName});
    const bool has_meta = meta_iter != extra_files.end() && !meta_iter->second.empty();
    const bool has_data = data_iter != extra_files.end() && !data_iter->second.empty();
    if (!has_meta && !has_data) {
        return true;
    }
    if (!has_meta || !has_data) {
        log_error("external positional embedding metadata is incomplete");
        return false;
    }

    const auto metadata = nlohmann::json::parse(meta_iter->second, nullptr, false);
    if (metadata.is_discarded() ||
        metadata.value("format", "") != "corridorkey_torchtrt_external_pos") {
        log_error("external positional embedding metadata is invalid");
        return false;
    }

    const auto shape = metadata.value("shape", nlohmann::json::array());
    int batch = 0;
    int channels = 0;
    int source_height = 0;
    int source_width = 0;
    if (!json_int_at(shape, 0, batch) || !json_int_at(shape, 1, channels) ||
        !json_int_at(shape, 2, source_height) || !json_int_at(shape, 3, source_width) ||
        batch != 1 || channels <= 0 || source_height <= 0 || source_height != source_width) {
        log_error("external positional embedding shape is invalid");
        return false;
    }

    const auto stride = metadata.value("patch_stride", nlohmann::json::array());
    int patch_stride_height = 0;
    int patch_stride_width = 0;
    if (!json_int_at(stride, 0, patch_stride_height) ||
        !json_int_at(stride, 1, patch_stride_width) || patch_stride_height <= 0 ||
        patch_stride_width <= 0) {
        log_error("external positional embedding stride is invalid");
        return false;
    }

    const auto expected_bytes =
        static_cast<std::size_t>(batch) * channels * source_height * source_width * sizeof(float);
    if (data_iter->second.size() != expected_bytes) {
        log_error("external positional embedding payload size is invalid");
        return false;
    }

    const auto element_count = expected_bytes / sizeof(float);
    std::vector<float> values(element_count);
    std::memcpy(values.data(), data_iter->second.data(), expected_bytes);
    auto base_cpu =
        torch::from_blob(values.data(), {1, channels, source_height, source_width},
                         torch::TensorOptions().dtype(torch::kFloat32).device(torch::kCPU))
            .clone();

    state.enabled = true;
    state.channels = channels;
    state.source_grid = source_height;
    state.patch_stride_height = patch_stride_height;
    state.patch_stride_width = patch_stride_width;
    state.base_cuda_float = base_cpu.to(torch::Device(torch::kCUDA), torch::kFloat32);
    return true;
}

// Hand-rolled key/value arg parser. Per-flag dispatch is table-driven so
// adding a new flag is one row and the parse_args body stays under the
// cognitive-complexity threshold.
using ArgHandler = std::function<bool(std::string_view value)>;

std::vector<std::pair<std::string_view, ArgHandler>> build_arg_handlers(Options& options) {
    auto require_value = [](std::string_view flag, std::string_view value) {
        if (value.empty()) {
            log_error(std::string{flag} + " needs a value");
            return false;
        }
        return true;
    };
    return {
        {"--ts",
         [&options, require_value](std::string_view value) {
             if (!require_value("--ts", value)) return false;
             options.ts_path = std::string{value};
             return true;
         }},
        {"--bin-dir",
         [&options, require_value](std::string_view value) {
             if (!require_value("--bin-dir", value)) return false;
             options.bin_dir = std::string{value};
             return true;
         }},
        {"--resolution",
         [&options, require_value](std::string_view value) {
             return require_value("--resolution", value) && parse_int(value, options.resolution);
         }},
        {"--iterations",
         [&options, require_value](std::string_view value) {
             return require_value("--iterations", value) && parse_int(value, options.iterations);
         }},
        {"--warmup",
         [&options, require_value](std::string_view value) {
             return require_value("--warmup", value) && parse_int(value, options.warmup);
         }},
    };
}

bool dispatch_arg(std::span<char*> args, std::size_t& index,
                  const std::vector<std::pair<std::string_view, ArgHandler>>& handlers) {
    const std::string_view token{args.subspan(index, 1).front()};
    const auto found =
        std::ranges::find_if(handlers, [&](const auto& pair) { return pair.first == token; });
    if (found == handlers.end()) {
        log_error("unknown arg: " + std::string{token});
        return false;
    }
    const auto next_index = index + 1;
    const std::string_view value = next_index < args.size()
                                       ? std::string_view{args.subspan(next_index, 1).front()}
                                       : std::string_view{};
    if (!found->second(value)) {
        return false;
    }
    ++index;
    return true;
}

bool parse_args(std::span<char*> args, Options& options) {
    const auto handlers = build_arg_handlers(options);
    for (std::size_t index = 1; index < args.size(); ++index) {
        const std::string_view token{args.subspan(index, 1).front()};
        if (token == "--help" || token == "-h") {
            (void)std::puts(
                "Usage: corridorkey-torchtrt-runner --ts <path> "
                "[--resolution N] [--bin-dir DIR] [--iterations N] [--warmup N]");
            return false;
        }
        if (!dispatch_arg(args, index, handlers)) {
            return false;
        }
    }
    if (options.ts_path.empty()) {
        log_error("--ts is required");
        return false;
    }
    return true;
}

int infer_resolution_from_filename(const std::filesystem::path& path) {
    // corridorkey_torchtrt_fp16_<res>.ts  -> <res>
    const auto stem = path.stem().string();
    const auto separator = stem.find_last_of('_');
    if (separator == std::string::npos) {
        return 0;
    }
    int resolution = 0;
    if (!parse_int(stem.substr(separator + 1), resolution)) {
        return 0;
    }
    return resolution;
}

#ifdef _WIN32
bool stage_dll_directory(const std::filesystem::path& bin_dir) {
    if (bin_dir.empty()) {
        return true;
    }
    if (!std::filesystem::exists(bin_dir)) {
        log_error("--bin-dir does not exist: " + bin_dir.string());
        return false;
    }
    // AddDllDirectory needs absolute wide path. The OS loader will then
    // search this directory when LoadLibraryEx-class calls request a DLL
    // by short name. This is the same hook src/core/torch_trt_session.cpp
    // uses from the blue model pack's runtime location.
    const auto absolute = std::filesystem::absolute(bin_dir);
    auto* cookie = AddDllDirectory(absolute.wstring().c_str());
    if (cookie == nullptr) {
        log_error("AddDllDirectory failed (GetLastError=" + std::to_string(GetLastError()) + ")");
        return false;
    }
    SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS | LOAD_LIBRARY_SEARCH_USER_DIRS);

    // Force-load torchtrt.dll by absolute path so its static initializers
    // register the TorchScript custom ops the .ts engines reference.
    // SetDefaultDllDirectories above narrows the loader to AddDllDirectory'd
    // dirs, which excludes the exe directory; the absolute path side-steps
    // that restriction for this one explicit load while still letting
    // torchtrt.dll's transitive deps resolve from the AddDllDirectory'd
    // vendor/bin.
    const auto torchtrt_path = (absolute / L"torchtrt.dll").wstring();
    auto* handle = LoadLibraryExW(torchtrt_path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (handle == nullptr) {
        log_error("LoadLibrary torchtrt.dll failed (GetLastError=" +
                  std::to_string(GetLastError()) + ")");
        return false;
    }
    log_ok("AddDllDirectory + LoadLibrary torchtrt.dll succeeded");
    return true;
}
#else
bool stage_dll_directory(const std::filesystem::path& /*bin_dir*/) {
    return true;
}
#endif

std::filesystem::path default_bin_dir_relative_to_exe() {
    std::filesystem::path exe_dir;
#ifdef _WIN32
    std::array<wchar_t, MAX_PATH> buf{};
    const DWORD count = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
    if (count > 0 && count < buf.size()) {
        exe_dir = std::filesystem::path(buf.data()).parent_path();
    }
#endif
    if (exe_dir.empty()) {
        return {};
    }
    // build/<preset>/tools/torchtrt_runner/<exe> -> ../../../vendor/torchtrt-windows/bin
    return exe_dir / ".." / ".." / ".." / "vendor" / "torchtrt-windows" / "bin";
}

torch::Tensor make_synthetic_input(int resolution, torch::Dtype dtype, std::mt19937& rng) {
    std::uniform_real_distribution<float> dist(0.0F, 1.0F);
    std::vector<float> host(static_cast<std::size_t>(1) * kSyntheticInputChannels *
                            static_cast<std::size_t>(resolution) *
                            static_cast<std::size_t>(resolution));
    for (auto& value : host) {
        value = dist(rng);
    }
    auto cpu_input =
        torch::from_blob(host.data(), {1, kSyntheticInputChannels, resolution, resolution},
                         torch::kFloat32)
            .clone();
    return cpu_input.to(torch::TensorOptions().dtype(dtype).device(torch::kCUDA));
}

torch::Tensor make_pos_grid(const ExternalPosState& state, int resolution, torch::Dtype dtype) {
    if (!state.enabled) {
        return {};
    }
    const std::vector<int64_t> output_size{
        resolution / state.patch_stride_height,
        resolution / state.patch_stride_width,
    };
    auto grid = at::upsample_bicubic2d(state.base_cuda_float, output_size, false, std::nullopt,
                                       std::nullopt);
    if (dtype != torch::kFloat32) {
        grid = grid.to(dtype);
    }
    return grid;
}

}  // namespace

struct BenchOutcome {
    int exit_code = 0;
    double mean_ms = 0.0;
    double p50_ms = 0.0;
    double p99_ms = 0.0;
    float alpha_min = 0.0F;
    float alpha_max = 0.0F;
    bool has_nan = false;
    bool has_inf = false;
};

namespace {

int finalise_options(Options& options) {
    if (options.bin_dir.empty()) {
        // Default to the vendored runtime relative to this exe's location.
        // If the user moves the exe, they must pass --bin-dir explicitly.
        options.bin_dir = default_bin_dir_relative_to_exe();
    }

    if (options.resolution == 0) {
        options.resolution = infer_resolution_from_filename(options.ts_path);
        if (options.resolution == 0) {
            log_error("could not infer --resolution from filename " + options.ts_path.string() +
                      "; pass --resolution explicitly");
            return kExitArgs;
        }
        log_ok("inferred resolution=" + std::to_string(options.resolution) + " from filename");
    }
    return 0;
}

torch::Dtype infer_input_dtype_from_filename(const std::filesystem::path& path) {
    const auto stem = path.stem().string();
    if (stem.find("fp32") != std::string::npos) {
        return torch::kFloat32;
    }
    return torch::kFloat16;
}

torch::Tensor extract_alpha(const torch::IValue& output) {
    if (output.isTuple()) {
        return output.toTuple()->elements().at(0).toTensor();
    }
    if (output.isTensor()) {
        return output.toTensor();
    }
    return {};
}

struct BenchSchedule {
    int warmup_iterations = 0;
    int timed_iterations = 0;
};

torch::IValue forward_module(torch::jit::script::Module& module, const torch::Tensor& input,
                             const torch::Tensor& pos_grid) {
    if (pos_grid.defined()) {
        return module.forward({input, pos_grid});
    }
    return module.forward({input});
}

double percentile_latency(std::vector<double> latencies_ms, double percentile) {
    if (latencies_ms.empty()) {
        return 0.0;
    }
    std::ranges::sort(latencies_ms);
    const auto last_index = latencies_ms.size() - 1U;
    const auto rank = static_cast<std::size_t>(
        std::clamp(std::ceil((percentile / 100.0) * static_cast<double>(latencies_ms.size())), 1.0,
                   static_cast<double>(latencies_ms.size())) -
        1.0);
    return latencies_ms[std::min(rank, last_index)];
}

BenchOutcome run_bench(torch::jit::script::Module& module, const torch::Tensor& input,
                       const torch::Tensor& pos_grid, BenchSchedule schedule) {
    const int warmup = schedule.warmup_iterations;
    const int iterations = schedule.timed_iterations;
    BenchOutcome outcome;
    const torch::NoGradGuard no_grad;
    for (int iter = 0; iter < warmup; ++iter) {
        try {
            (void)forward_module(module, input, pos_grid);
        } catch (const c10::Error& e) {
            log_error(std::string("warmup forward failed: ") + e.what());
            outcome.exit_code = kExitWarmup;
            return outcome;
        }
    }
    torch::cuda::synchronize();

    std::vector<double> latencies_ms;
    latencies_ms.reserve(static_cast<std::size_t>(iterations));
    torch::Tensor last_alpha;
    for (int iter = 0; iter < iterations; ++iter) {
        const auto run_t0 = std::chrono::high_resolution_clock::now();
        torch::IValue out;
        try {
            out = forward_module(module, input, pos_grid);
        } catch (const c10::Error& e) {
            log_error(std::string("forward iter ") + std::to_string(iter) + " failed: " + e.what());
            outcome.exit_code = kExitForward;
            return outcome;
        }
        torch::cuda::synchronize();
        latencies_ms.push_back(std::chrono::duration<double, std::milli>(
                                   std::chrono::high_resolution_clock::now() - run_t0)
                                   .count());
        last_alpha = extract_alpha(out);
        if (!last_alpha.defined()) {
            log_error("unexpected forward return type");
            outcome.exit_code = kExitForwardOutput;
            return outcome;
        }
    }

    const auto alpha_cpu = last_alpha.detach().to(torch::kCPU).to(torch::kFloat32);
    outcome.alpha_min = alpha_cpu.min().item<float>();
    outcome.alpha_max = alpha_cpu.max().item<float>();
    outcome.has_nan = alpha_cpu.isnan().any().item<bool>();
    outcome.has_inf = alpha_cpu.isinf().any().item<bool>();

    double sum_ms = 0;
    for (const auto value : latencies_ms) {
        sum_ms += value;
    }
    outcome.mean_ms = sum_ms / static_cast<double>(latencies_ms.size());
    outcome.p50_ms = percentile_latency(latencies_ms, 50.0);
    outcome.p99_ms = percentile_latency(latencies_ms, 99.0);
    return outcome;
}

}  // namespace

int main(int argc, char** argv) {  // NOLINT(bugprone-exception-escape)
    Options options;
    const std::span<char*> args(argv, static_cast<std::size_t>(argc));
    if (!parse_args(args, options)) {
        return kExitArgs;
    }
    if (const int exit_code = finalise_options(options); exit_code != 0) {
        return exit_code;
    }
    if (!stage_dll_directory(options.bin_dir)) {
        return kExitDllStaging;
    }
    if (!std::filesystem::exists(options.ts_path)) {
        log_error("--ts not found: " + options.ts_path.string());
        return kExitTsMissing;
    }
    if (!torch::cuda::is_available()) {
        log_error("CUDA not available - this runner expects an Ampere or newer GPU");
        return kExitNoCuda;
    }

    torch::jit::script::Module module;
    std::unordered_map<std::string, std::string> extra_files{
        {std::string{kExternalPosMetaName}, {}},
        {std::string{kExternalPosDataName}, {}},
    };
    ExternalPosState external_pos;
    try {
        const auto load_t0 = std::chrono::high_resolution_clock::now();
        module =
            torch::jit::load(options.ts_path.string(), torch::Device(torch::kCUDA), extra_files);
        const auto load_ms = std::chrono::duration<double, std::milli>(
                                 std::chrono::high_resolution_clock::now() - load_t0)
                                 .count();
        log_ok("torch::jit::load " + std::to_string(load_ms) + " ms");
    } catch (const c10::Error& e) {
        log_error(std::string("torch::jit::load failed: ") + e.what());
        return kExitJitLoad;
    }
    if (!parse_external_pos_state(extra_files, external_pos)) {
        return kExitJitLoad;
    }
    if (external_pos.enabled) {
        log_ok("external positional embedding metadata loaded");
    }
    module.eval();

    std::mt19937 rng(static_cast<std::uint32_t>(kRandomSeedBase + options.resolution));
    const auto input_dtype = infer_input_dtype_from_filename(options.ts_path);
    auto input = make_synthetic_input(options.resolution, input_dtype, rng);
    auto pos_grid = make_pos_grid(external_pos, options.resolution, input.scalar_type());

    const auto outcome = run_bench(
        module, input, pos_grid,
        BenchSchedule{.warmup_iterations = options.warmup, .timed_iterations = options.iterations});
    if (outcome.exit_code != 0) {
        return outcome.exit_code;
    }

    (void)std::printf(
        "[OK] forward mean=%.1f ms p50=%.1f ms p99=%.1f ms  alpha=[%.4f, %.4f]  "
        "nan=%s inf=%s  iters=%d\n",
        outcome.mean_ms, outcome.p50_ms, outcome.p99_ms, outcome.alpha_min, outcome.alpha_max,
        outcome.has_nan ? "true" : "false", outcome.has_inf ? "true" : "false", options.iterations);

    return (outcome.has_nan || outcome.has_inf) ? kExitNanOrInf : 0;
}
