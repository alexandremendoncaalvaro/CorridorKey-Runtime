#include <catch2/catch_all.hpp>
#include <corridorkey/engine.hpp>

#include "plugins/ofx/ofx_backend_matching.hpp"

using namespace corridorkey;
using namespace corridorkey::ofx;

namespace {

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

DeviceInfo placeholder_path_b_device() {
    // Mirrors ofx_instance.cpp create_instance: the .ofx populates a Backend::Auto
    // placeholder until the runtime server reports the real device on the first
    // prepare_session response.
    DeviceInfo device;
    device.backend = Backend::Auto;
    device.name = "Pending runtime server bootstrap";
    device.available_memory_mb = 0;
    return device;
}

DeviceInfo windows_rtx_effective_device() {
    DeviceInfo device;
    device.backend = Backend::TensorRT;
    device.name = "NVIDIA GeForce RTX 3080";
    device.available_memory_mb = 10240;
    return device;
}

}  // namespace

TEST_CASE("backend_matches_request treats Backend::Auto requested as a wildcard",
          "[unit][ofx][regression]") {
    // The Path B out-of-process refactor populates the .ofx-side DeviceInfo with
    // Backend::Auto until the runtime server reports the real backend on the
    // first prepare_session response. The candidate-selection loop in
    // ensure_engine_for_quality calls backend_matches_request to decide whether
    // to short-circuit at the first compatible candidate. Without wildcard
    // semantics for Auto, every server response is treated as a backend
    // mismatch and the loop iterates into incompatible fallback artifacts
    // (notably int8 ONNX files that crash the runtime server on Windows RTX).
    //
    // Observed in production v0.8.0 (Resolve, 2026-04-29 09:51-09:53):
    //   "Quality switch requested backend auto for corridorkey_fp16_1024.onnx
    //    but the runtime is using tensorrt." → continues to int8_1024 → server
    //   crashes (Socket closed before JSON message completed).
    REQUIRE(backend_matches_request(windows_rtx_effective_device(), placeholder_path_b_device()));
}

TEST_CASE("backend_matches_request matches identical backends (sanity)",
          "[unit][ofx][regression]") {
    DeviceInfo same_backend;
    same_backend.backend = Backend::TensorRT;
    same_backend.name = "RTX";

    REQUIRE(backend_matches_request(windows_rtx_effective_device(), same_backend));
}

TEST_CASE("backend_matches_request rejects a real backend mismatch", "[unit][ofx][regression]") {
    DeviceInfo dml_request;
    dml_request.backend = Backend::DirectML;
    dml_request.name = "DML";

    REQUIRE_FALSE(backend_matches_request(windows_rtx_effective_device(), dml_request));
}

TEST_CASE("backend_matches_request honors CPU as always-matching when explicitly requested",
          "[unit][ofx][regression]") {
    DeviceInfo cpu_request;
    cpu_request.backend = Backend::CPU;
    cpu_request.name = "Generic CPU";

    REQUIRE(backend_matches_request(windows_rtx_effective_device(), cpu_request));
}

// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,readability-identifier-length,bugprone-easily-swappable-parameters,readability-function-cognitive-complexity,readability-function-size,cppcoreguidelines-avoid-magic-numbers,modernize-use-designated-initializers,readability-uppercase-literal-suffix,readability-math-missing-parentheses,modernize-use-ranges,modernize-use-starts-ends-with,modernize-use-emplace,modernize-use-auto,modernize-loop-convert,modernize-avoid-c-style-cast,modernize-return-braced-init-list,readability-implicit-bool-conversion,readability-container-contains,readability-redundant-member-init,readability-redundant-string-init,bugprone-narrowing-conversions,cppcoreguidelines-narrowing-conversions,readability-avoid-nested-conditional-operator,modernize-use-nodiscard,readability-make-member-function-const,cppcoreguidelines-pro-type-reinterpret-cast,bugprone-implicit-widening-of-multiplication-result,readability-redundant-inline-specifier,cppcoreguidelines-prefer-member-initializer,performance-unnecessary-value-param,readability-use-concise-preprocessor-directives,readability-else-after-return,readability-string-compare,bugprone-exception-escape,cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays,bugprone-branch-clone,cert-err33-c,readability-redundant-declaration,readability-qualified-auto,modernize-use-scoped-lock,modernize-use-bool-literals,cppcoreguidelines-init-variables,cppcoreguidelines-special-member-functions,cppcoreguidelines-owning-memory,cppcoreguidelines-no-malloc,performance-enum-size,performance-avoid-endl,bugprone-unchecked-optional-access,bugprone-unchecked-string-to-number-conversion,cppcoreguidelines-pro-type-cstyle-cast,modernize-use-using,modernize-use-integer-sign-comparison,cert-dcl50-cpp,cppcoreguidelines-pro-type-const-cast,readability-identifier-naming,modernize-raw-string-literal,readability-container-size-empty,bugprone-command-processor,readability-use-std-min-max,cppcoreguidelines-avoid-non-const-global-variables,bugprone-misplaced-widening-cast,readability-misleading-indentation,cert-env33-c,performance-unnecessary-copy-initialization,readability-named-parameter,readability-isolate-declaration,cert-err34-c,modernize-avoid-variadic-functions,cppcoreguidelines-pro-bounds-constant-array-index)
