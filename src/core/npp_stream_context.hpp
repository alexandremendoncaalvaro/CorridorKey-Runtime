#pragma once

#include <cuda_runtime_api.h>
#include <nppdefs.h>

namespace corridorkey::core::detail {

inline bool make_npp_stream_context(cudaStream_t stream, NppStreamContext& context) {
    if (stream == nullptr) {
        return false;
    }

    int device_id = 0;
    if (cudaGetDevice(&device_id) != cudaSuccess) {
        return false;
    }

    cudaDeviceProp properties{};
    if (cudaGetDeviceProperties(&properties, device_id) != cudaSuccess) {
        return false;
    }

    int compute_major = 0;
    int compute_minor = 0;
    if (cudaDeviceGetAttribute(&compute_major, cudaDevAttrComputeCapabilityMajor, device_id) !=
        cudaSuccess) {
        return false;
    }
    if (cudaDeviceGetAttribute(&compute_minor, cudaDevAttrComputeCapabilityMinor, device_id) !=
        cudaSuccess) {
        return false;
    }

    unsigned int stream_flags = 0;
    if (cudaStreamGetFlags(stream, &stream_flags) != cudaSuccess) {
        return false;
    }

    context = NppStreamContext{};
    context.hStream = stream;
    context.nCudaDeviceId = device_id;
    context.nMultiProcessorCount = properties.multiProcessorCount;
    context.nMaxThreadsPerMultiProcessor = properties.maxThreadsPerMultiProcessor;
    context.nMaxThreadsPerBlock = properties.maxThreadsPerBlock;
    context.nSharedMemPerBlock = properties.sharedMemPerBlock;
    context.nCudaDevAttrComputeCapabilityMajor = compute_major;
    context.nCudaDevAttrComputeCapabilityMinor = compute_minor;
    context.nStreamFlags = stream_flags;
    return true;
}

}  // namespace corridorkey::core::detail
