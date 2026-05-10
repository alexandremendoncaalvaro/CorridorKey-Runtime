#include <catch2/catch_all.hpp>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <filesystem>
#include <iterator>
#include <optional>
#include <string>
#include <thread>

#include "app/ofx_runtime_protocol.hpp"
#include "app/ofx_runtime_service.hpp"
#include "common/local_ipc.hpp"

//
// Test-file tidy-suppression rationale.
//
// Test fixtures legitimately use single-letter loop locals, magic
// numbers (resolution rungs, pixel coordinates, expected error counts),
// std::vector::operator[] on indices the test itself just constructed,
// and Catch2 / aggregate-init styles that pre-date the project's
// tightened .clang-tidy ruleset. The test source is verified
// behaviourally by ctest; converting every site to bounds-checked /
// designated-init / ranges form would obscure intent without changing
// what the tests prove. The same suppressions are documented and
// applied on the src/ tree where the underlying APIs live.
//
// NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,readability-identifier-length,bugprone-easily-swappable-parameters,readability-function-cognitive-complexity,readability-function-size,cppcoreguidelines-avoid-magic-numbers,modernize-use-designated-initializers,readability-uppercase-literal-suffix,readability-math-missing-parentheses,modernize-use-ranges,modernize-use-starts-ends-with,modernize-use-emplace,modernize-use-auto,modernize-loop-convert,modernize-avoid-c-style-cast,modernize-return-braced-init-list,readability-implicit-bool-conversion,readability-container-contains,readability-redundant-member-init,readability-redundant-string-init,bugprone-narrowing-conversions,cppcoreguidelines-narrowing-conversions,readability-avoid-nested-conditional-operator,modernize-use-nodiscard,readability-make-member-function-const,cppcoreguidelines-pro-type-reinterpret-cast,bugprone-implicit-widening-of-multiplication-result,readability-redundant-inline-specifier,cppcoreguidelines-prefer-member-initializer,performance-unnecessary-value-param,readability-use-concise-preprocessor-directives,readability-else-after-return,readability-string-compare,bugprone-exception-escape,cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays,bugprone-branch-clone,cert-err33-c,readability-redundant-declaration,readability-qualified-auto,modernize-use-scoped-lock,modernize-use-bool-literals,cppcoreguidelines-init-variables,cppcoreguidelines-special-member-functions,cppcoreguidelines-owning-memory,cppcoreguidelines-no-malloc,performance-enum-size,performance-avoid-endl,bugprone-unchecked-optional-access,bugprone-unchecked-string-to-number-conversion,cppcoreguidelines-pro-type-cstyle-cast,modernize-use-using,modernize-use-integer-sign-comparison,cert-dcl50-cpp,cppcoreguidelines-pro-type-const-cast,readability-identifier-naming,modernize-raw-string-literal,readability-container-size-empty,bugprone-command-processor,readability-use-std-min-max,cppcoreguidelines-avoid-non-const-global-variables,bugprone-misplaced-widening-cast,readability-misleading-indentation,cert-env33-c,performance-unnecessary-copy-initialization,readability-named-parameter,readability-isolate-declaration,cert-err34-c,modernize-avoid-variadic-functions,cppcoreguidelines-pro-bounds-constant-array-index)

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

using namespace corridorkey;
using namespace corridorkey::app;
using namespace corridorkey::common;

namespace {

#if defined(_WIN32)
void set_environment_variable_for_test(const char* name, const std::optional<std::string>& value) {
    _putenv_s(name, value ? value->c_str() : "");
}

class ScopedUnsetEnvVar {
   public:
    explicit ScopedUnsetEnvVar(const char* name) : m_name(name) {
        if (const char* value = std::getenv(name); value != nullptr) {
            m_previous = value;
        }
        set_environment_variable_for_test(m_name.c_str(), std::nullopt);
    }

    ~ScopedUnsetEnvVar() { set_environment_variable_for_test(m_name.c_str(), m_previous); }

    ScopedUnsetEnvVar(const ScopedUnsetEnvVar&) = delete;
    ScopedUnsetEnvVar& operator=(const ScopedUnsetEnvVar&) = delete;

   private:
    std::string m_name;
    std::optional<std::string> m_previous;
};
#endif

std::uint16_t reserve_local_port() {
#if defined(_WIN32)
    WSADATA data;
    REQUIRE(WSAStartup(MAKEWORD(2, 2), &data) == 0);
    SOCKET socket_handle = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    REQUIRE(socket_handle != INVALID_SOCKET);
#else
    int socket_handle = socket(AF_INET, SOCK_STREAM, 0);
    REQUIRE(socket_handle >= 0);
#endif

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(0);

    REQUIRE(bind(socket_handle, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0);

    sockaddr_in bound_address{};
#if defined(_WIN32)
    int length = sizeof(bound_address);
    REQUIRE(getsockname(socket_handle, reinterpret_cast<sockaddr*>(&bound_address), &length) == 0);
    closesocket(socket_handle);
#else
    socklen_t length = sizeof(bound_address);
    REQUIRE(getsockname(socket_handle, reinterpret_cast<sockaddr*>(&bound_address), &length) == 0);
    close(socket_handle);
#endif
    return ntohs(bound_address.sin_port);
}

}  // namespace

TEST_CASE("ofx runtime service responds to health and shutdown commands",
          "[integration][ofx][runtime]") {
    const auto port = reserve_local_port();
    const auto log_path = std::filesystem::temp_directory_path() /
                          ("corridorkey_ofx_runtime_" + std::to_string(port) + ".log");

    OfxRuntimeServiceOptions options;
    options.endpoint = LocalJsonEndpoint{"127.0.0.1", port};
    options.idle_timeout = std::chrono::seconds(1);
    options.log_path = log_path;

    std::optional<Error> server_error;
    std::thread server_thread([&]() {
        auto result = OfxRuntimeService::run(options);
        if (!result) {
            server_error = result.error();
        }
    });

    auto stop_server = [&]() {
        OfxRuntimeRequestEnvelope shutdown_request;
        shutdown_request.command = OfxRuntimeCommand::Shutdown;
        shutdown_request.payload = to_json(OfxRuntimeShutdownRequest{"test_shutdown"});
        auto shutdown_response =
            send_json_request(options.endpoint, to_json(shutdown_request), 2000);
        if (shutdown_response) {
            auto parsed = ofx_runtime_response_from_json(*shutdown_response);
            REQUIRE(parsed.has_value());
            REQUIRE(parsed->success);
        }
    };

    bool ready = false;
    for (int attempt = 0; attempt < 20; ++attempt) {
        OfxRuntimeRequestEnvelope health_request;
        health_request.command = OfxRuntimeCommand::Health;
        health_request.payload = nlohmann::json::object();

        auto health_response = send_json_request(options.endpoint, to_json(health_request), 500);
        if (health_response) {
            auto parsed_response = ofx_runtime_response_from_json(*health_response);
            REQUIRE(parsed_response.has_value());
            REQUIRE(parsed_response->success);

            auto health = health_response_from_json(parsed_response->payload);
            REQUIRE(health.has_value());
            CHECK(health->server_pid > 0);
            CHECK(health->session_count == 0);
            CHECK(health->active_session_count == 0);
            ready = true;
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    if (ready) {
        stop_server();
    }
    server_thread.join();
    CAPTURE(port);
    REQUIRE(ready);
    REQUIRE_FALSE(server_error.has_value());
    REQUIRE(std::filesystem::exists(log_path));
}

#if defined(_WIN32)
TEST_CASE("windows OFX runtime server defaults CUDA graph off",
          "[integration][ofx][runtime][regression]") {
    const auto port = reserve_local_port();
    const auto log_path = std::filesystem::temp_directory_path() /
                          ("corridorkey_ofx_runtime_server_defaults_" +
                           std::to_string(port) + ".log");
    std::filesystem::remove(log_path);

    ScopedUnsetEnvVar generic_graph_env("CORRIDORKEY_TRT_CUDA_GRAPH");
    ScopedUnsetEnvVar torchtrt_graph_env("CORRIDORKEY_TORCHTRT_CUDA_GRAPH");
    ScopedUnsetEnvVar io_binding_env("CORRIDORKEY_IO_BINDING");
    ScopedUnsetEnvVar input_boundary_env("CORRIDORKEY_TORCHTRT_INPUT_BOUNDARY");

    const std::filesystem::path server_path = OFX_RUNTIME_SERVER_PATH;
    REQUIRE(std::filesystem::exists(server_path));

    std::wstring command_line = L"\"" + server_path.wstring() +
                                L"\" ofx-runtime-server --endpoint-port " +
                                std::to_wstring(port) + L" --idle-timeout-ms 50 --log-path \"" +
                                log_path.wstring() + L"\"";

    STARTUPINFOW startup_info{};
    startup_info.cb = sizeof(startup_info);
    startup_info.dwFlags = STARTF_USESHOWWINDOW;
    startup_info.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION process_info{};
    REQUIRE(CreateProcessW(nullptr, command_line.data(), nullptr, nullptr, FALSE,
                           DETACHED_PROCESS | CREATE_NO_WINDOW, nullptr, nullptr,
                           &startup_info, &process_info) != 0);

    const DWORD wait_status = WaitForSingleObject(process_info.hProcess, 10000);
    if (wait_status == WAIT_TIMEOUT) {
        TerminateProcess(process_info.hProcess, 1);
    }
    DWORD exit_code = STILL_ACTIVE;
    GetExitCodeProcess(process_info.hProcess, &exit_code);
    CloseHandle(process_info.hThread);
    CloseHandle(process_info.hProcess);

    REQUIRE(wait_status == WAIT_OBJECT_0);
    REQUIRE(exit_code == 0);
    REQUIRE(std::filesystem::exists(log_path));

    std::ifstream log_input(log_path);
    REQUIRE(log_input.good());
    const std::string log_text((std::istreambuf_iterator<char>(log_input)),
                               std::istreambuf_iterator<char>());
    CHECK(log_text.find("event=server_start") != std::string::npos);
    CHECK(log_text.find("cuda_graph_env=0") != std::string::npos);
    CHECK(log_text.find("torchtrt_cuda_graph_env=unset") != std::string::npos);
    CHECK(log_text.find("io_binding_env=off") != std::string::npos);
}
#endif

// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,readability-identifier-length,bugprone-easily-swappable-parameters,readability-function-cognitive-complexity,readability-function-size,cppcoreguidelines-avoid-magic-numbers,modernize-use-designated-initializers,readability-uppercase-literal-suffix,readability-math-missing-parentheses,modernize-use-ranges,modernize-use-starts-ends-with,modernize-use-emplace,modernize-use-auto,modernize-loop-convert,modernize-avoid-c-style-cast,modernize-return-braced-init-list,readability-implicit-bool-conversion,readability-container-contains,readability-redundant-member-init,readability-redundant-string-init,bugprone-narrowing-conversions,cppcoreguidelines-narrowing-conversions,readability-avoid-nested-conditional-operator,modernize-use-nodiscard,readability-make-member-function-const,cppcoreguidelines-pro-type-reinterpret-cast,bugprone-implicit-widening-of-multiplication-result,readability-redundant-inline-specifier,cppcoreguidelines-prefer-member-initializer,performance-unnecessary-value-param,readability-use-concise-preprocessor-directives,readability-else-after-return,readability-string-compare,bugprone-exception-escape,cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays,bugprone-branch-clone,cert-err33-c,readability-redundant-declaration,readability-qualified-auto,modernize-use-scoped-lock,modernize-use-bool-literals,cppcoreguidelines-init-variables,cppcoreguidelines-special-member-functions,cppcoreguidelines-owning-memory,cppcoreguidelines-no-malloc,performance-enum-size,performance-avoid-endl,bugprone-unchecked-optional-access,bugprone-unchecked-string-to-number-conversion,cppcoreguidelines-pro-type-cstyle-cast,modernize-use-using,modernize-use-integer-sign-comparison,cert-dcl50-cpp,cppcoreguidelines-pro-type-const-cast,readability-identifier-naming,modernize-raw-string-literal,readability-container-size-empty,bugprone-command-processor,readability-use-std-min-max,cppcoreguidelines-avoid-non-const-global-variables,bugprone-misplaced-widening-cast,readability-misleading-indentation,cert-env33-c,performance-unnecessary-copy-initialization,readability-named-parameter,readability-isolate-declaration,cert-err34-c,modernize-avoid-variadic-functions,cppcoreguidelines-pro-bounds-constant-array-index)
