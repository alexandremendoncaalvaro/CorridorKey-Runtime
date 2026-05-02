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
//        [--resolution <n>] [--bin-dir <path>] [--iterations <n>]
//
// On success prints:
//   [OK] forward avg=<ms>ms alpha=[<min>,<max>]  no NaN/Inf
// Non-zero exit on any failure.

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <random>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

#include <torch/cuda.h>
#include <torch/script.h>

namespace {

struct Options {
    std::filesystem::path ts_path;
    std::filesystem::path bin_dir;
    int resolution = 0;  // auto-detect from filename if 0
    int iterations = 5;
    int warmup = 2;
};

bool parse_int(const char* arg, int& out) {
    try {
        out = std::stoi(arg);
        return true;
    } catch (...) {
        return false;
    }
}

bool parse_args(int argc, char* argv[], Options& options) {
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto need = [&](const char* flag) -> const char* {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "[ERR] %s needs a value\n", flag);
                return nullptr;
            }
            return argv[++i];
        };
        if (a == "--ts") {
            const char* v = need("--ts");
            if (!v) return false;
            options.ts_path = v;
        } else if (a == "--bin-dir") {
            const char* v = need("--bin-dir");
            if (!v) return false;
            options.bin_dir = v;
        } else if (a == "--resolution") {
            const char* v = need("--resolution");
            if (!v) return false;
            if (!parse_int(v, options.resolution)) return false;
        } else if (a == "--iterations") {
            const char* v = need("--iterations");
            if (!v) return false;
            if (!parse_int(v, options.iterations)) return false;
        } else if (a == "--warmup") {
            const char* v = need("--warmup");
            if (!v) return false;
            if (!parse_int(v, options.warmup)) return false;
        } else if (a == "--help" || a == "-h") {
            std::printf(
                "Usage: corridorkey-torchtrt-runner --ts <path> "
                "[--resolution N] [--bin-dir DIR] [--iterations N] [--warmup N]\n");
            return false;
        } else {
            std::fprintf(stderr, "[ERR] unknown arg: %s\n", a.c_str());
            return false;
        }
    }
    if (options.ts_path.empty()) {
        std::fprintf(stderr, "[ERR] --ts is required\n");
        return false;
    }
    return true;
}

int infer_resolution_from_filename(const std::filesystem::path& p) {
    // corridorkey_torchtrt_fp16_<res>.ts  -> <res>
    const auto stem = p.stem().string();
    auto pos = stem.find_last_of('_');
    if (pos == std::string::npos) return 0;
    int res = 0;
    if (!parse_int(stem.substr(pos + 1).c_str(), res)) return 0;
    return res;
}

#ifdef _WIN32
bool stage_dll_directory(const std::filesystem::path& bin_dir) {
    if (bin_dir.empty()) return true;
    if (!std::filesystem::exists(bin_dir)) {
        std::fprintf(stderr, "[ERR] --bin-dir does not exist: %s\n", bin_dir.string().c_str());
        return false;
    }
    // AddDllDirectory needs absolute wide path. The OS loader will then
    // search this directory when LoadLibraryEx-class calls request a DLL
    // by short name. This is the same hook PR 3's TorchTrtSession will
    // call from the blue model pack's runtime location.
    const auto absolute = std::filesystem::absolute(bin_dir);
    auto cookie = AddDllDirectory(absolute.wstring().c_str());
    if (cookie == nullptr) {
        std::fprintf(stderr, "[ERR] AddDllDirectory failed for %s (GetLastError=%lu)\n",
                     absolute.string().c_str(), GetLastError());
        return false;
    }
    SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS | LOAD_LIBRARY_SEARCH_USER_DIRS);

    // Force-load torchtrt.dll so its static initializers register the
    // TorchScript custom ops the .ts engines reference. torch::jit::load
    // dispatches to those custom op resolvers; without this the load
    // raises "Unknown builtin op: tensorrt::execute_engine".
    //
    // Use absolute path to bypass DLL search rules entirely. The
    // SetDefaultDllDirectories call above narrows the loader to
    // AddDllDirectory'd dirs, which excludes the exe directory; passing
    // the absolute torchtrt.dll path side-steps that restriction
    // for this one explicit load while still letting torchtrt.dll's
    // transitive deps resolve from the AddDllDirectory'd vendor/bin.
    const auto torchtrt_path = (absolute / L"torchtrt.dll").wstring();
    HMODULE handle = LoadLibraryExW(torchtrt_path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (handle == nullptr) {
        std::fprintf(stderr, "[ERR] LoadLibrary torchtrt.dll failed (GetLastError=%lu)\n",
                     GetLastError());
        return false;
    }
    std::printf("[ok] AddDllDirectory + LoadLibrary torchtrt.dll succeeded\n");
    return true;
}
#else
bool stage_dll_directory(const std::filesystem::path&) {
    return true;
}
#endif

}  // namespace

int main(int argc, char* argv[]) {
    Options options;
    if (!parse_args(argc, argv, options)) {
        return 2;
    }
    if (options.bin_dir.empty()) {
        // Default to the vendored runtime relative to this exe's location
        // (CMake places the binary under build/<preset>/tools/torchtrt_runner/).
        // Walk up to repo root and into vendor/torchtrt-windows/bin.
        // If the user moves the exe, they must pass --bin-dir explicitly.
        std::filesystem::path exe_dir;
#ifdef _WIN32
        wchar_t buf[MAX_PATH];
        DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
        if (n > 0 && n < MAX_PATH) {
            exe_dir = std::filesystem::path(buf).parent_path();
        }
#endif
        // build/<preset>/tools/torchtrt_runner/<exe> -> ../../../vendor/torchtrt-windows/bin
        if (!exe_dir.empty()) {
            options.bin_dir = exe_dir / ".." / ".." / ".." / "vendor" / "torchtrt-windows" / "bin";
        }
    }

    if (options.resolution == 0) {
        options.resolution = infer_resolution_from_filename(options.ts_path);
        if (options.resolution == 0) {
            std::fprintf(stderr,
                         "[ERR] could not infer --resolution from filename %s; pass --resolution "
                         "explicitly\n",
                         options.ts_path.string().c_str());
            return 2;
        }
        std::printf("[ok] inferred resolution=%d from filename\n", options.resolution);
    }

    if (!stage_dll_directory(options.bin_dir)) {
        return 3;
    }

    if (!std::filesystem::exists(options.ts_path)) {
        std::fprintf(stderr, "[ERR] --ts not found: %s\n", options.ts_path.string().c_str());
        return 4;
    }

    if (!torch::cuda::is_available()) {
        std::fprintf(stderr,
                     "[ERR] CUDA not available - this runner expects an Ampere or newer GPU\n");
        return 5;
    }

    torch::jit::script::Module module;
    try {
        const auto t0 = std::chrono::high_resolution_clock::now();
        module = torch::jit::load(options.ts_path.string(), torch::Device(torch::kCUDA));
        const auto load_ms = std::chrono::duration<double, std::milli>(
                                 std::chrono::high_resolution_clock::now() - t0)
                                 .count();
        std::printf("[ok] torch::jit::load %.1f ms\n", load_ms);
    } catch (const c10::Error& e) {
        std::fprintf(stderr, "[ERR] torch::jit::load failed: %s\n", e.what());
        return 6;
    }
    module.eval();

    // Synthetic input: shape (1, 4, R, R), FP16 on CUDA. Same shape the
    // Sprint 0 Python harness uses, kept apples-to-apples.
    auto input_options = torch::TensorOptions().dtype(torch::kFloat16).device(torch::kCUDA);
    std::mt19937 rng(42 + options.resolution);
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);
    std::vector<float> host(1 * 4 * static_cast<size_t>(options.resolution) *
                            static_cast<size_t>(options.resolution));
    for (auto& v : host) v = dist(rng);
    auto cpu_input = torch::from_blob(host.data(), {1, 4, options.resolution, options.resolution},
                                      torch::kFloat32)
                         .clone();
    auto input = cpu_input.to(input_options);

    // Warmup + timed runs.
    torch::NoGradGuard no_grad;
    for (int i = 0; i < options.warmup; ++i) {
        try {
            (void)module.forward({input});
        } catch (const c10::Error& e) {
            std::fprintf(stderr, "[ERR] warmup forward failed: %s\n", e.what());
            return 7;
        }
    }
    if (torch::cuda::is_available()) {
        torch::cuda::synchronize();
    }

    std::vector<double> latencies_ms;
    latencies_ms.reserve(options.iterations);
    torch::Tensor last_alpha;
    for (int i = 0; i < options.iterations; ++i) {
        const auto t0 = std::chrono::high_resolution_clock::now();
        torch::IValue out;
        try {
            out = module.forward({input});
        } catch (const c10::Error& e) {
            std::fprintf(stderr, "[ERR] forward iter %d failed: %s\n", i, e.what());
            return 8;
        }
        torch::cuda::synchronize();
        const double dt = std::chrono::duration<double, std::milli>(
                              std::chrono::high_resolution_clock::now() - t0)
                              .count();
        latencies_ms.push_back(dt);

        if (out.isTuple()) {
            last_alpha = out.toTuple()->elements()[0].toTensor();
        } else if (out.isTensor()) {
            last_alpha = out.toTensor();
        } else {
            std::fprintf(stderr, "[ERR] unexpected forward return type\n");
            return 9;
        }
    }

    auto alpha_cpu = last_alpha.detach().to(torch::kCPU).to(torch::kFloat32);
    auto alpha_min = alpha_cpu.min().item<float>();
    auto alpha_max = alpha_cpu.max().item<float>();
    bool has_nan = alpha_cpu.isnan().any().item<bool>();
    bool has_inf = alpha_cpu.isinf().any().item<bool>();

    double sum_ms = 0;
    for (auto v : latencies_ms) sum_ms += v;
    const double avg_ms = sum_ms / static_cast<double>(latencies_ms.size());

    std::printf("[OK] forward avg=%.1f ms  alpha=[%.4f, %.4f]  nan=%s inf=%s  iters=%d\n", avg_ms,
                alpha_min, alpha_max, has_nan ? "true" : "false", has_inf ? "true" : "false",
                options.iterations);

    return (has_nan || has_inf) ? 10 : 0;
}
