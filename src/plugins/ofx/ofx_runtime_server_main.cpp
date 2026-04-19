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
            LocalFree(argv);
            return Unexpected<Error>(converted.error());
        }
        args.push_back(std::move(*converted));
    }

    LocalFree(argv);
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

        if (token.rfind("--endpoint-port=", 0) == 0) {
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

        if (token.rfind("--idle-timeout-ms=", 0) == 0) {
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

        if (token.rfind("--log-path=", 0) == 0) {
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
    // CUDA graph capture (CORRIDORKEY_TRT_CUDA_GRAPH=1) requires I/O binding
    // by contract: the captured graph reads from / writes to fixed CUDA
    // virtual addresses at replay time, so outputs must be pre-allocated and
    // stable. When CUDA graph is opt-in, flip the I/O binding default back
    // to On even in this runtime server process so the two features agree.
    const char* cuda_graph_env = std::getenv("CORRIDORKEY_TRT_CUDA_GRAPH");
    const bool cuda_graph_requested =
        cuda_graph_env != nullptr && std::string_view(cuda_graph_env) == "1";

    if (std::getenv("CORRIDORKEY_IO_BINDING") == nullptr) {
        _putenv_s("CORRIDORKEY_IO_BINDING", cuda_graph_requested ? "on" : "off");
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

int APIENTRY wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    return corridorkey::ofx::run_runtime_server();
}
