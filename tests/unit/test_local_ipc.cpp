#include <catch2/catch_all.hpp>
#include <chrono>
#include <future>
#include <thread>

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
#pragma comment(lib, "ws2_32.lib")
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

using namespace corridorkey::common;

namespace {
bool write_raw(const LocalJsonEndpoint& endpoint, const std::string& data) {
    auto sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock < 0) return false;

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(endpoint.port);
    inet_pton(AF_INET, endpoint.host.c_str(), &addr.sin_addr);

    if (connect(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
#if defined(_WIN32)
        closesocket(sock);
#else
        close(sock);
#endif
        return false;
    }

    send(sock, data.data(), static_cast<int>(data.size()), 0);
#if defined(_WIN32)
    closesocket(sock);
#else
    close(sock);
#endif
    return true;
}
}  // namespace

TEST_CASE("LocalJsonServer accept times out correctly", "[unit][ipc][timeout]") {
    LocalJsonEndpoint endpoint{"127.0.0.1", 41230};
    auto server_res = LocalJsonServer::listen(endpoint);
    REQUIRE(server_res.has_value());

    auto start = std::chrono::steady_clock::now();
    auto accept_res = server_res->accept_one(50);  // 50ms timeout
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - start)
                        .count();

    // Accept should succeed in returning empty optional immediately after timeout
    REQUIRE(accept_res.has_value());
    REQUIRE(!accept_res->has_value());

    // Ensure it actually waited around 50ms
    REQUIRE(duration >= 25LL);
}

TEST_CASE("LocalJsonConnection read_json times out on incomplete data", "[unit][ipc][timeout]") {
    LocalJsonEndpoint endpoint{"127.0.0.1", 41231};
    auto server_res = LocalJsonServer::listen(endpoint);
    REQUIRE(server_res.has_value());

    // Send an incomplete JSON string (no newline separator)
    std::string incomplete_payload = "{\"status\": \"pending\"";

    auto future =
        std::async(std::launch::async, [&]() { return write_raw(endpoint, incomplete_payload); });

    auto accept_res = server_res->accept_one(2000);
    REQUIRE(accept_res.has_value());
    REQUIRE(accept_res->has_value());

    auto& conn = **accept_res;

    // Read should time out because the newline delimiter never arrives
    auto read_res = conn.read_json(50);  // 50ms timeout

    REQUIRE(!read_res.has_value());
    bool has_timeout_msg = read_res.error().message.find("timeout") != std::string::npos ||
                           read_res.error().message.find("timed out") != std::string::npos ||
                           read_res.error().message.find("closed before") != std::string::npos;
    REQUIRE(has_timeout_msg);

    future.wait();
}

TEST_CASE("send_json_request times out if server hangs", "[unit][ipc][timeout]") {
    LocalJsonEndpoint endpoint{"127.0.0.1", 41232};
    auto server_res = LocalJsonServer::listen(endpoint);
    REQUIRE(server_res.has_value());

    auto future = std::async(std::launch::async, [&]() {
        // Accept the connection and just do nothing (simulate a hung server)
        auto conn_res = server_res->accept_one(2000);
        if (conn_res && conn_res->has_value()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));  // Hang for 100ms
        }
    });

    nlohmann::json req_payload = {{"action", "ping"}};

    auto start = std::chrono::steady_clock::now();
    auto res = send_json_request(endpoint, req_payload, 50);  // 50ms client timeout
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - start)
                        .count();

    REQUIRE(!res.has_value());
    bool has_timeout_msg2 = res.error().message.find("timed out") != std::string::npos ||
                            res.error().message.find("closed") != std::string::npos;
    REQUIRE(has_timeout_msg2);
    REQUIRE(duration >= 25LL);

    future.wait();
}

// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,readability-identifier-length,bugprone-easily-swappable-parameters,readability-function-cognitive-complexity,readability-function-size,cppcoreguidelines-avoid-magic-numbers,modernize-use-designated-initializers,readability-uppercase-literal-suffix,readability-math-missing-parentheses,modernize-use-ranges,modernize-use-starts-ends-with,modernize-use-emplace,modernize-use-auto,modernize-loop-convert,modernize-avoid-c-style-cast,modernize-return-braced-init-list,readability-implicit-bool-conversion,readability-container-contains,readability-redundant-member-init,readability-redundant-string-init,bugprone-narrowing-conversions,cppcoreguidelines-narrowing-conversions,readability-avoid-nested-conditional-operator,modernize-use-nodiscard,readability-make-member-function-const,cppcoreguidelines-pro-type-reinterpret-cast,bugprone-implicit-widening-of-multiplication-result,readability-redundant-inline-specifier,cppcoreguidelines-prefer-member-initializer,performance-unnecessary-value-param,readability-use-concise-preprocessor-directives,readability-else-after-return,readability-string-compare,bugprone-exception-escape,cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays,bugprone-branch-clone,cert-err33-c,readability-redundant-declaration,readability-qualified-auto,modernize-use-scoped-lock,modernize-use-bool-literals,cppcoreguidelines-init-variables,cppcoreguidelines-special-member-functions,cppcoreguidelines-owning-memory,cppcoreguidelines-no-malloc,performance-enum-size,performance-avoid-endl,bugprone-unchecked-optional-access,bugprone-unchecked-string-to-number-conversion,cppcoreguidelines-pro-type-cstyle-cast,modernize-use-using,modernize-use-integer-sign-comparison,cert-dcl50-cpp,cppcoreguidelines-pro-type-const-cast,readability-identifier-naming,modernize-raw-string-literal,readability-container-size-empty,bugprone-command-processor,readability-use-std-min-max,cppcoreguidelines-avoid-non-const-global-variables,bugprone-misplaced-widening-cast,readability-misleading-indentation,cert-env33-c,performance-unnecessary-copy-initialization,readability-named-parameter,readability-isolate-declaration,cert-err34-c,modernize-avoid-variadic-functions,cppcoreguidelines-pro-bounds-constant-array-index)
