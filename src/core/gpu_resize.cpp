#include "gpu_resize.hpp"

#if defined(CORRIDORKEY_HAS_CUDA) && CORRIDORKEY_HAS_CUDA
#include <cuda_runtime_api.h>
#include <npp.h>
#include <nppi.h>
#endif

namespace corridorkey::core {

#if defined(CORRIDORKEY_HAS_CUDA) && CORRIDORKEY_HAS_CUDA

struct GpuResizeState {
    float* src_alpha_dev = nullptr;
    float* src_fg_dev = nullptr;
    float* dst_alpha_dev = nullptr;
    float* dst_fg_planar_dev = nullptr;
    float* dst_fg_interleaved_dev = nullptr;

    int current_src_width = 0;
    int current_src_height = 0;
    int current_dst_width = 0;
    int current_dst_height = 0;

    bool available = false;
    cudaStream_t stream = nullptr;

    GpuResizeState() {
        int device_count = 0;
        if (cudaGetDeviceCount(&device_count) == cudaSuccess && device_count > 0) {
            if (cudaStreamCreate(&stream) == cudaSuccess) {
                available = true;
            }
        }
    }

    ~GpuResizeState() {
        release_buffers();
        if (stream) {
            cudaStreamDestroy(stream);
        }
    }

    void release_buffers() {
        if (src_alpha_dev) cudaFree(src_alpha_dev);
        if (src_fg_dev) cudaFree(src_fg_dev);
        if (dst_alpha_dev) cudaFree(dst_alpha_dev);
        if (dst_fg_planar_dev) cudaFree(dst_fg_planar_dev);
        if (dst_fg_interleaved_dev) cudaFree(dst_fg_interleaved_dev);

        src_alpha_dev = nullptr;
        src_fg_dev = nullptr;
        dst_alpha_dev = nullptr;
        dst_fg_planar_dev = nullptr;
        dst_fg_interleaved_dev = nullptr;
    }

    bool ensure_buffers(int src_w, int src_h, int dst_w, int dst_h, bool has_fg) {
        if (src_w == current_src_width && src_h == current_src_height &&
            dst_w == current_dst_width && dst_h == current_dst_height &&
            (has_fg == (src_fg_dev != nullptr))) {
            return true;
        }

        release_buffers();

        const size_t src_pixels = static_cast<size_t>(src_w) * src_h;
        const size_t dst_pixels = static_cast<size_t>(dst_w) * dst_h;

        if (cudaMalloc(&src_alpha_dev, src_pixels * sizeof(float)) != cudaSuccess) return false;
        if (cudaMalloc(&dst_alpha_dev, dst_pixels * sizeof(float)) != cudaSuccess) return false;

        if (has_fg) {
            if (cudaMalloc(&src_fg_dev, 3 * src_pixels * sizeof(float)) != cudaSuccess)
                return false;
            if (cudaMalloc(&dst_fg_planar_dev, 3 * dst_pixels * sizeof(float)) != cudaSuccess)
                return false;
            if (cudaMalloc(&dst_fg_interleaved_dev, 3 * dst_pixels * sizeof(float)) != cudaSuccess)
                return false;
        }

        current_src_width = src_w;
        current_src_height = src_h;
        current_dst_width = dst_w;
        current_dst_height = dst_h;
        return true;
    }
};

#else

// Mock implementation when CUDA is not enabled
struct GpuResizeState {
    bool available = false;
};

#endif

GpuResizer::GpuResizer() : m_state(std::make_unique<GpuResizeState>()) {}

GpuResizer::~GpuResizer() = default;

GpuResizer::GpuResizer(GpuResizer&&) noexcept = default;
GpuResizer& GpuResizer::operator=(GpuResizer&&) noexcept = default;

bool GpuResizer::available() const {
#if defined(CORRIDORKEY_HAS_CUDA) && CORRIDORKEY_HAS_CUDA
    return m_state && m_state->available;
#else
    return false;
#endif
}

Result<void> GpuResizer::resize_planar_outputs(const float* src_alpha, const float* src_fg,
                                               int src_width, int src_height, Image dst_alpha,
                                               Image dst_fg) {
#if defined(CORRIDORKEY_HAS_CUDA) && CORRIDORKEY_HAS_CUDA
    if (!available()) {
        return Unexpected(Error{ErrorCode::HardwareNotSupported, "GPU resize is not available"});
    }

    const bool has_fg = (src_fg != nullptr && !dst_fg.empty());
    if (!m_state->ensure_buffers(src_width, src_height, dst_alpha.width, dst_alpha.height,
                                 has_fg)) {
        return Unexpected(
            Error{ErrorCode::InferenceFailed, "Failed to allocate GPU resize buffers"});
    }

    nppSetStream(m_state->stream);

    const size_t src_pixels = static_cast<size_t>(src_width) * src_height;
    const size_t dst_pixels = static_cast<size_t>(dst_alpha.width) * dst_alpha.height;

    // 1. Upload alpha
    cudaMemcpyAsync(m_state->src_alpha_dev, src_alpha, src_pixels * sizeof(float),
                    cudaMemcpyHostToDevice, m_state->stream);

    // 2. Resize alpha
    NppiSize src_size = {src_width, src_height};
    NppiRect src_roi = {0, 0, src_width, src_height};
    NppiSize dst_size = {dst_alpha.width, dst_alpha.height};
    NppiRect dst_roi = {0, 0, dst_alpha.width, dst_alpha.height};

    NppStatus status =
        nppiResize_32f_C1R(m_state->src_alpha_dev, src_width * sizeof(float), src_size, src_roi,
                           m_state->dst_alpha_dev, dst_alpha.width * sizeof(float), dst_size,
                           dst_roi, NPPI_INTER_LINEAR);

    if (status != NPP_SUCCESS) {
        return Unexpected(Error{ErrorCode::InferenceFailed, "NPP alpha resize failed"});
    }

    // 3. Download alpha
    cudaMemcpyAsync(dst_alpha.data.data(), m_state->dst_alpha_dev, dst_pixels * sizeof(float),
                    cudaMemcpyDeviceToHost, m_state->stream);

    if (has_fg) {
        // Upload foreground (3 planar channels)
        cudaMemcpyAsync(m_state->src_fg_dev, src_fg, 3 * src_pixels * sizeof(float),
                        cudaMemcpyHostToDevice, m_state->stream);

        // Resize foreground (planar)
        const Npp32f* src_ptrs[3] = {m_state->src_fg_dev, m_state->src_fg_dev + src_pixels,
                                     m_state->src_fg_dev + 2 * src_pixels};
        Npp32f* dst_ptrs[3] = {m_state->dst_fg_planar_dev, m_state->dst_fg_planar_dev + dst_pixels,
                               m_state->dst_fg_planar_dev + 2 * dst_pixels};

        status = nppiResize_32f_P3R(src_ptrs, src_width * sizeof(float), src_size, src_roi,
                                    dst_ptrs, dst_alpha.width * sizeof(float), dst_size, dst_roi,
                                    NPPI_INTER_LINEAR);

        if (status != NPP_SUCCESS) {
            return Unexpected(Error{ErrorCode::InferenceFailed, "NPP foreground resize failed"});
        }

        // Interleave planar RGB to HWC
        status = nppiCopy_32f_P3C3R((const Npp32f**)dst_ptrs, dst_alpha.width * sizeof(float),
                                    m_state->dst_fg_interleaved_dev,
                                    dst_alpha.width * 3 * sizeof(float), dst_size);

        if (status != NPP_SUCCESS) {
            return Unexpected(
                Error{ErrorCode::InferenceFailed, "NPP foreground interleave failed"});
        }

        // Download foreground interleaved
        cudaMemcpyAsync(dst_fg.data.data(), m_state->dst_fg_interleaved_dev,
                        dst_pixels * 3 * sizeof(float), cudaMemcpyDeviceToHost, m_state->stream);
    }

    // Synchronize stream since we're giving host data back directly
    cudaError_t cuda_err = cudaStreamSynchronize(m_state->stream);
    if (cuda_err != cudaSuccess) {
        return Unexpected(Error{ErrorCode::InferenceFailed, "GPU resize synchronization failed"});
    }

    return {};
#else
    (void)src_alpha;
    (void)src_fg;
    (void)src_width;
    (void)src_height;
    (void)dst_alpha;
    (void)dst_fg;
    return Unexpected(
        Error{ErrorCode::HardwareNotSupported, "CorridorKey was built without CUDA support"});
#endif
}

}  // namespace corridorkey::core
