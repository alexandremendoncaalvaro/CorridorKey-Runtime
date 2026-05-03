#include <catch2/catch_all.hpp>
#include <corridorkey/engine.hpp>

#include "core/mlx_probe.hpp"

using namespace corridorkey;

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

TEST_CASE("auto_detect returns valid device info", "[unit][device]") {
    auto device = auto_detect();

    REQUIRE_FALSE(device.name.empty());
    REQUIRE(device.available_memory_mb >= 0);
    REQUIRE(device.backend != Backend::Auto);
}

TEST_CASE("list_devices includes at least one device", "[unit][device]") {
    auto devices = list_devices();

    REQUIRE(devices.size() >= 1);

    // Should always include a CPU-capable device
    bool has_cpu = false;
    for (const auto& d : devices) {
        if (d.backend == Backend::CPU) {
            has_cpu = true;
        }
        REQUIRE_FALSE(d.name.empty());
    }

    // If the primary device is not CPU, there should be a CPU fallback
    if (devices.front().backend != Backend::CPU) {
        REQUIRE(has_cpu);
    }

#if defined(__APPLE__)
    if (core::mlx_probe_available()) {
        bool has_mlx = false;
        for (const auto& device : devices) {
            if (device.backend == Backend::MLX) {
                has_mlx = true;
                break;
            }
        }
        REQUIRE(has_mlx);
    }
#endif
}

TEST_CASE("arm64 macOS hardware detection still exposes CoreML capability", "[unit][device]") {
#if defined(__APPLE__) && (defined(__aarch64__) || defined(__arm64__))
    auto device = auto_detect();

    REQUIRE(device.backend == Backend::CoreML);
    REQUIRE(device.name.find("Apple Silicon") != std::string::npos);
#else
    SUCCEED("Apple Silicon-specific detection is not applicable on this build.");
#endif
}

TEST_CASE("apple device listing prioritizes operational backends over diagnostic ones",
          "[unit][device]") {
#if defined(__APPLE__)
    auto devices = list_devices();
    REQUIRE_FALSE(devices.empty());
    REQUIRE(devices.front().backend != Backend::CoreML);
#else
    SUCCEED("Apple ordering is not applicable on this build.");
#endif
}

TEST_CASE("windows device listing prefers TensorRT RTX when available", "[unit][device]") {
#if defined(_WIN32)
    auto devices = list_devices();
    REQUIRE_FALSE(devices.empty());

    bool has_cpu = false;
    bool has_tensorrt = false;
    for (const auto& device : devices) {
        has_cpu = has_cpu || device.backend == Backend::CPU;
        has_tensorrt = has_tensorrt || device.backend == Backend::TensorRT;
    }
    REQUIRE(has_cpu);

    if (has_tensorrt) {
        REQUIRE(devices.front().backend == Backend::TensorRT);
    }
#else
    SUCCEED("Windows TensorRT ordering is not applicable on this build.");
#endif
}

// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,readability-identifier-length,bugprone-easily-swappable-parameters,readability-function-cognitive-complexity,readability-function-size,cppcoreguidelines-avoid-magic-numbers,modernize-use-designated-initializers,readability-uppercase-literal-suffix,readability-math-missing-parentheses,modernize-use-ranges,modernize-use-starts-ends-with,modernize-use-emplace,modernize-use-auto,modernize-loop-convert,modernize-avoid-c-style-cast,modernize-return-braced-init-list,readability-implicit-bool-conversion,readability-container-contains,readability-redundant-member-init,readability-redundant-string-init,bugprone-narrowing-conversions,cppcoreguidelines-narrowing-conversions,readability-avoid-nested-conditional-operator,modernize-use-nodiscard,readability-make-member-function-const,cppcoreguidelines-pro-type-reinterpret-cast,bugprone-implicit-widening-of-multiplication-result,readability-redundant-inline-specifier,cppcoreguidelines-prefer-member-initializer,performance-unnecessary-value-param,readability-use-concise-preprocessor-directives,readability-else-after-return,readability-string-compare,bugprone-exception-escape,cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays,bugprone-branch-clone,cert-err33-c,readability-redundant-declaration,readability-qualified-auto,modernize-use-scoped-lock,modernize-use-bool-literals,cppcoreguidelines-init-variables,cppcoreguidelines-special-member-functions,cppcoreguidelines-owning-memory,cppcoreguidelines-no-malloc,performance-enum-size,performance-avoid-endl,bugprone-unchecked-optional-access,bugprone-unchecked-string-to-number-conversion,cppcoreguidelines-pro-type-cstyle-cast,modernize-use-using,modernize-use-integer-sign-comparison,cert-dcl50-cpp,cppcoreguidelines-pro-type-const-cast,readability-identifier-naming,modernize-raw-string-literal,readability-container-size-empty,bugprone-command-processor,readability-use-std-min-max,cppcoreguidelines-avoid-non-const-global-variables,bugprone-misplaced-widening-cast,readability-misleading-indentation,cert-env33-c,performance-unnecessary-copy-initialization,readability-named-parameter,readability-isolate-declaration,cert-err34-c,modernize-avoid-variadic-functions,cppcoreguidelines-pro-bounds-constant-array-index)
