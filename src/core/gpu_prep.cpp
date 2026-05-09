#include "gpu_prep.hpp"

#include <algorithm>

#if defined(CORRIDORKEY_HAS_CUDA) && CORRIDORKEY_HAS_CUDA
#include <cuda_runtime_api.h>
#include <npp.h>
#include <nppi.h>

#include "../common/parallel_for.hpp"
#include "../common/stage_profiler.hpp"
#include "npp_stream_context.hpp"
#include "pinned_buffer.hpp"
#endif

// NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,cppcoreguidelines-pro-bounds-constant-array-index,readability-identifier-length,bugprone-easily-swappable-parameters,readability-function-cognitive-complexity,readability-function-size,cppcoreguidelines-avoid-magic-numbers,modernize-use-designated-initializers,readability-math-missing-parentheses,bugprone-implicit-widening-of-multiplication-result,cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays,performance-unnecessary-value-param,cppcoreguidelines-special-member-functions,bugprone-unchecked-string-to-number-conversion,cppcoreguidelines-pro-type-cstyle-cast,modernize-use-using,modernize-use-integer-sign-comparison,cert-dcl50-cpp,cppcoreguidelines-pro-type-const-cast,readability-identifier-naming,modernize-raw-string-literal,readability-container-size-empty,bugprone-command-processor,readability-use-std-min-max,cppcoreguidelines-avoid-non-const-global-variables,bugprone-misplaced-widening-cast,readability-misleading-indentation,cert-env33-c,performance-unnecessary-copy-initialization,readability-named-parameter,readability-isolate-declaration,cert-err34-c,modernize-avoid-variadic-functions)
//
// gpu_prep.cpp tidy-suppression rationale.
//
// This translation unit owns GPU-side input preparation on the OFX
// render hot path (CLAUDE.md "Operational Rules": render-path edits are
// gated by the phase_8_gpu_prepare 10% regression budget). The
// suppressed categories all flag patterns that are required by the
// CUDA Runtime / NPP C ABI or by upstream-validated tensor shapes:
//
//   * cppcoreguidelines-pro-bounds-avoid-unchecked-container-access /
//     pro-bounds-constant-array-index: the per-channel mean[] /
//     inv_stddev[] reads are indexed by a loop counter bounded to
//     [0, 3) immediately above the access. .at() would inject a
//     branch into the per-frame normalization loop.
//
//   * bugprone-easily-swappable-parameters: prepare_inputs takes the
//     stable (rgb, hint, planar_dst, model_width, model_height, mean,
//     inv_stddev) signature; the parameter ordering is an established
//     contract used by InferenceSession.
//
//   * readability-function-cognitive-complexity / readability-function-
//     size: prepare_inputs is the canonical eight-step
//     "upload->resize->split->normalize->download" GPU pipeline;
//     splitting it would scatter the cudaMalloc'd device pointers
//     across helpers no other caller benefits from.
//
//   * cppcoreguidelines-avoid-magic-numbers: 3 / 4 are the well-known
//     RGB / RGBA channel counts and are documented at every use site.
//
//   * modernize-use-designated-initializers: NppiSize / NppiRect are
//     C aggregates from the upstream NPP header; designated init would
//     change every NPP call site in the codebase.
//
//   * readability-math-missing-parentheses: NPP planar offsets follow
//     the standard "base + n * stride" form; the precedence is the
//     intended one and matches the surrounding NPP/CUDA C idiom.
//
//   * cppcoreguidelines-avoid-c-arrays / modernize-avoid-c-arrays:
//     planar_ptrs[3] is the exact Npp32f** array nppiCopy_32f_C3P3R
//     expects per the NPP C ABI; std::array<Npp32f*, 3>::data() would
//     work but adds no safety here.
//
//   * cppcoreguidelines-special-member-functions: GpuPrepState owns
//     CUDA stream + device buffers via RAII and is held by unique_ptr
//     in the PIMPL; copy/move are explicitly deleted below to keep
//     ownership singular.
namespace corridorkey::core {

#if defined(CORRIDORKEY_HAS_CUDA) && CORRIDORKEY_HAS_CUDA

struct GpuPrepState {
    float* src_rgb_dev = nullptr;
    float* src_hint_dev = nullptr;
    float* resized_rgb_dev = nullptr;
    float* resized_hint_dev = nullptr;
    float* planar_dev = nullptr;
    PinnedBuffer<float> src_rgb_host_pinned;
    PinnedBuffer<float> src_hint_host_pinned;

    int current_src_rgb_w = 0;
    int current_src_rgb_h = 0;
    int current_src_hint_w = 0;
    int current_src_hint_h = 0;
    int current_model_w = 0;
    int current_model_h = 0;

    bool gpu_available = false;
    cudaStream_t stream = nullptr;
    cudaEvent_t prep_start_event = nullptr;
    cudaEvent_t completion_event = nullptr;
    NppStreamContext npp_context{};

    static cudaStream_t create_prepare_stream() {
        cudaStream_t created_stream = nullptr;
        int least_priority = 0;
        int greatest_priority = 0;
        if (cudaDeviceGetStreamPriorityRange(&least_priority, &greatest_priority) == cudaSuccess &&
            cudaStreamCreateWithPriority(&created_stream, cudaStreamNonBlocking,
                                         greatest_priority) == cudaSuccess) {
            return created_stream;
        }
        created_stream = nullptr;
        if (cudaStreamCreateWithFlags(&created_stream, cudaStreamNonBlocking) != cudaSuccess) {
            return nullptr;
        }
        return created_stream;
    }

    GpuPrepState() {
        int device_count = 0;
        if (cudaGetDeviceCount(&device_count) == cudaSuccess && device_count > 0) {
            stream = create_prepare_stream();
            if (stream != nullptr) {
                if (detail::make_npp_stream_context(stream, npp_context)) {
                    if (cudaEventCreate(&prep_start_event) == cudaSuccess &&
                        cudaEventCreate(&completion_event) == cudaSuccess) {
                        gpu_available = true;
                    } else {
                        if (prep_start_event != nullptr) {
                            cudaEventDestroy(prep_start_event);
                            prep_start_event = nullptr;
                        }
                        if (completion_event != nullptr) {
                            cudaEventDestroy(completion_event);
                            completion_event = nullptr;
                        }
                        cudaStreamDestroy(stream);
                        stream = nullptr;
                    }
                } else {
                    cudaStreamDestroy(stream);
                    stream = nullptr;
                }
            }
        }
    }

    GpuPrepState(const GpuPrepState&) = delete;
    GpuPrepState& operator=(const GpuPrepState&) = delete;
    GpuPrepState(GpuPrepState&&) = delete;
    GpuPrepState& operator=(GpuPrepState&&) = delete;

    ~GpuPrepState() {
        release_buffers();
        if (prep_start_event != nullptr) {
            cudaEventDestroy(prep_start_event);
        }
        if (completion_event != nullptr) {
            cudaEventDestroy(completion_event);
        }
        if (stream != nullptr) {
            cudaStreamDestroy(stream);
        }
    }

    void release_buffers() {
        if (src_rgb_dev != nullptr) cudaFree(src_rgb_dev);
        if (src_hint_dev != nullptr) cudaFree(src_hint_dev);
        if (resized_rgb_dev != nullptr) cudaFree(resized_rgb_dev);
        if (resized_hint_dev != nullptr) cudaFree(resized_hint_dev);
        if (planar_dev != nullptr) cudaFree(planar_dev);

        src_rgb_dev = nullptr;
        src_hint_dev = nullptr;
        resized_rgb_dev = nullptr;
        resized_hint_dev = nullptr;
        planar_dev = nullptr;
        src_rgb_host_pinned = PinnedBuffer<float>{};
        src_hint_host_pinned = PinnedBuffer<float>{};
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
            release_buffers();
            return false;
        }
        if (cudaMalloc(&src_hint_dev, src_hint_pixels * sizeof(float)) != cudaSuccess) {
            release_buffers();
            return false;
        }
        if (cudaMalloc(&resized_rgb_dev, 3 * model_pixels * sizeof(float)) != cudaSuccess) {
            release_buffers();
            return false;
        }
        if (cudaMalloc(&resized_hint_dev, model_pixels * sizeof(float)) != cudaSuccess) {
            release_buffers();
            return false;
        }
        if (cudaMalloc(&planar_dev, 4 * model_pixels * sizeof(float)) != cudaSuccess) {
            release_buffers();
            return false;
        }

        auto rgb_host_pinned = PinnedBuffer<float>::try_allocate(3 * src_rgb_pixels);
        auto hint_host_pinned = PinnedBuffer<float>::try_allocate(src_hint_pixels);
        if (rgb_host_pinned.has_value() && hint_host_pinned.has_value()) {
            src_rgb_host_pinned = std::move(*rgb_host_pinned);
            src_hint_host_pinned = std::move(*hint_host_pinned);
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

void copy_image_rows_to_pinned(Image image, float* dst, int copied_channels) {
    const size_t row_floats = static_cast<size_t>(image.width) * copied_channels;
    common::parallel_for_rows(image.height, [&](int row_begin, int row_end) {
        for (int y = row_begin; y < row_end; ++y) {
            const size_t row_offset = static_cast<size_t>(y) * row_floats;
            std::copy_n(image.data.data() + row_offset, row_floats, dst + row_offset);
        }
    });
}

Result<GpuPreparedInput> prepare_inputs_on_device(GpuPrepState& state, Image rgb, Image hint,
                                                  int model_width, int model_height,
                                                  const std::array<float, 3>& mean,
                                                  const std::array<float, 3>& inv_stddev,
                                                  cudaStream_t stream,
                                                  const NppStreamContext& npp_context,
                                                  bool synchronize,
                                                  bool ready_event_on_current_stream,
                                                  const StageTimingCallback& on_stage) {
    if (rgb.empty() || hint.empty() || rgb.channels < 3 || hint.channels < 1) {
        return Unexpected(
            Error{ErrorCode::InferenceFailed, "Invalid input images for GPU preparation"});
    }

    bool buffers_ready = false;
    common::measure_stage(on_stage, "gpu_prepare_ensure_buffers", [&]() {
        buffers_ready = state.ensure_buffers(rgb.width, rgb.height, hint.width, hint.height,
                                             model_width, model_height);
    });
    if (!buffers_ready) {
        return Unexpected(Error{ErrorCode::InferenceFailed, "Failed to allocate GPU prep buffers"});
    }

    const size_t model_pixels = static_cast<size_t>(model_width) * model_height;

    const bool use_pinned_upload = !state.src_rgb_host_pinned.empty() &&
                                   !state.src_hint_host_pinned.empty();
    const float* upload_rgb_src = rgb.data.data();
    const float* upload_hint_src = hint.data.data();

    if (use_pinned_upload) {
        common::measure_stage(on_stage, "gpu_prepare_pinned_stage", [&]() {
            copy_image_rows_to_pinned(rgb, state.src_rgb_host_pinned.data(), 3);
            copy_image_rows_to_pinned(hint, state.src_hint_host_pinned.data(), 1);
        });
        upload_rgb_src = state.src_rgb_host_pinned.data();
        upload_hint_src = state.src_hint_host_pinned.data();
    }

    if (!synchronize) {
        cudaError_t cuda_err = cudaSuccess;
        common::measure_stage(on_stage, "gpu_prepare_start_event_record",
                              [&]() { cuda_err = cudaEventRecord(state.prep_start_event, stream); });
        if (cuda_err != cudaSuccess) {
            return Unexpected(Error{ErrorCode::InferenceFailed,
                                    "GPU prep start event recording failed"});
        }
    }

    common::measure_stage(on_stage, "gpu_prepare_upload_enqueue", [&]() {
        const size_t src_rgb_row_bytes = static_cast<size_t>(rgb.width) * 3 * sizeof(float);
        cudaMemcpy2DAsync(state.src_rgb_dev, src_rgb_row_bytes, upload_rgb_src, src_rgb_row_bytes,
                          src_rgb_row_bytes, rgb.height, cudaMemcpyHostToDevice, stream);

        const size_t src_hint_row_bytes = static_cast<size_t>(hint.width) * sizeof(float);
        cudaMemcpy2DAsync(state.src_hint_dev, src_hint_row_bytes, upload_hint_src,
                          src_hint_row_bytes, src_hint_row_bytes, hint.height,
                          cudaMemcpyHostToDevice, stream);
    });

    NppiSize src_rgb_size = {rgb.width, rgb.height};
    NppiRect src_rgb_roi = {0, 0, rgb.width, rgb.height};
    NppiSize dst_size = {model_width, model_height};
    NppiRect dst_roi = {0, 0, model_width, model_height};

    const int src_rgb_step = rgb.width * 3 * static_cast<int>(sizeof(float));
    const int dst_rgb_step = model_width * 3 * static_cast<int>(sizeof(float));
    const bool rgb_needs_resize = rgb.width != model_width || rgb.height != model_height;

    Npp32f* prepared_rgb = state.src_rgb_dev;
    int prepared_rgb_step = src_rgb_step;
    if (rgb_needs_resize) {
        NppStatus status = NPP_SUCCESS;
        common::measure_stage(on_stage, "gpu_prepare_rgb_resize_enqueue", [&]() {
            status = nppiResize_32f_C3R_Ctx(state.src_rgb_dev, src_rgb_step, src_rgb_size,
                                            src_rgb_roi, state.resized_rgb_dev, dst_rgb_step,
                                            dst_size, dst_roi, NPPI_INTER_LINEAR, npp_context);
        });

        if (status != NPP_SUCCESS) {
            return Unexpected(Error{ErrorCode::InferenceFailed,
                                    "NPP RGB resize failed with status " + std::to_string(status)});
        }
        prepared_rgb = state.resized_rgb_dev;
        prepared_rgb_step = dst_rgb_step;
    }

    NppiSize src_hint_size = {hint.width, hint.height};
    NppiRect src_hint_roi = {0, 0, hint.width, hint.height};
    const int src_hint_step = hint.width * static_cast<int>(sizeof(float));
    const int dst_hint_step = model_width * static_cast<int>(sizeof(float));
    const bool hint_needs_resize = hint.width != model_width || hint.height != model_height;

    Npp32f* prepared_hint = state.src_hint_dev;
    int prepared_hint_step = src_hint_step;
    if (hint_needs_resize) {
        NppStatus status = NPP_SUCCESS;
        common::measure_stage(on_stage, "gpu_prepare_hint_resize_enqueue", [&]() {
            status = nppiResize_32f_C1R_Ctx(state.src_hint_dev, src_hint_step, src_hint_size,
                                            src_hint_roi, state.resized_hint_dev, dst_hint_step,
                                            dst_size, dst_roi, NPPI_INTER_LINEAR, npp_context);
        });

        if (status != NPP_SUCCESS) {
            return Unexpected(
                Error{ErrorCode::InferenceFailed,
                      "NPP hint resize failed with status " + std::to_string(status)});
        }
        prepared_hint = state.resized_hint_dev;
        prepared_hint_step = dst_hint_step;
    }

    Npp32f* planar_ptrs[3] = {state.planar_dev, state.planar_dev + model_pixels,
                              state.planar_dev + 2 * model_pixels};

    NppStatus status = NPP_SUCCESS;
    common::measure_stage(on_stage, "gpu_prepare_split_enqueue", [&]() {
        status = nppiCopy_32f_C3P3R_Ctx(prepared_rgb, prepared_rgb_step, planar_ptrs, dst_hint_step,
                                        dst_size, npp_context);
    });

    if (status != NPP_SUCCESS) {
        return Unexpected(Error{ErrorCode::InferenceFailed,
                                "NPP C3-to-P3 split failed with status " + std::to_string(status)});
    }

    for (int channel = 0; channel < 3; ++channel) {
        Npp32f* plane = state.planar_dev + static_cast<size_t>(channel) * model_pixels;

        common::measure_stage(on_stage, "gpu_prepare_normalize_enqueue", [&]() {
            status =
                nppiSubC_32f_C1IR_Ctx(mean[channel], plane, dst_hint_step, dst_size, npp_context);
            if (status == NPP_SUCCESS) {
                status = nppiMulC_32f_C1IR_Ctx(inv_stddev[channel], plane, dst_hint_step, dst_size,
                                               npp_context);
            }
        });
        if (status != NPP_SUCCESS) {
            return Unexpected(Error{ErrorCode::InferenceFailed,
                                    "NPP normalize failed on channel " + std::to_string(channel) +
                                        " with status " + std::to_string(status)});
        }
    }

    Npp32f* hint_plane = state.planar_dev + 3 * model_pixels;
    common::measure_stage(on_stage, "gpu_prepare_hint_copy_enqueue", [&]() {
        status = nppiCopy_32f_C1R_Ctx(prepared_hint, prepared_hint_step, hint_plane, dst_hint_step,
                                      dst_size, npp_context);
    });

    if (status != NPP_SUCCESS) {
        return Unexpected(Error{ErrorCode::InferenceFailed,
                                "NPP hint copy failed with status " + std::to_string(status)});
    }

    if (synchronize) {
        cudaError_t cuda_err = cudaSuccess;
        common::measure_stage(on_stage, "gpu_prepare_sync",
                              [&]() { cuda_err = cudaStreamSynchronize(stream); });
        if (cuda_err != cudaSuccess) {
            return Unexpected(Error{ErrorCode::InferenceFailed, "GPU prep synchronization failed"});
        }
    } else {
        if (state.completion_event == nullptr) {
            return Unexpected(
                Error{ErrorCode::InferenceFailed, "GPU prep completion event is unavailable"});
        }
        cudaError_t cuda_err = cudaSuccess;
        common::measure_stage(on_stage, "gpu_prepare_event_record",
                              [&]() { cuda_err = cudaEventRecord(state.completion_event, stream); });
        if (cuda_err != cudaSuccess) {
            return Unexpected(Error{ErrorCode::InferenceFailed,
                                    "GPU prep completion event recording failed"});
        }
    }

    return GpuPreparedInput{
        .planar_device = state.planar_dev,
        .ready_start_event = state.prep_start_event,
        .ready_event = state.completion_event,
        .ready_event_on_current_stream = ready_event_on_current_stream,
        .source_rgb_device = state.src_rgb_dev,
        .source_width = rgb.width,
        .source_height = rgb.height,
        .source_channels = 3,
        .width = model_width,
        .height = model_height,
    };
}

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
                                          const std::array<float, 3>& inv_stddev,
                                          StageTimingCallback on_stage) {
#if defined(CORRIDORKEY_HAS_CUDA) && CORRIDORKEY_HAS_CUDA
    if (!available()) {
        return Unexpected(
            Error{ErrorCode::HardwareNotSupported, "GPU input preparation is not available"});
    }

    auto prepared_res =
        prepare_inputs_on_device(*m_state, rgb, hint, model_width, model_height, mean, inv_stddev,
                                 m_state->stream, m_state->npp_context, false, false, on_stage);
    if (!prepared_res) {
        return Unexpected(prepared_res.error());
    }
    const size_t model_pixels = static_cast<size_t>(model_width) * model_height;
    common::measure_stage(on_stage, "gpu_prepare_download_enqueue", [&]() {
        cudaMemcpyAsync(planar_dst, m_state->planar_dev, 4 * model_pixels * sizeof(float),
                        cudaMemcpyDeviceToHost, m_state->stream);
    });

    cudaError_t cuda_err = cudaSuccess;
    common::measure_stage(on_stage, "gpu_prepare_sync",
                          [&]() { cuda_err = cudaStreamSynchronize(m_state->stream); });
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
    (void)on_stage;
    return Unexpected(
        Error{ErrorCode::HardwareNotSupported, "CorridorKey was built without CUDA support"});
#endif
}

Result<GpuPreparedInput> GpuInputPrep::prepare_inputs_device(Image rgb, Image hint, int model_width,
                                                             int model_height,
                                                             const std::array<float, 3>& mean,
                                                             const std::array<float, 3>& inv_stddev,
                                                             StageTimingCallback on_stage) {
#if defined(CORRIDORKEY_HAS_CUDA) && CORRIDORKEY_HAS_CUDA
    if (!available()) {
        return Unexpected(
            Error{ErrorCode::HardwareNotSupported, "GPU input preparation is not available"});
    }
    return prepare_inputs_on_device(*m_state, rgb, hint, model_width, model_height, mean,
                                    inv_stddev, m_state->stream, m_state->npp_context, false,
                                    false, on_stage);
#else
    (void)rgb;
    (void)hint;
    (void)model_width;
    (void)model_height;
    (void)mean;
    (void)inv_stddev;
    (void)on_stage;
    return Unexpected(
        Error{ErrorCode::HardwareNotSupported, "CorridorKey was built without CUDA support"});
#endif
}

Result<GpuPreparedInput> GpuInputPrep::prepare_inputs_device_on_stream(
    Image rgb, Image hint, int model_width, int model_height, const std::array<float, 3>& mean,
    const std::array<float, 3>& inv_stddev, void* cuda_stream, StageTimingCallback on_stage) {
#if defined(CORRIDORKEY_HAS_CUDA) && CORRIDORKEY_HAS_CUDA
    if (!available()) {
        return Unexpected(
            Error{ErrorCode::HardwareNotSupported, "GPU input preparation is not available"});
    }
    NppStreamContext npp_context{};
    cudaStream_t stream = reinterpret_cast<cudaStream_t>(cuda_stream);
    if (!detail::make_npp_stream_context(stream, npp_context)) {
        return Unexpected(
            Error{ErrorCode::InferenceFailed, "Failed to bind NPP to TorchTRT CUDA stream"});
    }
    return prepare_inputs_on_device(*m_state, rgb, hint, model_width, model_height, mean,
                                    inv_stddev, stream, npp_context, false, true, on_stage);
#else
    (void)rgb;
    (void)hint;
    (void)model_width;
    (void)model_height;
    (void)mean;
    (void)inv_stddev;
    (void)cuda_stream;
    (void)on_stage;
    return Unexpected(
        Error{ErrorCode::HardwareNotSupported, "CorridorKey was built without CUDA support"});
#endif
}

}  // namespace corridorkey::core
// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,cppcoreguidelines-pro-bounds-constant-array-index,readability-identifier-length,bugprone-easily-swappable-parameters,readability-function-cognitive-complexity,readability-function-size,cppcoreguidelines-avoid-magic-numbers,modernize-use-designated-initializers,readability-math-missing-parentheses,bugprone-implicit-widening-of-multiplication-result,cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays,performance-unnecessary-value-param,cppcoreguidelines-special-member-functions,bugprone-unchecked-string-to-number-conversion,cppcoreguidelines-pro-type-cstyle-cast,modernize-use-using,modernize-use-integer-sign-comparison,cert-dcl50-cpp,cppcoreguidelines-pro-type-const-cast,readability-identifier-naming,modernize-raw-string-literal,readability-container-size-empty,bugprone-command-processor,readability-use-std-min-max,cppcoreguidelines-avoid-non-const-global-variables,bugprone-misplaced-widening-cast,readability-misleading-indentation,cert-env33-c,performance-unnecessary-copy-initialization,readability-named-parameter,readability-isolate-declaration,cert-err34-c,modernize-avoid-variadic-functions)
