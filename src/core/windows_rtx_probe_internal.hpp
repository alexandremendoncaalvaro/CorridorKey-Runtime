#pragma once

#include <array>
#include <cstring>
#include <optional>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace corridorkey::core::detail {

#ifdef _WIN32

struct CudaDeviceIdentity {
    std::array<unsigned char, sizeof(LUID)> luid = {};
    bool has_luid = false;
    int compute_capability_major = 0;
    int compute_capability_minor = 0;
};

// TensorRT-RTX requires compute capability >= 7.5 (Turing+). The 7 / 5
// pair encodes the canonical NVIDIA SM threshold; named constants make
// the comparison readable while keeping the integer compare cheap.
inline constexpr int kTensorRtRtxMinCcMajor = 7;
inline constexpr int kTensorRtRtxMinCcMinor = 5;

inline bool compute_capability_supports_tensorrt_rtx(int major, int minor) {
    return major > kTensorRtRtxMinCcMajor ||
           (major == kTensorRtRtxMinCcMajor && minor >= kTensorRtRtxMinCcMinor);
}

inline bool luid_matches(const LUID& adapter_luid,
                         const std::array<unsigned char, sizeof(LUID)>& cuda_luid) {
    std::array<unsigned char, sizeof(LUID)> adapter_bytes = {};
    std::memcpy(adapter_bytes.data(), &adapter_luid, sizeof(LUID));
    return adapter_bytes == cuda_luid;
}

inline std::optional<CudaDeviceIdentity> find_cuda_device_for_adapter(
    const LUID& adapter_luid, const std::vector<CudaDeviceIdentity>& cuda_devices) {
    for (const auto& cuda_device : cuda_devices) {
        if (cuda_device.has_luid && luid_matches(adapter_luid, cuda_device.luid)) {
            return cuda_device;
        }
    }
    return std::nullopt;
}

#endif

}  // namespace corridorkey::core::detail
