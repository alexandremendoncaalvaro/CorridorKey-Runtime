#include <catch2/catch_all.hpp>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "core/windows_rtx_probe_internal.hpp"
#include "plugins/ofx/ofx_runtime_client.hpp"
#include "plugins/ofx/ofx_shared.hpp"

using namespace corridorkey;

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

std::string read_repo_source_file(const std::filesystem::path& relative_path) {
    const auto full_path = std::filesystem::path(PROJECT_ROOT) / relative_path;
    std::ifstream stream(full_path);
    REQUIRE(stream.is_open());
    return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}

#if defined(_WIN32)
std::array<unsigned char, sizeof(LUID)> bytes_from_luid(const LUID& luid) {
    std::array<unsigned char, sizeof(LUID)> bytes = {};
    std::memcpy(bytes.data(), &luid, sizeof(LUID));
    return bytes;
}
#endif

}  // namespace

TEST_CASE("windows RTX probe matches DXGI adapters to CUDA devices by LUID",
          "[unit][windows][regression]") {
#if defined(_WIN32)
    LUID adapter_luid{};
    adapter_luid.LowPart = 0x11223344;
    adapter_luid.HighPart = 0x12345678;

    LUID other_luid{};
    other_luid.LowPart = 0x21222324;
    other_luid.HighPart = 0x31323334;

    corridorkey::core::detail::CudaDeviceIdentity other_device;
    other_device.luid = bytes_from_luid(other_luid);
    other_device.has_luid = true;
    other_device.compute_capability_major = 8;
    other_device.compute_capability_minor = 6;

    corridorkey::core::detail::CudaDeviceIdentity matching_device;
    matching_device.luid = bytes_from_luid(adapter_luid);
    matching_device.has_luid = true;
    matching_device.compute_capability_major = 8;
    matching_device.compute_capability_minor = 6;

    const std::vector<corridorkey::core::detail::CudaDeviceIdentity> cuda_devices = {
        other_device,
        matching_device,
    };

    auto resolved =
        corridorkey::core::detail::find_cuda_device_for_adapter(adapter_luid, cuda_devices);
    REQUIRE(resolved.has_value());
    CHECK(resolved->compute_capability_major == 8);
    CHECK(resolved->compute_capability_minor == 6);
#else
    SUCCEED("Windows-only probe matching is not applicable on this build.");
#endif
}

TEST_CASE("windows RTX probe disables TensorRT RTX when no CUDA device matches the adapter LUID",
          "[unit][windows][regression]") {
#if defined(_WIN32)
    LUID adapter_luid{};
    adapter_luid.LowPart = 0x01020304;
    adapter_luid.HighPart = 0x05060708;

    LUID other_luid{};
    other_luid.LowPart = 0x11121314;
    other_luid.HighPart = 0x21222324;

    corridorkey::core::detail::CudaDeviceIdentity cuda_device;
    cuda_device.luid = bytes_from_luid(other_luid);
    cuda_device.has_luid = true;
    cuda_device.compute_capability_major = 8;
    cuda_device.compute_capability_minor = 9;

    auto resolved = corridorkey::core::detail::find_cuda_device_for_adapter(
        adapter_luid, std::vector<corridorkey::core::detail::CudaDeviceIdentity>{cuda_device});
    CHECK_FALSE(resolved.has_value());
#else
    SUCCEED("Windows-only probe matching is not applicable on this build.");
#endif
}

TEST_CASE("windows RTX probe requires compute capability 7.5 or newer",
          "[unit][windows][regression]") {
#if defined(_WIN32)
    CHECK_FALSE(corridorkey::core::detail::compute_capability_supports_tensorrt_rtx(7, 4));
    CHECK(corridorkey::core::detail::compute_capability_supports_tensorrt_rtx(7, 5));
    CHECK(corridorkey::core::detail::compute_capability_supports_tensorrt_rtx(8, 0));
#else
    SUCCEED("Windows-only compute capability gating is not applicable on this build.");
#endif
}

TEST_CASE("OFX unrestricted quality attempts use cached runtime capabilities",
          "[unit][ofx][regression]") {
    corridorkey::ofx::InstanceData data;
    data.runtime_capabilities.platform = "windows";
    data.runtime_capabilities.supported_backends = {Backend::TensorRT, Backend::CPU};

    const DeviceInfo tensor_rt{"RTX 3080", 10240, Backend::TensorRT};
    const DeviceInfo cpu{"Generic CPU", 0, Backend::CPU};

    CHECK(corridorkey::ofx::allow_unrestricted_quality_attempt_for_request(
        data, corridorkey::ofx::kQualityMaximum, tensor_rt));
    CHECK_FALSE(corridorkey::ofx::allow_unrestricted_quality_attempt_for_request(
        data, corridorkey::ofx::kQualityAuto, tensor_rt));
    CHECK_FALSE(corridorkey::ofx::allow_unrestricted_quality_attempt_for_request(
        data, corridorkey::ofx::kQualityMaximum, cpu));
}

TEST_CASE("windows RTX probe source stays shell free", "[unit][windows][regression]") {
    const auto source = read_repo_source_file("src/core/windows_rtx_probe.cpp");
    CHECK(source.find("_popen(") == std::string::npos);
    CHECK(source.find("nvidia-smi") == std::string::npos);
}

TEST_CASE("OFX quality switching avoids runtime capability probing in the hot path",
          "[unit][ofx][regression]") {
    const auto source = read_repo_source_file("src/plugins/ofx/ofx_instance.cpp");
    CHECK(source.find("runtime_optimization_profile_for_device(runtime_capabilities(),") ==
          std::string::npos);
}

// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,readability-identifier-length,bugprone-easily-swappable-parameters,readability-function-cognitive-complexity,readability-function-size,cppcoreguidelines-avoid-magic-numbers,modernize-use-designated-initializers,readability-uppercase-literal-suffix,readability-math-missing-parentheses,modernize-use-ranges,modernize-use-starts-ends-with,modernize-use-emplace,modernize-use-auto,modernize-loop-convert,modernize-avoid-c-style-cast,modernize-return-braced-init-list,readability-implicit-bool-conversion,readability-container-contains,readability-redundant-member-init,readability-redundant-string-init,bugprone-narrowing-conversions,cppcoreguidelines-narrowing-conversions,readability-avoid-nested-conditional-operator,modernize-use-nodiscard,readability-make-member-function-const,cppcoreguidelines-pro-type-reinterpret-cast,bugprone-implicit-widening-of-multiplication-result,readability-redundant-inline-specifier,cppcoreguidelines-prefer-member-initializer,performance-unnecessary-value-param,readability-use-concise-preprocessor-directives,readability-else-after-return,readability-string-compare,bugprone-exception-escape,cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays,bugprone-branch-clone,cert-err33-c,readability-redundant-declaration,readability-qualified-auto,modernize-use-scoped-lock,modernize-use-bool-literals,cppcoreguidelines-init-variables,cppcoreguidelines-special-member-functions,cppcoreguidelines-owning-memory,cppcoreguidelines-no-malloc,performance-enum-size,performance-avoid-endl,bugprone-unchecked-optional-access,bugprone-unchecked-string-to-number-conversion,cppcoreguidelines-pro-type-cstyle-cast,modernize-use-using,modernize-use-integer-sign-comparison,cert-dcl50-cpp,cppcoreguidelines-pro-type-const-cast,readability-identifier-naming,modernize-raw-string-literal,readability-container-size-empty,bugprone-command-processor,readability-use-std-min-max,cppcoreguidelines-avoid-non-const-global-variables,bugprone-misplaced-widening-cast,readability-misleading-indentation,cert-env33-c,performance-unnecessary-copy-initialization,readability-named-parameter,readability-isolate-declaration,cert-err34-c,modernize-avoid-variadic-functions,cppcoreguidelines-pro-bounds-constant-array-index)
