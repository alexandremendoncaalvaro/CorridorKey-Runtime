#include "gpu_prep.hpp"

#if defined(CORRIDORKEY_HAS_CUDA) && CORRIDORKEY_HAS_CUDA
#include <cuda_runtime_api.h>
#include <npp.h>
#include <nppi.h>
#endif

namespace corridorkey::core {

#if defined(CORRIDORKEY_HAS_CUDA) && CORRIDORKEY_HAS_CUDA

struct GpuPrepState {
    float* src_rgb_dev = nullptr;
    float* src_hint_dev = nullptr;
    float* resized_rgb_dev = nullptr;
    float* resized_hint_dev = nullptr;
    float* planar_dev = nullptr;

    int current_src_rgb_w = 0;
    int current_src_rgb_h = 0;
    int current_src_hint_w = 0;
    int current_src_hint_h = 0;
    int current_model_w = 0;
    int current_model_h = 0;

    bool gpu_available = false;
    cudaStream_t stream = nullptr;

    GpuPrepState() {
        int device_count = 0;
        if (cudaGetDeviceCount(&device_count) == cudaSuccess && device_count > 0) {
            if (cudaStreamCreate(&stream) == cudaSuccess) {
                gpu_available = true;
            }
        }
    }

    ~GpuPrepState() {
        release_buffers();
        if (stream) {
            cudaStreamDestroy(stream);
        }
    }

    void release_buffers() {
        if (src_rgb_dev) cudaFree(src_rgb_dev);
        if (src_hint_dev) cudaFree(src_hint_dev);
        if (resized_rgb_dev) cudaFree(resized_rgb_dev);
        if (resized_hint_dev) cudaFree(resized_hint_dev);
        if (planar_dev) cudaFree(planar_dev);

        src_rgb_dev = nullptr;
        src_hint_dev = nullptr;
        resized_rgb_dev = nullptr;
        resized_hint_dev = nullptr;
        planar_dev = nullptr;
    }

    bool ensure_buffers(int src_rgb_w, int src_rgb_h, int src_hint_w, int src_hint_h, int model_w,
                        int model_h) {
        if (src_rgb_w == current_src_rgb_w && src_rgb_h == current_src_rgb_h &&
            src_hint_w == current_src_hint_w && src_hint_h == current_src_hint_h &&
            model_w == current_model_w && model_h == current_model_h) {
            return true;
        }

        release_buffers();

        const size_t src_rgb_pixels = static_cast<size_t>(src_rgb_w) * src_rgb_h;
        const size_t src_hint_pixels = static_cast<size_t>(src_hint_w) * src_hint_h;
        const size_t model_pixels = static_cast<size_t>(model_w) * model_h;

        if (cudaMalloc(&src_rgb_dev, 3 * src_rgb_pixels * sizeof(float)) != cudaSuccess) {
            return false;
        }
        if (cudaMalloc(&src_hint_dev, src_hint_pixels * sizeof(float)) != cudaSuccess) {
            return false;
        }
        if (cudaMalloc(&resized_rgb_dev, 3 * model_pixels * sizeof(float)) != cudaSuccess) {
            return false;
        }
        if (cudaMalloc(&resized_hint_dev, model_pixels * sizeof(float)) != cudaSuccess) {
            return false;
        }
        if (cudaMalloc(&planar_dev, 4 * model_pixels * sizeof(float)) != cudaSuccess) {
            return false;
        }

        current_src_rgb_w = src_rgb_w;
        current_src_rgb_h = src_rgb_h;
        current_src_hint_w = src_hint_w;
        current_src_hint_h = src_hint_h;
        current_model_w = model_w;
        current_model_h = model_h;
        return true;
    }
};

#else

struct GpuPrepState {
    bool gpu_available = false;
};

#endif

GpuInputPrep::GpuInputPrep() : m_state(std::make_unique<GpuPrepState>()) {}

GpuInputPrep::~GpuInputPrep() = default;

GpuInputPrep::GpuInputPrep(GpuInputPrep&&) noexcept = default;
GpuInputPrep& GpuInputPrep::operator=(GpuInputPrep&&) noexcept = default;

bool GpuInputPrep::available() const {
#if defined(CORRIDORKEY_HAS_CUDA) && CORRIDORKEY_HAS_CUDA
    return m_state && m_state->gpu_available;
#else
    return false;
#endif
}

Result<void> GpuInputPrep::prepare_inputs(Image rgb, Image hint, float* planar_dst, int model_width,
                                          int model_height, const std::array<float, 3>& mean,
                                          const std::array<float, 3>& inv_stddev) {
#if defined(CORRIDORKEY_HAS_CUDA) && CORRIDORKEY_HAS_CUDA
    if (!available()) {
        return Unexpected(
            Error{ErrorCode::HardwareNotSupported, "GPU input preparation is not available"});
    }

    if (rgb.empty() || hint.empty() || rgb.channels < 3 || hint.channels < 1) {
        return Unexpected(
            Error{ErrorCode::InferenceFailed, "Invalid input images for GPU preparation"});
    }

    if (!m_state->ensure_buffers(rgb.width, rgb.height, hint.width, hint.height, model_width,
                                 model_height)) {
        return Unexpected(Error{ErrorCode::InferenceFailed, "Failed to allocate GPU prep buffers"});
    }

    nppSetStream(m_state->stream);

    const size_t model_pixels = static_cast<size_t>(model_width) * model_height;

    // 1. Upload interleaved RGB (C3) to device
    const size_t src_rgb_row_bytes = static_cast<size_t>(rgb.width) * 3 * sizeof(float);
    cudaMemcpy2DAsync(m_state->src_rgb_dev, src_rgb_row_bytes, rgb.data.data(), src_rgb_row_bytes,
                      src_rgb_row_bytes, rgb.height, cudaMemcpyHostToDevice, m_state->stream);

    // 2. Upload hint (C1) to device
    const size_t src_hint_row_bytes = static_cast<size_t>(hint.width) * sizeof(float);
    cudaMemcpy2DAsync(m_state->src_hint_dev, src_hint_row_bytes, hint.data.data(),
                      src_hint_row_bytes, src_hint_row_bytes, hint.height, cudaMemcpyHostToDevice,
                      m_state->stream);

    // 3. Resize RGB (interleaved C3) on GPU
    NppiSize src_rgb_size = {rgb.width, rgb.height};
    NppiRect src_rgb_roi = {0, 0, rgb.width, rgb.height};
    NppiSize dst_size = {model_width, model_height};
    NppiRect dst_roi = {0, 0, model_width, model_height};

    const int src_rgb_step = rgb.width * 3 * static_cast<int>(sizeof(float));
    const int dst_rgb_step = model_width * 3 * static_cast<int>(sizeof(float));

    NppStatus status = nppiResize_32f_C3R(m_state->src_rgb_dev, src_rgb_step, src_rgb_size,
                                          src_rgb_roi, m_state->resized_rgb_dev, dst_rgb_step,
                                          dst_size, dst_roi, NPPI_INTER_LINEAR);

    if (status != NPP_SUCCESS) {
        return Unexpected(Error{ErrorCode::InferenceFailed,
                                "NPP RGB resize failed with status " + std::to_string(status)});
    }

    // 4. Resize hint (C1) on GPU
    NppiSize src_hint_size = {hint.width, hint.height};
    NppiRect src_hint_roi = {0, 0, hint.width, hint.height};
    const int src_hint_step = hint.width * static_cast<int>(sizeof(float));
    const int dst_hint_step = model_width * static_cast<int>(sizeof(float));

    status = nppiResize_32f_C1R(m_state->src_hint_dev, src_hint_step, src_hint_size, src_hint_roi,
                                m_state->resized_hint_dev, dst_hint_step, dst_size, dst_roi,
                                NPPI_INTER_LINEAR);

    if (status != NPP_SUCCESS) {
        return Unexpected(Error{ErrorCode::InferenceFailed,
                                "NPP hint resize failed with status " + std::to_string(status)});
    }

    // 5. Split interleaved RGB to 3 planar channels in the output tensor
    //    dst layout: [R_plane | G_plane | B_plane | hint_plane]
    Npp32f* planar_ptrs[3] = {m_state->planar_dev, m_state->planar_dev + model_pixels,
                              m_state->planar_dev + 2 * model_pixels};

    status = nppiCopy_32f_C3P3R(m_state->resized_rgb_dev, dst_rgb_step, planar_ptrs, dst_hint_step,
                                dst_size);

    if (status != NPP_SUCCESS) {
        return Unexpected(Error{ErrorCode::InferenceFailed,
                                "NPP C3-to-P3 split failed with status " + std::to_string(status)});
    }

    // 6. Normalize each RGB plane: dst[i] = (dst[i] - mean[i]) * inv_stddev[i]
    for (int channel = 0; channel < 3; ++channel) {
        Npp32f* plane = m_state->planar_dev + static_cast<size_t>(channel) * model_pixels;

        status = nppiSubC_32f_C1IR(mean[channel], plane, dst_hint_step, dst_size);
        if (status != NPP_SUCCESS) {
            return Unexpected(Error{ErrorCode::InferenceFailed,
                                    "NPP SubC failed on channel " + std::to_string(channel)});
        }

        status = nppiMulC_32f_C1IR(inv_stddev[channel], plane, dst_hint_step, dst_size);
        if (status != NPP_SUCCESS) {
            return Unexpected(Error{ErrorCode::InferenceFailed,
                                    "NPP MulC failed on channel " + std::to_string(channel)});
        }
    }

    // 7. Copy resized hint into the 4th planar channel
    Npp32f* hint_plane = m_state->planar_dev + 3 * model_pixels;
    status = nppiCopy_32f_C1R(m_state->resized_hint_dev, dst_hint_step, hint_plane, dst_hint_step,
                              dst_size);

    if (status != NPP_SUCCESS) {
        return Unexpected(Error{ErrorCode::InferenceFailed,
                                "NPP hint copy failed with status " + std::to_string(status)});
    }

    // 8. Download the complete 4-channel planar tensor to host
    cudaMemcpyAsync(planar_dst, m_state->planar_dev, 4 * model_pixels * sizeof(float),
                    cudaMemcpyDeviceToHost, m_state->stream);

    cudaError_t cuda_err = cudaStreamSynchronize(m_state->stream);
    if (cuda_err != cudaSuccess) {
        return Unexpected(Error{ErrorCode::InferenceFailed, "GPU prep synchronization failed"});
    }

    return {};
#else
    (void)rgb;
    (void)hint;
    (void)planar_dst;
    (void)model_width;
    (void)model_height;
    (void)mean;
    (void)inv_stddev;
    return Unexpected(
        Error{ErrorCode::HardwareNotSupported, "CorridorKey was built without CUDA support"});
#endif
}

}  // namespace corridorkey::core
