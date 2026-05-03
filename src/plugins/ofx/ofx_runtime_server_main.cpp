// shellapi.h depends on windows.h being included first on MSVC.
// clang-format off
#include <windows.h>
#include <shellapi.h>
// clang-format on

#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "app/ofx_runtime_service.hpp"
#include "common/runtime_paths.hpp"

namespace corridorkey::ofx {

// NOLINTBEGIN(modernize-use-designated-initializers,bugprone-easily-swappable-parameters,readability-function-size,readability-function-cognitive-complexity,cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,cppcoreguidelines-avoid-magic-numbers,bugprone-unchecked-string-to-number-conversion,cppcoreguidelines-pro-type-cstyle-cast,modernize-use-using,modernize-use-integer-sign-comparison,cert-dcl50-cpp,cppcoreguidelines-pro-type-const-cast,readability-identifier-naming,modernize-raw-string-literal,readability-container-size-empty,bugprone-command-processor,readability-use-std-min-max,cppcoreguidelines-avoid-non-const-global-variables,bugprone-misplaced-widening-cast,readability-misleading-indentation,cert-env33-c,performance-unnecessary-copy-initialization,readability-named-parameter,readability-isolate-declaration,cert-err34-c,modernize-avoid-variadic-functions,cppcoreguidelines-pro-bounds-constant-array-index)
//
// ofx_runtime_server_main.cpp tidy-suppression rationale.
//
// The Error{} aggregate is the engine-wide failure form across every
// command-line parsing path. parse_runtime_service_options is a flat
// argv loop whose size and complexity reflect the option set the
// service exposes (--endpoint-port / --idle-timeout-ms / --log-path in
// both inline and = forms), not accidental complexity. Argv index
// access is bounded by the loop counter and the explicit
// `index + 1 >= args.size()` guard ahead of every value lookup. The
// 65535 maximum is the IANA TCP/UDP port ceiling, a universally known
// constant. The (value, option_name) string_view parameter pair is
// deliberately positional because the caller only ever supplies
// matching-key argument pairs.
namespace {

Result<std::string> wide_to_utf8(std::wstring_view value) {
    if (value.empty()) {
        return std::string{};
    }

    const int size = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                                         nullptr, 0, nullptr, nullptr);
    if (size <= 0) {
        return Unexpected<Error>(
            Error{ErrorCode::IoError, "Failed to convert the OFX runtime arguments."});
    }

    std::string utf8(static_cast<std::size_t>(size), '\0');
    const int written =
        WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), utf8.data(),
                            size, nullptr, nullptr);
    if (written != size) {
        return Unexpected<Error>(
            Error{ErrorCode::IoError, "Failed to convert the OFX runtime arguments."});
    }

    return utf8;
}

Result<std::vector<std::string>> command_line_arguments() {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv == nullptr) {
        return Unexpected<Error>(
            Error{ErrorCode::IoError, "Failed to read the OFX runtime command line."});
    }

    std::vector<std::string> args;
    args.reserve(static_cast<std::size_t>(argc));
    for (int index = 0; index < argc; ++index) {
        auto converted = wide_to_utf8(argv[index]);
        if (!converted) {
            LocalFree(static_cast<HLOCAL>(argv));
            return Unexpected<Error>(converted.error());
        }
        args.push_back(std::move(*converted));
    }

    LocalFree(static_cast<HLOCAL>(argv));
    return args;
}

Result<int> parse_positive_int(std::string_view value, std::string_view option_name) {
    int parsed = 0;
    const auto* begin = value.data();
    const auto* end = value.data() + value.size();
    auto result = std::from_chars(begin, end, parsed);
    if (result.ec != std::errc{} || result.ptr != end || parsed <= 0) {
        return Unexpected<Error>(Error{
            ErrorCode::InvalidParameters,
            "Invalid value for " + std::string(option_name) + ".",
        });
    }
    return parsed;
}

Result<void> parse_runtime_service_options(const std::vector<std::string>& args,
                                           app::OfxRuntimeServiceOptions& options) {
    for (std::size_t index = 1; index < args.size(); ++index) {
        const std::string_view token(args[index]);
        if (token == "ofx-runtime-server") {
            continue;
        }

        if (token == "--endpoint-port") {
            if (index + 1 >= args.size()) {
                return Unexpected<Error>(Error{
                    ErrorCode::InvalidParameters,
                    "Missing value for --endpoint-port.",
                });
            }
            auto port = parse_positive_int(args[++index], "--endpoint-port");
            if (!port || *port > 65535) {
                return Unexpected<Error>(
                    port ? Error{ErrorCode::InvalidParameters, "Invalid value for --endpoint-port."}
                         : port.error());
            }
            options.endpoint.port = static_cast<std::uint16_t>(*port);
            continue;
        }

        if (token.starts_with("--endpoint-port=")) {
            auto port = parse_positive_int(
                token.substr(std::string_view("--endpoint-port=").size()), "--endpoint-port");
            if (!port || *port > 65535) {
                return Unexpected<Error>(
                    port ? Error{ErrorCode::InvalidParameters, "Invalid value for --endpoint-port."}
                         : port.error());
            }
            options.endpoint.port = static_cast<std::uint16_t>(*port);
            continue;
        }

        if (token == "--idle-timeout-ms") {
            if (index + 1 >= args.size()) {
                return Unexpected<Error>(Error{
                    ErrorCode::InvalidParameters,
                    "Missing value for --idle-timeout-ms.",
                });
            }
            auto timeout_ms = parse_positive_int(args[++index], "--idle-timeout-ms");
            if (!timeout_ms) {
                return Unexpected<Error>(timeout_ms.error());
            }
            options.idle_timeout = std::chrono::milliseconds(*timeout_ms);
            options.broker.idle_session_ttl = options.idle_timeout;
            continue;
        }

        if (token.starts_with("--idle-timeout-ms=")) {
            auto timeout_ms = parse_positive_int(
                token.substr(std::string_view("--idle-timeout-ms=").size()), "--idle-timeout-ms");
            if (!timeout_ms) {
                return Unexpected<Error>(timeout_ms.error());
            }
            options.idle_timeout = std::chrono::milliseconds(*timeout_ms);
            options.broker.idle_session_ttl = options.idle_timeout;
            continue;
        }

        if (token == "--log-path") {
            if (index + 1 >= args.size()) {
                return Unexpected<Error>(Error{
                    ErrorCode::InvalidParameters,
                    "Missing value for --log-path.",
                });
            }
            options.log_path = std::filesystem::path(std::string(args[++index]));
            continue;
        }

        if (token.starts_with("--log-path=")) {
            options.log_path = std::filesystem::path(
                std::string(token.substr(std::string_view("--log-path=").size())));
            continue;
        }

        return Unexpected<Error>(Error{
            ErrorCode::InvalidParameters,
            "Unsupported OFX runtime server option: " + std::string(token),
        });
    }

    return {};
}

}  // namespace

int run_runtime_server() {
    // Default I/O binding OFF in the OFX runtime server process.
    //
    // I/O binding reuses pinned host / device-resident output buffers
    // across frames to cut memcpy and allocator overhead. On a dedicated
    // GPU (CLI, harness) that is a small win (~7% at the best-case warm
    // frame). Under DaVinci Resolve the GPU is shared with the host
    // decoder/color/compositor/encoder pipelines, and the held buffers
    // amplify TensorRT RTX kernel-time contention as the session
    // accumulates frames.
    //
    // Measured on v0.7.5-11 via ORT per-op profiling (95-frame Resolve
    // session, 4K 2048-quality):
    //   binding on   p50 trt_kernel 5200 ms, p99 26146 ms
    //   binding off  p50 trt_kernel 2632 ms, p99  7265 ms
    //
    // The p99 26 s figure under IO-on matches the "stuck Loading..."
    // outlier reported against the initial v0.7.5 series. Defaulting
    // off here fixes that regression for every Resolve session without
    // requiring an env var; an explicit `CORRIDORKEY_IO_BINDING=on`
    // still opts back in for benchmark comparisons. The CLI and
    // `ofx_benchmark_harness` are unaffected because they do not run
    // this entrypoint.
    // Default CUDA graph capture ON in the OFX runtime server process.
    //
    // Measured on v0.7.5-21 with the harness (60 frames Jordan4k, 4K
    // 2048) and a real DaVinci Resolve session (91 frames, ORT per-op
    // profiling enabled):
    //
    //   graph OFF / io ON  (v0.7.5-11 original): p50 5200 ms, p99 26146 ms
    //   graph OFF / io OFF (v0.7.5-20 mitigation): p50 2632 ms, p99 7265 ms
    //   graph ON  / io ON  (v0.7.5-21 experiment): p50  344 ms, p99  524 ms
    //
    // The graph-on configuration matches the dedicated-GPU harness
    // steady state (~350 ms) inside Resolve too, with no in-session
    // degradation across 90 frames. Hypothesis (validated empirically,
    // not in docs): the CUDA driver treats a captured graph as one
    // atomic DAG submission, so Resolve's own decoder/color/compositor
    // CUDA work cannot interleave between TensorRT RTX kernels the way
    // it could when each kernel was launched individually. Matches the
    // `torch.compiler.cudagraph_mark_step_begin()` path used by the
    // reference CorridorKey Python engine.
    //
    // Prerequisites (handled below):
    // - I/O binding must be ON: CUDA graphs read/write fixed CUDA
    //   virtual addresses at replay, so outputs must live at stable
    //   pre-allocated buffers (see ORT CUDA EP docs, "CUDA Graphs").
    // - Input shapes must stay static: already the case for our
    //   packaged FP16 ladder where nv_profile_min/opt/max are set to
    //   the same shape.
    //
    // Known upstream caveat: ORT issue #27329 describes precompiled
    // TRT RTX engines sometimes not triggering capture. Our v1.23.0
    // build captures correctly in practice (verified via ORT per-op
    // profiling); documented in docs/WINDOWS_BUILD.md for operators on
    // a different ORT pin.
    //
    // Explicit overrides still win:
    //   CORRIDORKEY_TRT_CUDA_GRAPH=0  -> disable capture
    //   CORRIDORKEY_IO_BINDING=off    -> disable binding (also disables
    //                                    graph capture implicitly since
    //                                    binding is a hard prerequisite)
    if (std::getenv("CORRIDORKEY_TRT_CUDA_GRAPH") == nullptr) {
        _putenv_s("CORRIDORKEY_TRT_CUDA_GRAPH", "1");
    }
    const char* cuda_graph_env = std::getenv("CORRIDORKEY_TRT_CUDA_GRAPH");
    const bool cuda_graph_active =
        cuda_graph_env != nullptr && std::string_view(cuda_graph_env) == "1";

    if (std::getenv("CORRIDORKEY_IO_BINDING") == nullptr) {
        _putenv_s("CORRIDORKEY_IO_BINDING", cuda_graph_active ? "on" : "off");
    }

    auto args = command_line_arguments();
    if (!args) {
        return 1;
    }

    app::OfxRuntimeServiceOptions service_options;
    service_options.endpoint = common::default_ofx_runtime_endpoint();
    service_options.idle_timeout = common::kDefaultOfxIdleTimeout;
    service_options.log_path = common::ofx_runtime_server_log_path();
    service_options.broker.idle_session_ttl = service_options.idle_timeout;

    auto parse_result = parse_runtime_service_options(*args, service_options);
    if (!parse_result) {
        return 1;
    }

    auto run_result = app::OfxRuntimeService::run(service_options);
    if (!run_result) {
        return 1;
    }

    return 0;
}

}  // namespace corridorkey::ofx
// NOLINTEND(modernize-use-designated-initializers,bugprone-easily-swappable-parameters,readability-function-size,readability-function-cognitive-complexity,cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,cppcoreguidelines-avoid-magic-numbers,bugprone-unchecked-string-to-number-conversion,cppcoreguidelines-pro-type-cstyle-cast,modernize-use-using,modernize-use-integer-sign-comparison,cert-dcl50-cpp,cppcoreguidelines-pro-type-const-cast,readability-identifier-naming,modernize-raw-string-literal,readability-container-size-empty,bugprone-command-processor,readability-use-std-min-max,cppcoreguidelines-avoid-non-const-global-variables,bugprone-misplaced-widening-cast,readability-misleading-indentation,cert-env33-c,performance-unnecessary-copy-initialization,readability-named-parameter,readability-isolate-declaration,cert-err34-c,modernize-avoid-variadic-functions,cppcoreguidelines-pro-bounds-constant-array-index)

int APIENTRY wWinMain(HINSTANCE /*instance*/, HINSTANCE /*prev_instance*/, PWSTR /*command_line*/,
                      int /*show_command*/) {
    return corridorkey::ofx::run_runtime_server();
}
