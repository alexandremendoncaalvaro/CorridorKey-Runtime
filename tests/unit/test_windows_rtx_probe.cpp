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
