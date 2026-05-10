#include "torch_trt_session.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <optional>
#include <regex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

// Pulling individual c10/torch headers piecemeal triggers CUDA include
// resolution that the vendored torchtrt-windows tree intentionally
// stubs out (the .ts is loaded by name and TensorRT plugins register
// themselves at LoadLibrary time, so the application never directly
// touches cuda_runtime_api.h) unless the API is surfaced through libtorch
// itself. Keep this TU on the libtorch CUDA abstractions: stream guards and
// events give us the same pinned-copy/copy-stream shape used by the Python
// engine without linking application code directly against hand-written CUDA
// kernels.
#include <ATen/cuda/CUDAContext.h>
#include <ATen/cuda/CUDAEvent.h>
#include <ATen/cuda/CUDAGraph.h>
#include <ATen/ops/conv2d.h>
#include <ATen/ops/max_pool2d.h>
#include <ATen/ops/replication_pad2d.h>
#include <ATen/ops/upsample_bicubic2d.h>
#include <ATen/ops/upsample_bilinear2d.h>
#include <c10/core/InferenceMode.h>
#include <c10/cuda/CUDAGuard.h>
#include <torch/cuda.h>
#include <torch/script.h>

#include <nlohmann/json.hpp>

#include "common/stage_profiler.hpp"
#include "core/model_input_normalization.hpp"
#include "core/postprocess_policy.hpp"
#if defined(CORRIDORKEY_HAS_CUDA) && CORRIDORKEY_HAS_CUDA
#include <cuda_runtime_api.h>
#include <npp.h>
#include <nppi.h>

#include "npp_stream_context.hpp"
#endif
// Strategy C, Sprint 1 PR 1 follow-up: the runtime DLL arming
// (AddDllDirectory + LoadLibraryEx of torchtrt.dll +
// corridorkey_torchtrt.dll) lives in a torch-free TU compiled into
// corridorkey_core, so the base runtime can prepare the loader before
// triggering the delay-load of this DLL. Calling arm_torchtrt_runtime
// from inside this TU would defeat the indirection because reaching
// any symbol here implies the DLL is already resolved.

namespace corridorkey::core {

namespace {

std::optional<int> resolution_from_filename(const std::filesystem::path& path) {
    // Fixed TorchTRT engines carry a trailing resolution token; dynamic
    // TorchScript artifacts intentionally do not.
    static const std::regex pattern(R"(.*_(\d+)\.ts$)");
    std::smatch match;
    auto filename = path.filename().string();
    if (!std::regex_match(filename, match, pattern) || match.size() != 2) {
        return std::nullopt;
    }
    try {
        return std::stoi(match.str(1));
    } catch (...) {
        return std::nullopt;
    }
}

// Splits the IValue returned by torch::jit::Module::forward into the
// (alpha, foreground) tensor pair our pipeline expects. Returns an empty
// pair on error so the caller can pick the right Error message; both
// tensors stay default-constructed (.defined() == false) on failure.
struct AlphaFgTensors {
    torch::Tensor alpha;
    torch::Tensor foreground;
};

std::optional<AlphaFgTensors> split_forward_output(const torch::IValue& raw_out) {
    if (raw_out.isTuple()) {
        const auto& elements = raw_out.toTuple()->elements();
        if (elements.empty()) {
            return std::nullopt;
        }
        AlphaFgTensors result;
        result.alpha = elements.at(0).toTensor();
        if (elements.size() > 1) {
            result.foreground = elements.at(1).toTensor();
        }
        return result;
    }
    if (raw_out.isTensor()) {
        return AlphaFgTensors{.alpha = raw_out.toTensor(), .foreground = {}};
    }
    return std::nullopt;
}

bool tensor_has_shape(const torch::Tensor& tensor, int channels, int width, int height) {
    return tensor.defined() && tensor.dim() == 4 && tensor.size(0) == 1 &&
           tensor.size(1) == channels && tensor.size(2) == height && tensor.size(3) == width;
}

constexpr int kDynamicInputAlignment = 32;
constexpr std::size_t kPinnedHostRingSize = 3;
constexpr std::string_view kExternalPosMetaName = "corridorkey.external_pos.v1.json";
constexpr std::string_view kExternalPosDataName = "corridorkey.external_pos.v1.fp32";

enum class OutputResizeFilter { Bilinear, Lanczos };

std::optional<double> synchronize_cuda_stream_marker(c10::cuda::CUDAStream stream,
                                                     const StageTimingCallback& on_stage,
                                                     std::string_view stage_name);
void emit_stage_timing(const StageTimingCallback& on_stage, std::string_view name,
                       double total_ms);

void emit_cuda_event_elapsed(const StageTimingCallback& on_stage, std::string_view name,
                             void* start_event, void* stop_event) {
    if (!on_stage || start_event == nullptr || stop_event == nullptr) {
        return;
    }
#if defined(CORRIDORKEY_HAS_CUDA) && CORRIDORKEY_HAS_CUDA
    float elapsed_ms = 0.0F;
    const auto status =
        cudaEventElapsedTime(&elapsed_ms, reinterpret_cast<cudaEvent_t>(start_event),
                             reinterpret_cast<cudaEvent_t>(stop_event));
    if (status == cudaSuccess) {
        emit_stage_timing(on_stage, name, static_cast<double>(elapsed_ms));
    }
#else
    (void)name;
#endif
}

int round_up_to_multiple(int value, int multiple) {
    return ((value + multiple - 1) / multiple) * multiple;
}

Result<void> wait_for_external_cuda_event(void* input_ready_event, void* input_ready_start_event,
                                          bool input_ready_event_on_current_stream,
                                          const StageTimingCallback& on_stage) {
    if (input_ready_event == nullptr) {
        return {};
    }
#if defined(CORRIDORKEY_HAS_CUDA) && CORRIDORKEY_HAS_CUDA
    cudaError_t status = cudaSuccess;
    const auto current_stream = c10::cuda::getCurrentCUDAStream();
    if (input_ready_event_on_current_stream) {
        emit_stage_timing(on_stage, "torchtrt_input_current_stream_event", 0.0);
    } else {
        common::measure_stage(on_stage, "torchtrt_input_wait_event_enqueue", [&]() {
            status = cudaStreamWaitEvent(current_stream.stream(),
                                         reinterpret_cast<cudaEvent_t>(input_ready_event), 0);
        });
        if (status != cudaSuccess) {
            return Unexpected<Error>{
                Error{.code = ErrorCode::InferenceFailed,
                      .message = "TorchTRT failed to wait for prepared CUDA input event"}};
        }
    }
    emit_stage_timing(on_stage, "torchtrt_input_ready_wait", 0.0);
    if (input_ready_start_event != nullptr) {
        emit_stage_timing(on_stage, "gpu_prepare_wait_over_device", 0.0);
    }
    return {};
#else
    (void)input_ready_start_event;
    (void)input_ready_event_on_current_stream;
    (void)on_stage;
    return Unexpected<Error>{
        Error{.code = ErrorCode::HardwareNotSupported,
              .message = "TorchTRT CUDA input event was provided without CUDA support"}};
#endif
}

struct DynamicPadding {
    int top = 0;
    int left = 0;
    int height = 0;
    int width = 0;
};

DynamicPadding dynamic_padding_for_input(int width, int height, bool dynamic_resolution) {
    if (!dynamic_resolution) {
        return {.height = height, .width = width};
    }
    const int padded_width = round_up_to_multiple(width, kDynamicInputAlignment);
    const int padded_height = round_up_to_multiple(height, kDynamicInputAlignment);
    return {
        .top = (padded_height - height) / 2,
        .left = (padded_width - width) / 2,
        .height = padded_height,
        .width = padded_width,
    };
}

torch::Tensor allocate_host_tensor(const std::vector<int64_t>& shape) {
    const auto base_options = torch::TensorOptions().dtype(torch::kFloat32).device(torch::kCPU);
    try {
        return torch::empty(shape, base_options.pinned_memory(true));
    } catch (const c10::Error&) {
        return torch::empty(shape, base_options);
    }
}

torch::Tensor allocate_host_input_tensor(int height, int width) {
    return allocate_host_tensor({1, 4, height, width});
}

class PinnedHostTensorRing {
   public:
    torch::Tensor acquire(int64_t element_count) {
        auto& slot = m_buffers[m_cursor];
        m_cursor = (m_cursor + 1U) % m_buffers.size();
        if (!slot.defined() || slot.numel() < element_count) {
            slot = allocate_host_tensor({element_count});
        }
        if (slot.numel() == element_count) {
            return slot;
        }
        return slot.narrow(0, 0, element_count);
    }

   private:
    std::array<torch::Tensor, kPinnedHostRingSize> m_buffers{};
    std::size_t m_cursor = 0;
};

// NOLINTBEGIN(bugprone-easily-swappable-parameters) - index and size match the reflection formula.
int reflect_index(int index, int size) {
    if (size <= 1) {
        return 0;
    }
    const int period = (2 * size) - 2;
    int reflected = index % period;
    if (reflected < 0) {
        reflected += period;
    }
    if (reflected >= size) {
        reflected = period - reflected;
    }
    return reflected;
}
// NOLINTEND(bugprone-easily-swappable-parameters)

torch::Tensor crop_output_nchw(const torch::Tensor& tensor_cuda, int output_width,
                               int output_height, int pad_top, int pad_left) {
    return tensor_cuda.detach()
        .narrow(2, pad_top, output_height)
        .narrow(3, pad_left, output_width)
        .to(torch::kFloat32)
        .contiguous();
}

void resize_output_bilinear_if_needed(torch::Tensor& tensor_cuda, int current_width,
                                      int current_height, int output_width, int output_height) {
    if (current_width == output_width && current_height == output_height) {
        return;
    }
    const std::vector<int64_t> output_size{output_height, output_width};
    tensor_cuda =
        at::upsample_bilinear2d(tensor_cuda, output_size, false, std::nullopt, std::nullopt);
}

#if defined(CORRIDORKEY_HAS_CUDA) && CORRIDORKEY_HAS_CUDA
void resize_output_lanczos_if_needed(torch::Tensor& tensor_cuda, int current_width,
                                     int current_height, int output_width, int output_height) {
    if (current_width == output_width && current_height == output_height) {
        return;
    }
    tensor_cuda = tensor_cuda.contiguous();
    const int channels = static_cast<int>(tensor_cuda.size(1));
    auto resized =
        torch::empty({1, channels, output_height, output_width}, tensor_cuda.options());
    const auto stream = c10::cuda::getCurrentCUDAStream();
    NppStreamContext npp_context{};
    if (!detail::make_npp_stream_context(stream.stream(), npp_context)) {
        const std::vector<int64_t> output_size{output_height, output_width};
        tensor_cuda =
            at::upsample_bicubic2d(tensor_cuda, output_size, false, std::nullopt, std::nullopt);
        return;
    }

    NppiSize src_size = {current_width, current_height};
    NppiRect src_roi = {0, 0, current_width, current_height};
    NppiSize dst_size = {output_width, output_height};
    NppiRect dst_roi = {0, 0, output_width, output_height};
    const int src_step = current_width * static_cast<int>(sizeof(float));
    const int dst_step = output_width * static_cast<int>(sizeof(float));
    auto* src_base = tensor_cuda.data_ptr<float>();
    auto* dst_base = resized.data_ptr<float>();
    const auto src_plane = static_cast<std::ptrdiff_t>(current_width) * current_height;
    const auto dst_plane = static_cast<std::ptrdiff_t>(output_width) * output_height;
    for (int channel = 0; channel < channels; ++channel) {
        const NppStatus status = nppiResize_32f_C1R_Ctx(
            src_base + (static_cast<std::ptrdiff_t>(channel) * src_plane), src_step, src_size,
            src_roi, dst_base + (static_cast<std::ptrdiff_t>(channel) * dst_plane), dst_step,
            dst_size, dst_roi, NPPI_INTER_LANCZOS, npp_context);
        if (status != NPP_SUCCESS) {
            const std::vector<int64_t> output_size{output_height, output_width};
            tensor_cuda =
                at::upsample_bicubic2d(tensor_cuda, output_size, false, std::nullopt, std::nullopt);
            return;
        }
    }
    tensor_cuda = resized;
}
#endif

void resize_output_if_needed(torch::Tensor& tensor_cuda, int current_width, int current_height,
                             int output_width, int output_height, OutputResizeFilter filter) {
#if defined(CORRIDORKEY_HAS_CUDA) && CORRIDORKEY_HAS_CUDA
    if (filter == OutputResizeFilter::Lanczos) {
        resize_output_lanczos_if_needed(tensor_cuda, current_width, current_height, output_width,
                                        output_height);
        return;
    }
#else
    (void)filter;
#endif
    resize_output_bilinear_if_needed(tensor_cuda, current_width, current_height, output_width,
                                     output_height);
}

torch::Tensor flatten_nchw(const torch::Tensor& tensor_cuda) {
    return tensor_cuda.contiguous().view({-1});
}

struct ExternalPosState {
    bool enabled = false;
    int channels = 0;
    int source_grid = 0;
    int patch_stride_height = 0;
    int patch_stride_width = 0;
    torch::Tensor base_cuda_float;
    torch::Tensor cached_grid;
    int cached_input_width = 0;
    int cached_input_height = 0;
    torch::Dtype cached_dtype = torch::kFloat32;
};

struct ForwardGraphState {
    bool enabled = false;
    bool disabled = false;
    bool warmed = false;
    bool captured = false;
    int input_width = 0;
    int input_height = 0;
    torch::Dtype input_dtype = torch::kFloat32;
    torch::Tensor static_input;
    AlphaFgTensors static_outputs;
    std::string fallback_stage;
    at::cuda::CUDAGraph graph;
};

bool torchtrt_cuda_graph_requested() {
    if (const char* value = std::getenv("CORRIDORKEY_TORCHTRT_CUDA_GRAPH"); value != nullptr) {
        return std::string_view(value) == "1";
    }
    if (const char* value = std::getenv("CORRIDORKEY_TRT_CUDA_GRAPH"); value != nullptr) {
        return std::string_view(value) == "1";
    }
    return false;
}

bool torchtrt_direct_forward_sync_timing_requested() {
    const char* value = std::getenv("CORRIDORKEY_TORCHTRT_FORWARD_SYNC_TIMING");
    return value != nullptr && std::string_view(value) == "1";
}

bool artifact_contains_true_torchtrt_marker(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return false;
    }
    constexpr std::array<std::string_view, 2> markers = {
        "torch_tensorrt/dynamo/runtime",
        "torch/classes/tensorrt.py",
    };
    constexpr std::size_t kBufferSize = 1024U * 1024U;
    constexpr std::size_t kOverlap = 128U;
    std::string carry;
    std::string buffer(kBufferSize, '\0');
    while (input) {
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto count = input.gcount();
        if (count <= 0) {
            break;
        }
        std::string chunk = carry;
        chunk.append(buffer.data(), static_cast<std::size_t>(count));
        for (std::string_view marker : markers) {
            if (chunk.find(marker) != std::string::npos) {
                return true;
            }
        }
        if (chunk.size() > kOverlap) {
            carry = chunk.substr(chunk.size() - kOverlap);
        } else {
            carry = std::move(chunk);
        }
    }
    return false;
}

void reset_forward_graph(ForwardGraphState& state) {
    state.graph.reset();
    state.warmed = false;
    state.captured = false;
    state.static_outputs = {};
}

void emit_graph_event(const StageTimingCallback& on_stage, std::string_view name) {
    if (!on_stage) {
        return;
    }
    on_stage(StageTiming{std::string{name}, 0.0, 1, 1});
}

void set_graph_fallback(ForwardGraphState& state, std::string name) {
    state.fallback_stage = std::move(name);
}

void disable_forward_graph(ForwardGraphState& state, std::string reason) {
    reset_forward_graph(state);
    state.disabled = true;
    set_graph_fallback(state, std::move(reason));
}

std::string_view fallback_stage_name(const ForwardGraphState& state) {
    if (!state.fallback_stage.empty()) {
        return state.fallback_stage;
    }
    if (!state.enabled) {
        return "torchtrt_cuda_graph_fallback_not_enabled";
    }
    if (state.disabled) {
        return "torchtrt_cuda_graph_fallback_disabled_unknown";
    }
    return "torchtrt_cuda_graph_fallback_unavailable";
}

std::string graph_exception_stage_name(std::string_view prefix, const char* message) {
    constexpr std::size_t kMaxSuffixLength = 96;
    std::string suffix;
    suffix.reserve(kMaxSuffixLength);
    bool last_was_separator = false;
    for (const char* cursor = message; cursor != nullptr && *cursor != '\0'; ++cursor) {
        const auto ch = static_cast<unsigned char>(*cursor);
        if (std::isalnum(ch) != 0) {
            suffix.push_back(static_cast<char>(std::tolower(ch)));
            last_was_separator = false;
        } else if (!last_was_separator && !suffix.empty()) {
            suffix.push_back('_');
            last_was_separator = true;
        }
        if (suffix.size() >= kMaxSuffixLength) {
            break;
        }
    }
    while (!suffix.empty() && suffix.back() == '_') {
        suffix.pop_back();
    }
    if (suffix.empty()) {
        suffix = "empty_message";
    }
    return std::string{prefix} + "_" + suffix;
}

void synchronize_current_cuda_stream() {
    // Keep the wait scoped to the Torch stream that owns this work. A device
    // sync also waits for independent copy/post streams in the process.
    const auto stream = c10::cuda::getCurrentCUDAStream();
    stream.synchronize();
}

void emit_stage_timing(const StageTimingCallback& on_stage, std::string_view name,
                       double total_ms) {
    if (!on_stage) {
        return;
    }
    on_stage(StageTiming{std::string{name}, total_ms, 1, 1});
}

#if defined(CORRIDORKEY_HAS_CUDA) && CORRIDORKEY_HAS_CUDA
struct DeferredCudaEventTiming {
    cudaEvent_t start_event = nullptr;
    cudaEvent_t stop_event = nullptr;
    std::string gpu_stage_name;
    bool active = false;

    DeferredCudaEventTiming() = default;
    DeferredCudaEventTiming(const DeferredCudaEventTiming&) = delete;
    DeferredCudaEventTiming& operator=(const DeferredCudaEventTiming&) = delete;

    DeferredCudaEventTiming(DeferredCudaEventTiming&& other) noexcept
        : start_event(std::exchange(other.start_event, nullptr)),
          stop_event(std::exchange(other.stop_event, nullptr)),
          gpu_stage_name(std::move(other.gpu_stage_name)),
          active(std::exchange(other.active, false)) {}

    DeferredCudaEventTiming& operator=(DeferredCudaEventTiming&& other) noexcept {
        if (this != &other) {
            reset();
            start_event = std::exchange(other.start_event, nullptr);
            stop_event = std::exchange(other.stop_event, nullptr);
            gpu_stage_name = std::move(other.gpu_stage_name);
            active = std::exchange(other.active, false);
        }
        return *this;
    }

    ~DeferredCudaEventTiming() { reset(); }

    void reset() {
        if (start_event != nullptr) {
            cudaEventDestroy(start_event);
            start_event = nullptr;
        }
        if (stop_event != nullptr) {
            cudaEventDestroy(stop_event);
            stop_event = nullptr;
        }
        active = false;
        gpu_stage_name.clear();
    }
};

DeferredCudaEventTiming begin_deferred_cuda_event_timing(
    const StageTimingCallback& on_stage, std::string_view gpu_stage_name) {
    DeferredCudaEventTiming timing;
    if (!on_stage) {
        return timing;
    }
    const auto stream = c10::cuda::getCurrentCUDAStream();
    timing.gpu_stage_name = std::string(gpu_stage_name);
    if (cudaEventCreate(&timing.start_event) != cudaSuccess ||
        cudaEventCreate(&timing.stop_event) != cudaSuccess ||
        cudaEventRecord(timing.start_event, stream.stream()) != cudaSuccess) {
        timing.reset();
        return timing;
    }
    timing.active = true;
    return timing;
}

void record_deferred_cuda_event_stop(DeferredCudaEventTiming& timing) {
    if (!timing.active) {
        return;
    }
    const auto stream = c10::cuda::getCurrentCUDAStream();
    if (cudaEventRecord(timing.stop_event, stream.stream()) != cudaSuccess) {
        timing.reset();
    }
}

void emit_deferred_cuda_event_elapsed(const StageTimingCallback& on_stage,
                                      DeferredCudaEventTiming& timing) {
    if (!timing.active) {
        return;
    }
    float elapsed_ms = 0.0F;
    if (cudaEventElapsedTime(&elapsed_ms, timing.start_event, timing.stop_event) == cudaSuccess) {
        emit_stage_timing(on_stage, timing.gpu_stage_name, static_cast<double>(elapsed_ms));
    }
    timing.reset();
}
#endif

template <typename Function>
std::optional<double> run_and_synchronize_with_cuda_event_timing(
    const StageTimingCallback& on_stage, std::string_view gpu_stage_name, Function&& function,
    std::string_view enqueue_wall_stage_name = {},
    std::string_view event_sync_wait_stage_name = {},
    std::string_view event_sync_over_gpu_stage_name = {}) {
#if defined(CORRIDORKEY_HAS_CUDA) && CORRIDORKEY_HAS_CUDA
    const auto stream = c10::cuda::getCurrentCUDAStream();
    cudaEvent_t start_event = nullptr;
    cudaEvent_t stop_event = nullptr;
    std::optional<double> gpu_elapsed_ms;
    std::optional<double> event_sync_ms;
    bool can_time_on_stream = on_stage != nullptr;
    can_time_on_stream =
        can_time_on_stream && cudaEventCreate(&start_event) == cudaSuccess;
    can_time_on_stream =
        can_time_on_stream && cudaEventCreate(&stop_event) == cudaSuccess;
    can_time_on_stream =
        can_time_on_stream && cudaEventRecord(start_event, stream.stream()) == cudaSuccess;

    try {
        const auto enqueue_start = std::chrono::steady_clock::now();
        std::forward<Function>(function)();
        const auto enqueue_end = std::chrono::steady_clock::now();
        if (!enqueue_wall_stage_name.empty()) {
            emit_stage_timing(
                on_stage, enqueue_wall_stage_name,
                std::chrono::duration<double, std::milli>(enqueue_end - enqueue_start).count());
        }
    } catch (...) {
        if (start_event != nullptr) {
            cudaEventDestroy(start_event);
        }
        if (stop_event != nullptr) {
            cudaEventDestroy(stop_event);
        }
        throw;
    }

    if (can_time_on_stream &&
        cudaEventRecord(stop_event, stream.stream()) == cudaSuccess) {
        const auto sync_start = std::chrono::steady_clock::now();
        const cudaError_t sync_status = cudaEventSynchronize(stop_event);
        const auto sync_end = std::chrono::steady_clock::now();
        event_sync_ms =
            std::chrono::duration<double, std::milli>(sync_end - sync_start).count();
        if (!event_sync_wait_stage_name.empty()) {
            emit_stage_timing(on_stage, event_sync_wait_stage_name, *event_sync_ms);
        }
        if (sync_status == cudaSuccess) {
            float elapsed_ms = 0.0F;
            if (cudaEventElapsedTime(&elapsed_ms, start_event, stop_event) == cudaSuccess) {
                gpu_elapsed_ms = static_cast<double>(elapsed_ms);
                emit_stage_timing(on_stage, gpu_stage_name, *gpu_elapsed_ms);
                if (!event_sync_over_gpu_stage_name.empty() && event_sync_ms.has_value()) {
                    emit_stage_timing(on_stage, event_sync_over_gpu_stage_name,
                                      std::max(0.0, *event_sync_ms - *gpu_elapsed_ms));
                }
            }
        } else {
            synchronize_current_cuda_stream();
        }
    } else {
        synchronize_current_cuda_stream();
    }

    if (start_event != nullptr) {
        cudaEventDestroy(start_event);
    }
    if (stop_event != nullptr) {
        cudaEventDestroy(stop_event);
    }
    return gpu_elapsed_ms;
#else
    (void)on_stage;
    (void)gpu_stage_name;
    (void)enqueue_wall_stage_name;
    (void)event_sync_wait_stage_name;
    (void)event_sync_over_gpu_stage_name;
    std::forward<Function>(function)();
    synchronize_current_cuda_stream();
    return std::nullopt;
#endif
}

template <typename Function>
void measure_cuda_wall_and_gpu_stage(const StageTimingCallback& on_stage,
                                     std::string_view wall_stage_name,
                                     std::string_view gpu_stage_name,
                                     std::string_view queue_wait_stage_name,
                                     Function&& function,
                                     std::string_view enqueue_wall_stage_name = {},
                                     std::string_view event_sync_wait_stage_name = {},
                                     std::string_view event_sync_over_gpu_stage_name = {}) {
    const auto start = std::chrono::steady_clock::now();
    std::optional<double> gpu_elapsed_ms;
    try {
        gpu_elapsed_ms = run_and_synchronize_with_cuda_event_timing(
            on_stage, gpu_stage_name, std::forward<Function>(function), enqueue_wall_stage_name,
            event_sync_wait_stage_name, event_sync_over_gpu_stage_name);
    } catch (...) {
        const auto end = std::chrono::steady_clock::now();
        emit_stage_timing(
            on_stage, wall_stage_name,
            std::chrono::duration<double, std::milli>(end - start).count());
        throw;
    }
    const auto end = std::chrono::steady_clock::now();
    const double wall_ms = std::chrono::duration<double, std::milli>(end - start).count();
    emit_stage_timing(on_stage, wall_stage_name, wall_ms);
    if (gpu_elapsed_ms.has_value()) {
        emit_stage_timing(on_stage, queue_wait_stage_name,
                          std::max(0.0, wall_ms - *gpu_elapsed_ms));
    }
}

template <typename Function>
void measure_cuda_graph_replay(const StageTimingCallback& on_stage, Function&& function) {
    measure_cuda_wall_and_gpu_stage(
        on_stage, "torchtrt_cuda_graph_replay", "torchtrt_cuda_graph_replay_gpu",
        "torchtrt_cuda_graph_replay_queue_wait", std::forward<Function>(function));
}

std::optional<double> synchronize_cuda_stream_marker(c10::cuda::CUDAStream stream,
                                                     const StageTimingCallback& on_stage,
                                                     std::string_view stage_name) {
    if (!on_stage) {
        return std::nullopt;
    }
    const auto measure_wait = [&](auto&& wait_function) -> double {
        const auto start = std::chrono::steady_clock::now();
        try {
            wait_function();
        } catch (...) {
            const auto end = std::chrono::steady_clock::now();
            const double elapsed_ms =
                std::chrono::duration<double, std::milli>(end - start).count();
            emit_stage_timing(on_stage, stage_name, elapsed_ms);
            throw;
        }
        const auto end = std::chrono::steady_clock::now();
        const double elapsed_ms = std::chrono::duration<double, std::milli>(end - start).count();
        emit_stage_timing(on_stage, stage_name, elapsed_ms);
        return elapsed_ms;
    };
#if defined(CORRIDORKEY_HAS_CUDA) && CORRIDORKEY_HAS_CUDA
    cudaEvent_t marker = nullptr;
    cudaError_t status = cudaEventCreateWithFlags(&marker, cudaEventDisableTiming);
    if (status != cudaSuccess || marker == nullptr) {
        return measure_wait([&]() { stream.synchronize(); });
    }

    status = cudaEventRecord(marker, stream.stream());
    double elapsed_ms = 0.0;
    if (status == cudaSuccess) {
        elapsed_ms = measure_wait([&]() { status = cudaEventSynchronize(marker); });
    } else {
        elapsed_ms = measure_wait([&]() { stream.synchronize(); });
    }
    cudaEventDestroy(marker);
    return elapsed_ms;
#else
    return measure_wait([&]() { stream.synchronize(); });
#endif
}

torch::IValue run_direct_forward(torch::jit::script::Module& module, const torch::Tensor& input,
                                 const torch::Tensor& pos_grid) {
    if (pos_grid.defined()) {
        return module.forward({input, pos_grid});
    }
    return module.forward({input});
}

void make_stream_wait_for(c10::cuda::CUDAStream waiting_stream,
                          c10::cuda::CUDAStream dependency_stream) {
    at::cuda::CUDAEvent event;
    event.record(dependency_stream);
    event.block(waiting_stream);
}

void replay_forward_graph(ForwardGraphState& state, c10::cuda::CUDAStream& capture_stream,
                          const StageTimingCallback& on_stage) {
    const auto current_stream = c10::cuda::getCurrentCUDAStream();
    make_stream_wait_for(capture_stream, current_stream);
    synchronize_cuda_stream_marker(capture_stream, on_stage,
                                   "torchtrt_cuda_graph_capture_stream_wait");
    {
        const c10::cuda::CUDAStreamGuard replay_guard(capture_stream);
        measure_cuda_graph_replay(on_stage, [&]() { state.graph.replay(); });
    }
    make_stream_wait_for(current_stream, capture_stream);
    synchronize_cuda_stream_marker(current_stream, on_stage,
                                   "torchtrt_cuda_graph_current_stream_wait");
}

bool graph_shape_matches(const ForwardGraphState& state, const torch::Tensor& input,
                         int input_width, int input_height, torch::Dtype input_dtype) {
    return state.static_input.defined() && state.input_width == input_width &&
           state.input_height == input_height && state.input_dtype == input_dtype &&
           state.static_input.sizes() == input.sizes();
}

std::optional<AlphaFgTensors> try_forward_cuda_graph(
    torch::jit::script::Module& module, ForwardGraphState& state, const torch::Tensor& input,
    const torch::Tensor& pos_grid, c10::cuda::CUDAStream& capture_stream, int input_width,
    int input_height, torch::Dtype input_dtype, const StageTimingCallback& on_stage) {
    if (!state.enabled) {
        set_graph_fallback(state, "torchtrt_cuda_graph_fallback_not_enabled");
        return std::nullopt;
    }
    if (state.disabled) {
        return std::nullopt;
    }
    if (!input.is_cuda()) {
        set_graph_fallback(state, "torchtrt_cuda_graph_fallback_input_not_cuda");
        return std::nullopt;
    }
    state.fallback_stage.clear();

    if (!graph_shape_matches(state, input, input_width, input_height, input_dtype)) {
        reset_forward_graph(state);
        emit_graph_event(on_stage, "torchtrt_cuda_graph_shape_reset");
        state.static_input = torch::empty_like(input);
        state.input_width = input_width;
        state.input_height = input_height;
        state.input_dtype = input_dtype;
    }

    measure_cuda_wall_and_gpu_stage(
        on_stage, "torchtrt_cuda_graph_input_copy", "torchtrt_cuda_graph_input_copy_gpu",
        "torchtrt_cuda_graph_input_copy_queue_wait", [&]() { state.static_input.copy_(input); });

    try {
        if (!state.warmed) {
            torch::IValue raw_out;
            const auto current_stream = c10::cuda::getCurrentCUDAStream();
            make_stream_wait_for(capture_stream, current_stream);
            {
                const c10::cuda::CUDAStreamGuard capture_guard(capture_stream);
                common::measure_stage(on_stage, "torchtrt_cuda_graph_warmup", [&]() {
                    raw_out = run_direct_forward(module, state.static_input, pos_grid);
                    capture_stream.synchronize();
                });
            }
            make_stream_wait_for(current_stream, capture_stream);
            auto split = split_forward_output(raw_out);
            if (!split.has_value()) {
                disable_forward_graph(state, "torchtrt_cuda_graph_fallback_warmup_output");
                return std::nullopt;
            }
            state.warmed = true;
            return split;
        }

        if (!state.captured) {
            torch::IValue raw_out;
            const auto current_stream = c10::cuda::getCurrentCUDAStream();
            make_stream_wait_for(capture_stream, current_stream);
            common::measure_stage(on_stage, "torchtrt_cuda_graph_capture", [&]() {
                const c10::cuda::CUDAStreamGuard capture_guard(capture_stream);
                state.graph.capture_begin();
                raw_out = run_direct_forward(module, state.static_input, pos_grid);
                state.graph.capture_end();
            });
            make_stream_wait_for(current_stream, capture_stream);
            auto split = split_forward_output(raw_out);
            if (!split.has_value()) {
                disable_forward_graph(state, "torchtrt_cuda_graph_fallback_capture_output");
                return std::nullopt;
            }
            state.static_outputs = *split;
            state.captured = true;
            replay_forward_graph(state, capture_stream, on_stage);
            return state.static_outputs;
        }

        replay_forward_graph(state, capture_stream, on_stage);
        return state.static_outputs;
    } catch (const c10::Error& e) {
        disable_forward_graph(
            state, graph_exception_stage_name("torchtrt_cuda_graph_fallback_c10_error", e.what()));
        return std::nullopt;
    } catch (const std::exception& e) {
        disable_forward_graph(
            state,
            graph_exception_stage_name("torchtrt_cuda_graph_fallback_std_exception", e.what()));
        return std::nullopt;
    }
}

bool json_int_at(const nlohmann::json& array, std::size_t index, int& value) {
    if (!array.is_array() || index >= array.size() || !array.at(index).is_number_integer()) {
        return false;
    }
    value = array.at(index).get<int>();
    return true;
}

Result<ExternalPosState> parse_external_pos_state(
    const std::unordered_map<std::string, std::string>& extra_files) {
    const auto meta_iter = extra_files.find(std::string{kExternalPosMetaName});
    const auto data_iter = extra_files.find(std::string{kExternalPosDataName});
    const bool has_meta = meta_iter != extra_files.end() && !meta_iter->second.empty();
    const bool has_data = data_iter != extra_files.end() && !data_iter->second.empty();
    if (!has_meta && !has_data) {
        return ExternalPosState{};
    }
    if (!has_meta || !has_data) {
        return Unexpected<Error>{
            Error{.code = ErrorCode::ModelLoadFailed,
                  .message = "TorchTRT external positional embedding metadata is incomplete"}};
    }

    const auto metadata = nlohmann::json::parse(meta_iter->second, nullptr, false);
    if (metadata.is_discarded() ||
        metadata.value("format", "") != "corridorkey_torchtrt_external_pos") {
        return Unexpected<Error>{
            Error{.code = ErrorCode::ModelLoadFailed,
                  .message = "TorchTRT external positional embedding metadata is invalid"}};
    }
    if (metadata.value("version", 0) != 1 || metadata.value("dtype", "") != "float32") {
        return Unexpected<Error>{Error{
            .code = ErrorCode::ModelLoadFailed,
            .message = "TorchTRT external positional embedding metadata version is unsupported"}};
    }

    const auto shape = metadata.value("shape", nlohmann::json::array());
    int batch = 0;
    int channels = 0;
    int source_height = 0;
    int source_width = 0;
    if (!json_int_at(shape, 0, batch) || !json_int_at(shape, 1, channels) ||
        !json_int_at(shape, 2, source_height) || !json_int_at(shape, 3, source_width) ||
        batch != 1 || channels <= 0 || source_height <= 0 || source_height != source_width) {
        return Unexpected<Error>{
            Error{.code = ErrorCode::ModelLoadFailed,
                  .message = "TorchTRT external positional embedding shape is invalid"}};
    }

    const auto stride = metadata.value("patch_stride", nlohmann::json::array());
    int patch_stride_height = 0;
    int patch_stride_width = 0;
    if (!json_int_at(stride, 0, patch_stride_height) ||
        !json_int_at(stride, 1, patch_stride_width) || patch_stride_height <= 0 ||
        patch_stride_width <= 0) {
        return Unexpected<Error>{
            Error{.code = ErrorCode::ModelLoadFailed,
                  .message = "TorchTRT external positional embedding stride is invalid"}};
    }

    const auto expected_bytes =
        static_cast<std::size_t>(batch) * channels * source_height * source_width * sizeof(float);
    if (data_iter->second.size() != expected_bytes) {
        return Unexpected<Error>{
            Error{.code = ErrorCode::ModelLoadFailed,
                  .message = "TorchTRT external positional embedding payload size is invalid"}};
    }

    const auto element_count = expected_bytes / sizeof(float);
    std::vector<float> values(element_count);
    std::memcpy(values.data(), data_iter->second.data(), expected_bytes);
    auto base_cpu =
        torch::from_blob(values.data(), {1, channels, source_height, source_width},
                         torch::TensorOptions().dtype(torch::kFloat32).device(torch::kCPU))
            .clone();

    ExternalPosState state;
    state.enabled = true;
    state.channels = channels;
    state.source_grid = source_height;
    state.patch_stride_height = patch_stride_height;
    state.patch_stride_width = patch_stride_width;
    state.base_cuda_float = base_cpu.to(torch::Device(torch::kCUDA), torch::kFloat32);
    return state;
}

Result<torch::Tensor> external_pos_grid_for(ExternalPosState& state, int input_width,
                                            int input_height, torch::Dtype dtype,
                                            const StageTimingCallback& on_stage) {
    if (!state.enabled) {
        return torch::Tensor{};
    }
    if ((input_width % state.patch_stride_width) != 0 ||
        (input_height % state.patch_stride_height) != 0) {
        return Unexpected<Error>{
            Error{.code = ErrorCode::InvalidParameters,
                  .message = "TorchTRT external positional embedding input size is not aligned"}};
    }
    if (state.cached_grid.defined() && state.cached_input_width == input_width &&
        state.cached_input_height == input_height && state.cached_dtype == dtype) {
        return state.cached_grid;
    }

    common::measure_stage(on_stage, "torchtrt_prepare_pos_grid", [&]() {
        const std::vector<int64_t> output_size{
            input_height / state.patch_stride_height,
            input_width / state.patch_stride_width,
        };
        state.cached_grid = at::upsample_bicubic2d(state.base_cuda_float, output_size, false,
                                                   std::nullopt, std::nullopt);
        if (dtype != torch::kFloat32) {
            state.cached_grid = state.cached_grid.to(dtype);
        }
        state.cached_input_width = input_width;
        state.cached_input_height = input_height;
        state.cached_dtype = dtype;
    });
    return state.cached_grid;
}

struct MaterializedOutputTensors {
    ImageBuffer alpha;
    ImageBuffer foreground;
    bool post_processed = false;
    bool post_source_passthrough_applied = false;
    bool post_despill_applied = false;
    bool external_output_written = false;
};

struct CudaPostProcessResult {
    bool source_passthrough_applied = false;
    bool despill_applied = false;
    bool alpha_clamped = false;
};

#if defined(CORRIDORKEY_HAS_CUDA) && CORRIDORKEY_HAS_CUDA
class PinnedImageBufferPool {
   public:
    PinnedImageBufferPool() : m_state(std::make_shared<State>()) {}

    ImageBuffer acquire(int width, int height, int channels) {
        const auto count =
            static_cast<std::size_t>(width) * static_cast<std::size_t>(height) *
            static_cast<std::size_t>(channels);
        if (count == 0) {
            return {};
        }

        if (auto reused = try_reuse(width, height, channels, count); reused.has_value()) {
            return adopt_allocation(width, height, channels, *reused);
        }

        void* ptr = nullptr;
        if (cudaMallocHost(&ptr, count * sizeof(float)) != cudaSuccess || ptr == nullptr) {
            return ImageBuffer(width, height, channels);
        }
        return adopt_allocation(width, height, channels,
                                Allocation{.ptr = static_cast<float*>(ptr), .count = count});
    }

   private:
    struct Allocation {
        float* ptr = nullptr;
        std::size_t count = 0;
    };

    struct FreeEntry {
        int width = 0;
        int height = 0;
        int channels = 0;
        Allocation allocation;
    };

    struct State {
        ~State() {
            for (const auto& entry : free_entries) {
                if (entry.allocation.ptr != nullptr) {
                    (void)cudaFreeHost(entry.allocation.ptr);
                }
            }
        }

        std::mutex mutex;
        std::vector<FreeEntry> free_entries;
    };

    std::optional<Allocation> try_reuse(int width, int height, int channels, std::size_t count) {
        std::lock_guard lock(m_state->mutex);
        const auto it = std::ranges::find_if(m_state->free_entries, [&](const FreeEntry& entry) {
            return entry.width == width && entry.height == height && entry.channels == channels &&
                   entry.allocation.count >= count;
        });
        if (it == m_state->free_entries.end()) {
            return std::nullopt;
        }
        Allocation allocation = it->allocation;
        m_state->free_entries.erase(it);
        return allocation;
    }

    ImageBuffer adopt_allocation(int width, int height, int channels, Allocation allocation) {
        auto state = m_state;
        return ImageBuffer::adopt(width, height, channels, allocation.ptr,
                                  [state, width, height, channels, allocation](float*) mutable {
                                      std::lock_guard lock(state->mutex);
                                      state->free_entries.push_back(FreeEntry{
                                          .width = width,
                                          .height = height,
                                          .channels = channels,
                                          .allocation = allocation,
                                      });
                                  });
    }

    std::shared_ptr<State> m_state;
};

ImageBuffer allocate_cuda_pinned_image_buffer(PinnedImageBufferPool& pool, int width, int height,
                                              int channels) {
    return pool.acquire(width, height, channels);
}
#else
class PinnedImageBufferPool {};

ImageBuffer allocate_cuda_pinned_image_buffer(PinnedImageBufferPool&, int width, int height,
                                              int channels) {
    return ImageBuffer(width, height, channels);
}
#endif

struct OutputMaterializationShape {
    int crop_width = 0;
    int crop_height = 0;
    int final_width = 0;
    int final_height = 0;
    int pad_top = 0;
    int pad_left = 0;
};

constexpr float kSourcePassthroughInteriorThreshold = 0.95F;

bool output_view_matches(Image view, int width, int height, int channels) {
    return !view.empty() && view.width == width && view.height == height &&
           view.channels == channels;
}

ImageBuffer adopt_external_output(Image view) {
    return ImageBuffer::adopt(view.width, view.height, view.channels, view.data.data(),
                              [](float*) {});
}

#if defined(CORRIDORKEY_HAS_CUDA) && CORRIDORKEY_HAS_CUDA
class ScopedCudaHostRegistration {
   public:
    ScopedCudaHostRegistration() = default;
    ScopedCudaHostRegistration(const ScopedCudaHostRegistration&) = delete;
    ScopedCudaHostRegistration& operator=(const ScopedCudaHostRegistration&) = delete;

    ~ScopedCudaHostRegistration() {
        unregister();
    }

    bool register_views(Image alpha, bool include_alpha, Image foreground, bool include_foreground) {
        std::uintptr_t begin = 0;
        std::uintptr_t end = 0;
        if (include_alpha) {
            extend_range(alpha, begin, end);
        }
        if (include_foreground) {
            extend_range(foreground, begin, end);
        }
        if (begin == 0 || end <= begin) {
            return false;
        }

        // cudaHostRegister works on OS page ranges; round the shared mapping span outward.
        constexpr std::uintptr_t kPageAlignment = 4096;
        const std::uintptr_t aligned_begin = begin & ~(kPageAlignment - 1U);
        const std::uintptr_t aligned_end = (end + kPageAlignment - 1U) & ~(kPageAlignment - 1U);
        m_ptr = reinterpret_cast<void*>(aligned_begin);
        m_size = static_cast<std::size_t>(aligned_end - aligned_begin);
        const cudaError_t status = cudaHostRegister(m_ptr, m_size, cudaHostRegisterPortable);
        m_registered = status == cudaSuccess;
        if (!m_registered) {
            m_ptr = nullptr;
            m_size = 0;
        }
        return m_registered;
    }

    void unregister() {
        if (!m_registered || m_ptr == nullptr) {
            return;
        }
        (void)cudaHostUnregister(m_ptr);
        m_ptr = nullptr;
        m_size = 0;
        m_registered = false;
    }

   private:
    static void extend_range(Image view, std::uintptr_t& begin, std::uintptr_t& end) {
        if (view.empty()) {
            return;
        }
        const auto view_begin = reinterpret_cast<std::uintptr_t>(view.data.data());
        const auto view_end = view_begin + view.data.size_bytes();
        begin = begin == 0 ? view_begin : std::min(begin, view_begin);
        end = std::max(end, view_end);
    }

    void* m_ptr = nullptr;
    std::size_t m_size = 0;
    bool m_registered = false;
};
#endif

torch::Tensor shifted_mask_zero_border(const torch::Tensor& mask, int dy, int dx) {
    const int64_t height = mask.size(2);
    const int64_t width = mask.size(3);
    const int64_t pad_top = std::max(-dy, 0);
    const int64_t pad_bottom = std::max(dy, 0);
    const int64_t pad_left = std::max(-dx, 0);
    const int64_t pad_right = std::max(dx, 0);
    const int64_t start_y = std::max(dy, 0);
    const int64_t start_x = std::max(dx, 0);
    auto padded = torch::constant_pad_nd(mask, {pad_left, pad_right, pad_top, pad_bottom}, 0.0);
    return padded.narrow(2, start_y, height).narrow(3, start_x, width);
}

torch::Tensor horizontal_min_mask_zero_border(const torch::Tensor& mask, int radius) {
    if (radius <= 0) {
        return mask;
    }
    auto padded = torch::constant_pad_nd(mask, {radius, radius, 0, 0}, 0.0);
    const std::array<int64_t, 2> kernel = {1, (radius * 2) + 1};
    const std::array<int64_t, 2> stride = {1, 1};
    const std::array<int64_t, 2> padding = {0, 0};
    const std::array<int64_t, 2> dilation = {1, 1};
    return -at::max_pool2d(-padded, kernel, stride, padding, dilation, false);
}

torch::Tensor erode_mask_elliptical_pool(const torch::Tensor& mask, int radius) {
    if (radius <= 0) {
        return mask;
    }
    std::unordered_map<int, torch::Tensor> horizontal_by_radius;
    auto eroded = torch::ones_like(mask);
    const int radius_sq = radius * radius;
    for (int dy = -radius; dy <= radius; ++dy) {
        const int dx_radius =
            static_cast<int>(std::floor(std::sqrt(static_cast<float>(radius_sq - (dy * dy)))));
        auto [entry, inserted] = horizontal_by_radius.try_emplace(dx_radius);
        if (inserted) {
            entry->second = horizontal_min_mask_zero_border(mask, dx_radius);
        }
        eroded = torch::minimum(eroded, shifted_mask_zero_border(entry->second, dy, 0));
    }
    return eroded;
}

std::vector<float> gaussian_kernel_values(int half_size) {
    const int kernel_size = half_size * 2 + 1;
    const float sigma =
        0.3F * ((static_cast<float>(kernel_size) - 1.0F) * 0.5F - 1.0F) + 0.8F;
    std::vector<float> values(static_cast<std::size_t>(kernel_size));
    float sum = 0.0F;
    for (int index = 0; index < kernel_size; ++index) {
        const float x = static_cast<float>(index - half_size);
        const float value = std::exp(-0.5F * (x * x) / (sigma * sigma));
        values[static_cast<std::size_t>(index)] = value;
        sum += value;
    }
    for (float& value : values) {
        value /= sum;
    }
    return values;
}

torch::Tensor gaussian_kernel_tensor(int half_size, const torch::Tensor& reference,
                                     bool horizontal) {
    const int kernel_size = half_size * 2 + 1;
    const float sigma =
        0.3F * ((static_cast<float>(kernel_size) - 1.0F) * 0.5F - 1.0F) + 0.8F;
    std::vector<float> values(static_cast<std::size_t>(kernel_size));
    float sum = 0.0F;
    for (int index = 0; index < kernel_size; ++index) {
        const float x = static_cast<float>(index - half_size);
        const float value = std::exp(-0.5F * (x * x) / (sigma * sigma));
        values[static_cast<std::size_t>(index)] = value;
        sum += value;
    }
    for (float& value : values) {
        value /= sum;
    }
    auto cpu_options = torch::TensorOptions().dtype(torch::kFloat32).device(torch::kCPU);
    auto kernel = torch::from_blob(values.data(), {kernel_size}, cpu_options).clone();
    if (reference.device().is_cuda()) {
        kernel = kernel.to(reference.device(), torch::kFloat32, false);
    }
    return horizontal ? kernel.view({1, 1, 1, kernel_size})
                      : kernel.view({1, 1, kernel_size, 1});
}

bool blur_mask_npp_separable(torch::Tensor& mask, int half_size) {
#if defined(CORRIDORKEY_HAS_CUDA) && CORRIDORKEY_HAS_CUDA
    if (half_size <= 0) {
        return true;
    }
    const int width = static_cast<int>(mask.size(3));
    const int height = static_cast<int>(mask.size(2));
    if (width <= 0 || height <= 0 || !mask.is_cuda() || mask.scalar_type() != torch::kFloat32) {
        return false;
    }
    if (!mask.is_contiguous()) {
        mask = mask.contiguous();
    }

    const auto stream = c10::cuda::getCurrentCUDAStream();
    NppStreamContext npp_context{};
    if (!detail::make_npp_stream_context(stream.stream(), npp_context)) {
        return false;
    }

    auto temp = torch::empty_like(mask);
    auto blurred = torch::empty_like(mask);
    const auto kernel = gaussian_kernel_values(half_size);
    auto kernel_tensor =
        torch::empty({static_cast<int64_t>(kernel.size())}, mask.options().dtype(torch::kFloat32));
    const int step = width * static_cast<int>(sizeof(float));
    NppiSize source_size = {width, height};
    NppiPoint source_offset = {0, 0};
    NppiSize roi = {width, height};
    const int kernel_size = static_cast<int>(kernel.size());
    const cudaError_t kernel_copy_status =
        cudaMemcpyAsync(kernel_tensor.data_ptr<float>(), kernel.data(),
                        kernel.size() * sizeof(float), cudaMemcpyHostToDevice, stream.stream());
    if (kernel_copy_status != cudaSuccess) {
        return false;
    }
    const NppStatus row_status = nppiFilterRowBorder_32f_C1R_Ctx(
        mask.data_ptr<float>(), step, source_size, source_offset, temp.data_ptr<float>(), step,
        roi, kernel_tensor.data_ptr<float>(), kernel_size, half_size, NPP_BORDER_REPLICATE,
        npp_context);
    if (row_status != NPP_SUCCESS) {
        return false;
    }
    const NppStatus column_status = nppiFilterColumnBorder_32f_C1R_Ctx(
        temp.data_ptr<float>(), step, source_size, source_offset, blurred.data_ptr<float>(), step,
        roi, kernel_tensor.data_ptr<float>(), kernel_size, half_size, NPP_BORDER_REPLICATE,
        npp_context);
    if (column_status != NPP_SUCCESS) {
        return false;
    }
    mask = blurred;
    return true;
#else
    (void)mask;
    (void)half_size;
    return false;
#endif
}

torch::Tensor blur_mask_replicate_border(const torch::Tensor& mask, int half_size) {
    if (half_size <= 0) {
        return mask;
    }
    auto kernel_x = gaussian_kernel_tensor(half_size, mask, true);
    auto kernel_y = gaussian_kernel_tensor(half_size, mask, false);
    const std::optional<torch::Tensor> bias = std::nullopt;
    const std::array<int64_t, 2> stride = {1, 1};
    const std::array<int64_t, 2> padding = {0, 0};
    const std::array<int64_t, 2> dilation = {1, 1};
    auto padded_x = at::replication_pad2d(mask, {half_size, half_size, 0, 0});
    auto temp = at::conv2d(padded_x, kernel_x, bias, stride, padding, dilation, 1);
    auto padded_y = at::replication_pad2d(temp, {0, 0, half_size, half_size});
    return at::conv2d(padded_y, kernel_y, bias, stride, padding, dilation, 1);
}

bool apply_source_passthrough_cuda(torch::Tensor& foreground_nchw, const torch::Tensor& alpha_nchw,
                                   TorchTrtDeviceSource source, const InferenceParams& params,
                                   const StageTimingCallback& on_stage) {
    if (!params.source_passthrough || source.channels < 3 ||
        (source.host_rgb.empty() && source.rgb_device == nullptr) ||
        source.width != foreground_nchw.size(3) || source.height != foreground_nchw.size(2)) {
        return false;
    }

    common::measure_stage(on_stage, "post_source_passthrough_gpu", [&]() {
        torch::Tensor interior_mask;
        common::measure_stage(on_stage, "post_source_passthrough_gpu_threshold", [&]() {
            interior_mask =
                (alpha_nchw > kSourcePassthroughInteriorThreshold).to(torch::kFloat32);
        });
        auto mask = interior_mask;
        if (params.sp_erode_px > 0) {
            common::measure_stage(on_stage, "post_source_passthrough_gpu_erode", [&]() {
                mask = erode_mask_elliptical_pool(mask, params.sp_erode_px);
            });
        }
        if (params.sp_blur_px > 0) {
            common::measure_stage(on_stage, "post_source_passthrough_gpu_blur", [&]() {
                if (!blur_mask_npp_separable(mask, params.sp_blur_px)) {
                    mask = blur_mask_replicate_border(mask, params.sp_blur_px);
                }
                mask.mul_(interior_mask);
            });
        }

        torch::Tensor source_hwc;
        auto options = torch::TensorOptions().dtype(torch::kFloat32).device(torch::kCUDA);
        source_hwc = torch::empty({1, source.height, source.width, 3}, options);
#if defined(CORRIDORKEY_HAS_CUDA) && CORRIDORKEY_HAS_CUDA
        const auto byte_count = static_cast<std::size_t>(source.width) *
                                static_cast<std::size_t>(source.height) * 3U * sizeof(float);
        const auto stream = c10::cuda::getCurrentCUDAStream().stream();
        if (source.rgb_device != nullptr) {
            emit_graph_event(on_stage, "post_source_passthrough_gpu_copy_device_to_device");
            cudaError_t copy_status = cudaSuccess;
            common::measure_stage(on_stage, "post_source_passthrough_gpu_source_copy_enqueue",
                                  [&]() {
                                      copy_status = cudaMemcpyAsync(
                                          source_hwc.data_ptr<float>(), source.rgb_device,
                                          byte_count, cudaMemcpyDeviceToDevice, stream);
                                  });
            if (copy_status != cudaSuccess) {
                throw std::runtime_error(std::string("TorchTRT source passthrough D2D copy failed: ") +
                                         cudaGetErrorString(copy_status));
            }
        } else if (!source.host_rgb.empty()) {
            emit_graph_event(on_stage, "post_source_passthrough_gpu_copy_host_to_device");
            cudaError_t copy_status = cudaSuccess;
            common::measure_stage(on_stage, "post_source_passthrough_gpu_source_copy_enqueue",
                                  [&]() {
                                      copy_status = cudaMemcpyAsync(
                                          source_hwc.data_ptr<float>(),
                                          source.host_rgb.data.data(), byte_count,
                                          cudaMemcpyHostToDevice, stream);
                                  });
            if (copy_status != cudaSuccess) {
                throw std::runtime_error(std::string("TorchTRT source passthrough H2D copy failed: ") +
                                         cudaGetErrorString(copy_status));
            }
        }
#else
        return false;
#endif
        auto source_nchw = source_hwc.permute({0, 3, 1, 2});
        common::measure_stage(on_stage, "post_source_passthrough_gpu_blend",
                              [&]() { foreground_nchw.lerp_(source_nchw, mask); });
    });
    return true;
}

bool apply_despill_cuda(torch::Tensor& foreground_nchw, const InferenceParams& params,
                        const StageTimingCallback& on_stage) {
    if (params.despill_strength <= 0.0F || params.despill_screen_channel < 0 ||
        params.despill_screen_channel > 2) {
        return false;
    }

    common::measure_stage(on_stage, "post_despill_gpu", [&]() {
        const int screen_channel = params.despill_screen_channel;
        const int other_a = (screen_channel == 0) ? 1 : 0;
        const int other_b = (screen_channel == 2) ? 1 : 2;
        const SpillMethod method = effective_despill_method(
            DespillMethodRequest{.requested_method = params.spill_method,
                                 .screen_channel = screen_channel});

        auto screen_view = foreground_nchw.select(1, screen_channel);
        auto other_a_view = foreground_nchw.select(1, other_a);
        auto other_b_view = foreground_nchw.select(1, other_b);
        auto screen = screen_view.clone();
        auto a = other_a_view.clone();
        auto b = other_b_view.clone();

        torch::Tensor limit;
        if (method == SpillMethod::DoubleLimit) {
            limit = torch::maximum(a, b);
        } else {
            limit = (a + b) * 0.5F;
        }

        auto spill = torch::clamp_min(screen - limit, 0.0);
        auto active = spill > 0.0;
        auto effective_spill = spill * params.despill_strength;
        auto new_screen = screen - effective_spill;
        screen_view.copy_(torch::where(active, new_screen, screen));

        if (method == SpillMethod::ScreenOnly) {
            return;
        }

        if (method == SpillMethod::Neutral) {
            auto gray = (a + new_screen + b) / 3.0F;
            auto fill = effective_spill * 0.5F;
            auto new_a = a + fill * (gray / torch::clamp_min(a, 1e-6));
            auto new_b = b + fill * (gray / torch::clamp_min(b, 1e-6));
            other_a_view.copy_(torch::where(active, new_a, a));
            other_b_view.copy_(torch::where(active, new_b, b));
        } else {
            auto fill = effective_spill * 0.5F;
            other_a_view.copy_(torch::where(active, a + fill, a));
            other_b_view.copy_(torch::where(active, b + fill, b));
        }
    });
    return true;
}

CudaPostProcessResult apply_cuda_postprocess_if_supported(
    torch::Tensor& alpha_nchw, torch::Tensor& foreground_nchw, const InferenceParams* params,
    TorchTrtDeviceSource source, const StageTimingCallback& on_stage) {
    CudaPostProcessResult result;
    if (params == nullptr || foreground_nchw.numel() == 0 || params->output_alpha_only) {
        return result;
    }
    common::measure_stage(on_stage, "post_gpu_prepare", [&]() {
        if (params->source_passthrough) {
            result.source_passthrough_applied =
                apply_source_passthrough_cuda(foreground_nchw, alpha_nchw, source, *params,
                                              on_stage);
        }
        if (!params->source_passthrough || result.source_passthrough_applied) {
            result.despill_applied = apply_despill_cuda(foreground_nchw, *params, on_stage);
        }
        alpha_nchw.clamp_(0.0F, 1.0F);
        result.alpha_clamped = true;
    });
    return result;
}

bool despill_needed(const InferenceParams& params) {
    return params.despill_strength > 0.0F && params.despill_screen_channel >= 0 &&
           params.despill_screen_channel <= 2;
}

bool cuda_postprocess_completed(const InferenceParams& params,
                                const CudaPostProcessResult& result) {
    return result.alpha_clamped && !params.output_auxiliary_images && !params.auto_despeckle &&
           (!params.source_passthrough || result.source_passthrough_applied) &&
           (!despill_needed(params) || result.despill_applied);
}

MaterializedOutputTensors materialize_outputs(
    const torch::Tensor& alpha_cuda, const torch::Tensor& fg_cuda, OutputMaterializationShape shape,
    bool include_foreground, OutputResizeFilter resize_filter, PinnedHostTensorRing& host_ring,
    PinnedImageBufferPool& output_pool, c10::cuda::CUDAStream& copy_stream,
    const StageTimingCallback& on_stage, const InferenceParams* post_process_params = nullptr,
    TorchTrtDeviceSource source = {}, FrameOutputViews output_views = {}) {
#if defined(CORRIDORKEY_HAS_CUDA) && CORRIDORKEY_HAS_CUDA
    (void)host_ring;
#endif
    auto alpha_nchw = crop_output_nchw(alpha_cuda, shape.crop_width, shape.crop_height,
                                       shape.pad_top, shape.pad_left);
    torch::Tensor fg_nchw;
    if (include_foreground) {
        fg_nchw = crop_output_nchw(fg_cuda, shape.crop_width, shape.crop_height, shape.pad_top,
                                   shape.pad_left);
    }

    common::measure_stage(
        on_stage, "frame_extract_outputs_resize",
        [&]() {
            resize_output_if_needed(alpha_nchw, shape.crop_width, shape.crop_height,
                                    shape.final_width, shape.final_height, resize_filter);
            if (include_foreground) {
                resize_output_if_needed(fg_nchw, shape.crop_width, shape.crop_height,
                                        shape.final_width, shape.final_height, resize_filter);
            }
        },
        1);

    const CudaPostProcessResult gpu_post_process =
        apply_cuda_postprocess_if_supported(alpha_nchw, fg_nchw, post_process_params, source,
                                            on_stage);

    torch::Tensor alpha_flat;
    torch::Tensor fg_flat;
    common::measure_stage(on_stage, "torchtrt_output_pack", [&]() {
        alpha_flat = flatten_nchw(alpha_nchw);
        if (include_foreground) {
            fg_flat = fg_nchw.permute({0, 2, 3, 1}).contiguous().view({-1});
        }
    });

    MaterializedOutputTensors result;
    if (post_process_params != nullptr) {
        result.post_processed = cuda_postprocess_completed(*post_process_params, gpu_post_process);
    }
    result.post_source_passthrough_applied = gpu_post_process.source_passthrough_applied;
    result.post_despill_applied = gpu_post_process.despill_applied;
    const auto alpha_count =
        static_cast<std::size_t>(shape.final_width) * static_cast<std::size_t>(shape.final_height);
    const bool direct_alpha =
        output_view_matches(output_views.alpha, shape.final_width, shape.final_height, 1);
    const bool direct_foreground =
        !include_foreground ||
        output_view_matches(output_views.foreground, shape.final_width, shape.final_height, 3);
    common::measure_stage(on_stage, "torchtrt_output_allocate_host", [&]() {
        if (direct_alpha) {
            result.alpha = adopt_external_output(output_views.alpha);
        } else {
            result.alpha = allocate_cuda_pinned_image_buffer(output_pool, shape.final_width,
                                                             shape.final_height, 1);
        }
        if (include_foreground) {
            if (direct_foreground) {
                result.foreground = adopt_external_output(output_views.foreground);
            } else {
                result.foreground = allocate_cuda_pinned_image_buffer(
                    output_pool, shape.final_width, shape.final_height, 3);
            }
        }
    });
    result.external_output_written = direct_alpha && direct_foreground;

#if defined(CORRIDORKEY_HAS_CUDA) && CORRIDORKEY_HAS_CUDA
    common::measure_stage(on_stage, "torchtrt_output_d2h_direct", [&]() {
        ScopedCudaHostRegistration output_registration;
        common::measure_stage(on_stage, "torchtrt_output_host_register", [&]() {
            (void)output_registration.register_views(result.alpha.view(), direct_alpha,
                                                     result.foreground.view(),
                                                     include_foreground && direct_foreground);
        });
        const auto producer_stream = c10::cuda::getCurrentCUDAStream();
        at::cuda::CUDAEvent producer_done;
        common::measure_stage(on_stage, "torchtrt_output_wait_enqueue", [&]() {
            producer_done.record(producer_stream);
            producer_done.block(copy_stream);
        });
        {
            const c10::cuda::CUDAStreamGuard copy_guard(copy_stream);
            common::measure_stage(on_stage, "torchtrt_output_copy_enqueue", [&]() {
                const cudaError_t alpha_status = cudaMemcpyAsync(
                    result.alpha.view().data.data(), alpha_flat.data_ptr<float>(),
                    alpha_count * sizeof(float), cudaMemcpyDeviceToHost, copy_stream.stream());
                if (alpha_status != cudaSuccess) {
                    throw std::runtime_error(std::string("TorchTRT alpha D2H copy failed: ") +
                                             cudaGetErrorString(alpha_status));
                }
                if (include_foreground) {
                    const cudaError_t foreground_status = cudaMemcpyAsync(
                        result.foreground.view().data.data(), fg_flat.data_ptr<float>(),
                        result.foreground.view().data.size_bytes(), cudaMemcpyDeviceToHost,
                        copy_stream.stream());
                    if (foreground_status != cudaSuccess) {
                        throw std::runtime_error(
                            std::string("TorchTRT foreground D2H copy failed: ") +
                            cudaGetErrorString(foreground_status));
                    }
                }
            });
        }
        common::measure_stage(on_stage, "torchtrt_output_copy_sync",
                              [&]() { copy_stream.synchronize(); });
        common::measure_stage(on_stage, "torchtrt_output_host_unregister",
                              [&]() { output_registration.unregister(); });
    });
#else
    auto host_alpha = host_ring.acquire(alpha_flat.numel());
    torch::Tensor host_foreground;
    if (include_foreground) {
        host_foreground = host_ring.acquire(fg_flat.numel());
    }
    common::measure_stage(on_stage, "torchtrt_output_d2h", [&]() {
        const auto producer_stream = c10::cuda::getCurrentCUDAStream();
        at::cuda::CUDAEvent producer_done;
        common::measure_stage(on_stage, "torchtrt_output_wait_enqueue", [&]() {
            producer_done.record(producer_stream);
            producer_done.block(copy_stream);
        });
        {
            const c10::cuda::CUDAStreamGuard copy_guard(copy_stream);
            common::measure_stage(on_stage, "torchtrt_output_copy_enqueue", [&]() {
                host_alpha.copy_(alpha_flat, true);
                if (include_foreground) {
                    host_foreground.copy_(fg_flat, true);
                }
            });
        }
        common::measure_stage(on_stage, "torchtrt_output_copy_sync",
                              [&]() { copy_stream.synchronize(); });
    });
    common::measure_stage(on_stage, "torchtrt_output_unpack_cpu", [&]() {
        const auto* alpha_src = host_alpha.data_ptr<float>();
        std::memcpy(result.alpha.view().data.data(), alpha_src, alpha_count * sizeof(float));

        if (include_foreground) {
            const auto* foreground_src = host_foreground.data_ptr<float>();
            std::memcpy(result.foreground.view().data.data(), foreground_src,
                        result.foreground.view().data.size_bytes());
        }
    });
#endif
    return result;
}

// NOLINTBEGIN(bugprone-easily-swappable-parameters) - these dimensions mirror tensor shape order.
Result<TorchTrtFrameResult> forward_and_materialize(
    torch::jit::script::Module& module, ExternalPosState& external_pos,
    ForwardGraphState& forward_graph,
    const torch::Tensor& cuda_input, int crop_width, int crop_height, int final_width,
    int final_height, int inference_width, int inference_height, int pad_top, int pad_left,
    torch::Dtype input_dtype, bool output_alpha_only, OutputResizeFilter resize_filter,
    PinnedHostTensorRing& host_ring, PinnedImageBufferPool& output_pool,
    c10::cuda::CUDAStream& copy_stream, c10::cuda::CUDAStream& capture_stream,
    const StageTimingCallback& on_stage, const InferenceParams* post_process_params = nullptr,
    TorchTrtDeviceSource source = {}, FrameOutputViews output_views = {}) {
    const c10::InferenceMode inference_guard(true);
    auto pos_grid = external_pos_grid_for(external_pos, inference_width, inference_height,
                                          input_dtype, on_stage);
    if (!pos_grid.has_value()) {
        return Unexpected<Error>{pos_grid.error()};
    }

    AlphaFgTensors split;
#if defined(CORRIDORKEY_HAS_CUDA) && CORRIDORKEY_HAS_CUDA
    DeferredCudaEventTiming direct_forward_timing;
#endif
    common::measure_stage(on_stage, "torchtrt_forward", [&]() {
        auto graph_out = try_forward_cuda_graph(module, forward_graph, cuda_input, *pos_grid,
                                                capture_stream, inference_width, inference_height,
                                                input_dtype, on_stage);
        if (graph_out.has_value()) {
            split = *graph_out;
            return;
        }
        emit_graph_event(on_stage, fallback_stage_name(forward_graph));
        torch::IValue raw_out;
        if (torchtrt_direct_forward_sync_timing_requested()) {
            measure_cuda_wall_and_gpu_stage(on_stage, "torchtrt_forward_direct",
                                            "torchtrt_forward_direct_gpu",
                                            "torchtrt_forward_direct_queue_wait", [&]() {
                                                raw_out = run_direct_forward(module, cuda_input,
                                                                             *pos_grid);
                                            },
                                            "torchtrt_forward_direct_enqueue_wall",
                                            "torchtrt_forward_direct_event_sync_wait",
                                            "torchtrt_forward_direct_event_sync_over_gpu");
        } else {
            common::measure_stage(on_stage, "torchtrt_forward_direct", [&]() {
#if defined(CORRIDORKEY_HAS_CUDA) && CORRIDORKEY_HAS_CUDA
                direct_forward_timing =
                    begin_deferred_cuda_event_timing(on_stage, "torchtrt_forward_direct_gpu");
#endif
                const auto enqueue_start = std::chrono::steady_clock::now();
                raw_out = run_direct_forward(module, cuda_input, *pos_grid);
                const auto enqueue_end = std::chrono::steady_clock::now();
                emit_stage_timing(
                    on_stage, "torchtrt_forward_direct_enqueue_wall",
                    std::chrono::duration<double, std::milli>(enqueue_end - enqueue_start)
                        .count());
#if defined(CORRIDORKEY_HAS_CUDA) && CORRIDORKEY_HAS_CUDA
                record_deferred_cuda_event_stop(direct_forward_timing);
#else
                synchronize_current_cuda_stream();
#endif
            });
        }
        auto direct_split = split_forward_output(raw_out);
        if (direct_split.has_value()) {
            split = *direct_split;
        }
    });

    if (!split.alpha.defined()) {
        return Unexpected<Error>{
            Error{.code = ErrorCode::InferenceFailed,
                  .message = "TorchTRT forward returned no usable alpha tensor"}};
    }
    if (!tensor_has_shape(split.alpha, 1, inference_width, inference_height)) {
        return Unexpected<Error>{Error{
            .code = ErrorCode::InferenceFailed,
            .message = "TorchScript RTX alpha output shape did not match input " +
                       std::to_string(inference_width) + "x" + std::to_string(inference_height)}};
    }
    if (!output_alpha_only && split.foreground.defined() &&
        !tensor_has_shape(split.foreground, 3, inference_width, inference_height)) {
        return Unexpected<Error>{Error{
            .code = ErrorCode::InferenceFailed,
            .message = "TorchScript RTX foreground output shape did not match input " +
                       std::to_string(inference_width) + "x" + std::to_string(inference_height)}};
    }

    TorchTrtFrameResult result;
    common::measure_stage(on_stage, "torchtrt_extract_outputs", [&]() {
        const bool include_foreground = !output_alpha_only && split.foreground.defined();
        auto materialized =
            materialize_outputs(split.alpha, split.foreground,
                                OutputMaterializationShape{.crop_width = crop_width,
                                                           .crop_height = crop_height,
                                                           .final_width = final_width,
                                                           .final_height = final_height,
                                                           .pad_top = pad_top,
                                                           .pad_left = pad_left},
                                include_foreground, resize_filter, host_ring, output_pool,
                                copy_stream, on_stage, post_process_params, source,
                                output_views);
        result.frame.alpha = std::move(materialized.alpha);
        result.frame.foreground = std::move(materialized.foreground);
        result.frame.post_processed = materialized.post_processed;
        result.post_source_passthrough_applied = materialized.post_source_passthrough_applied;
        result.post_despill_applied = materialized.post_despill_applied;
        result.frame.external_output_written = materialized.external_output_written;
#if defined(CORRIDORKEY_HAS_CUDA) && CORRIDORKEY_HAS_CUDA
        emit_deferred_cuda_event_elapsed(on_stage, direct_forward_timing);
#endif
    });
    return result;
}
// NOLINTEND(bugprone-easily-swappable-parameters)

}  // namespace

class TorchTrtSession::Impl {
   public:
    Impl()
        : work_stream(c10::cuda::getStreamFromPool(true)),
          copy_stream(c10::cuda::getStreamFromPool(false)),
          capture_stream(c10::cuda::getStreamFromPool(true)) {}

    torch::jit::script::Module module;
    ExternalPosState external_pos;
    PinnedHostTensorRing host_ring;
    PinnedImageBufferPool output_pool;
    c10::cuda::CUDAStream work_stream;
    c10::cuda::CUDAStream copy_stream;
    c10::cuda::CUDAStream capture_stream;
    ForwardGraphState forward_graph;
    int resolution = 0;
    // Engine input dtype - inferred from filename (corridorkey_*_fp32_<res>.ts
    // vs corridorkey_*_fp16_<res>.ts). Sprint 0 found blue 1536+ needs FP32
    // because FP16 NaNs in LayerNorm/Softmax for the blue checkpoint; green
    // is stable at FP16 across the full ladder.
    torch::Dtype input_dtype = torch::kFloat16;
    DeviceInfo device;
};

namespace {
torch::Dtype infer_input_dtype(const std::filesystem::path& path) {
    auto stem = path.stem().string();
    if (stem.find("fp32") != std::string::npos) {
        return torch::kFloat32;
    }
    return torch::kFloat16;
}
}  // namespace

TorchTrtSession::TorchTrtSession() : m_impl(std::make_unique<Impl>()) {}
TorchTrtSession::~TorchTrtSession() = default;
TorchTrtSession::TorchTrtSession(TorchTrtSession&&) noexcept = default;
TorchTrtSession& TorchTrtSession::operator=(TorchTrtSession&&) noexcept = default;

Result<std::unique_ptr<TorchTrtSession>> TorchTrtSession::create(
    const std::filesystem::path& ts_path, const DeviceInfo& device,
    StageTimingCallback
        on_stage) {  // NOLINT(performance-unnecessary-value-param) — matches MlxSession signature.
    if (!std::filesystem::exists(ts_path)) {
        return Unexpected<Error>{
            Error{.code = ErrorCode::ModelLoadFailed,
                  .message = "TorchTRT engine not found: " + ts_path.string()}};
    }

    // Strategy C, Sprint 1 PR 1 follow-up: the caller is responsible for
    // arming the runtime via torch_trt_loader::arm_torchtrt_runtime
    // BEFORE invoking any symbol from this DLL. By the time control
    // reaches this function, the OS has already resolved every torch /
    // torchtrt / cuda dependency through the delay-loaded
    // corridorkey_torchtrt.dll, which means AddDllDirectory inside this
    // TU is too late.

    if (!torch::cuda::is_available()) {
        return Unexpected<Error>{Error{
            .code = ErrorCode::HardwareNotSupported,
            .message = "CUDA not available; TorchTRT engines require an Ampere or newer GPU."}};
    }

    auto session = std::unique_ptr<TorchTrtSession>(new TorchTrtSession());
    session->m_impl->device = device;

    auto inferred_res = resolution_from_filename(ts_path);
    session->m_impl->resolution = inferred_res.value_or(0);
    session->m_impl->input_dtype = infer_input_dtype(ts_path);
    const bool cuda_graph_requested = torchtrt_cuda_graph_requested();
    const bool has_true_torchtrt_marker = artifact_contains_true_torchtrt_marker(ts_path);
    session->m_impl->forward_graph.enabled = cuda_graph_requested && has_true_torchtrt_marker;
    if (session->m_impl->forward_graph.enabled) {
        emit_graph_event(on_stage, "torchtrt_cuda_graph_config_enabled");
    } else if (cuda_graph_requested) {
        emit_graph_event(on_stage, "torchtrt_cuda_graph_config_marker_missing");
    } else {
        emit_graph_event(on_stage, "torchtrt_cuda_graph_config_env_disabled");
    }

    try {
        std::unordered_map<std::string, std::string> extra_files{
            {std::string{kExternalPosMetaName}, {}},
            {std::string{kExternalPosDataName}, {}},
        };
        common::measure_stage(on_stage, "torchtrt_jit_load", [&]() {
            session->m_impl->module =
                torch::jit::load(ts_path.string(), torch::Device(torch::kCUDA), extra_files);
            session->m_impl->module.eval();
        });
        auto external_pos = parse_external_pos_state(extra_files);
        if (!external_pos.has_value()) {
            return Unexpected<Error>{external_pos.error()};
        }
        session->m_impl->external_pos = std::move(*external_pos);
    } catch (const c10::Error& e) {
        return Unexpected<Error>{
            Error{.code = ErrorCode::ModelLoadFailed,
                  .message = std::string("torch::jit::load failed: ") + e.what()}};
    } catch (const std::exception& e) {
        return Unexpected<Error>{
            Error{.code = ErrorCode::ModelLoadFailed,
                  .message = std::string("torch::jit::load std::exception: ") + e.what()}};
    }

    return session;
}

int TorchTrtSession::model_resolution() const {
    return m_impl ? m_impl->resolution : 0;
}

TorchTrtCudaStream TorchTrtSession::current_cuda_stream() const {
#if defined(CORRIDORKEY_HAS_CUDA) && CORRIDORKEY_HAS_CUDA
    if (m_impl == nullptr) {
        return {};
    }
    return TorchTrtCudaStream{
        .handle = m_impl->work_stream.stream(),
        .available = true,
    };
#else
    return {};
#endif
}

Result<TorchTrtFrameResult> TorchTrtSession::infer(
    const Image& rgb, const Image& alpha_hint, bool output_alpha_only,
    // NOLINTNEXTLINE(performance-unnecessary-value-param)
    StageTimingCallback on_stage) {
    if (m_impl == nullptr) {
        return Unexpected<Error>{Error{.code = ErrorCode::InferenceFailed,
                                       .message = "TorchTrtSession in moved-from state"}};
    }
    const int fixed_resolution = m_impl->resolution;
    const bool dynamic_resolution = fixed_resolution == 0;
    const int width = rgb.width;
    const int height = rgb.height;
    if (rgb.channels != 3 || width <= 0 || height <= 0) {
        return Unexpected<Error>{Error{
            .code = ErrorCode::InvalidParameters,
            .message = "TorchScript RTX session expects RGB input with positive width/height and "
                       "3 channels; got " +
                       std::to_string(rgb.width) + "x" + std::to_string(rgb.height) + "x" +
                       std::to_string(rgb.channels)}};
    }
    if (!dynamic_resolution && (width != fixed_resolution || height != fixed_resolution)) {
        return Unexpected<Error>{Error{
            .code = ErrorCode::InvalidParameters,
            .message = "TorchTRT session expects input at " + std::to_string(fixed_resolution) +
                       "x" + std::to_string(fixed_resolution) + "; got " + std::to_string(width) +
                       "x" + std::to_string(height)}};
    }
    if (alpha_hint.width != width || alpha_hint.height != height || alpha_hint.channels != 1) {
        return Unexpected<Error>{Error{.code = ErrorCode::InvalidParameters,
                                       .message = "TorchScript RTX session expects alpha_hint at " +
                                                  std::to_string(width) + "x" +
                                                  std::to_string(height) + "x1"}};
    }

    try {
        const c10::cuda::CUDAStreamGuard work_guard(m_impl->work_stream);
        emit_graph_event(on_stage, "torchtrt_work_stream_guard");
        const DynamicPadding padding = dynamic_padding_for_input(width, height, dynamic_resolution);
        const int inference_width = padding.width;
        const int inference_height = padding.height;
        torch::Tensor host_input;
        common::measure_stage(on_stage, "torchtrt_prepare_pack", [&]() {
            host_input = allocate_host_input_tensor(inference_height, inference_width);
            const float* rgb_data = rgb.data.data();
            const float* hint_data = alpha_hint.data.data();
            auto* dst = host_input.data_ptr<float>();
            const auto inference_plane =
                static_cast<std::ptrdiff_t>(inference_width) * inference_height;
            for (int row = 0; row < inference_height; ++row) {
                const int src_y = reflect_index(row - padding.top, height);
                for (int column = 0; column < inference_width; ++column) {
                    const int src_x = reflect_index(column - padding.left, width);
                    const auto dst_index =
                        (static_cast<std::ptrdiff_t>(row) * inference_width) + column;
                    const auto reflected_index =
                        (static_cast<std::ptrdiff_t>(src_y) * width) + src_x;
                    dst[(0 * inference_plane) + dst_index] = normalize_corridorkey_rgb(
                        rgb_data[(reflected_index * 3) + 0], ModelRgbChannel::Red);
                    dst[(1 * inference_plane) + dst_index] = normalize_corridorkey_rgb(
                        rgb_data[(reflected_index * 3) + 1], ModelRgbChannel::Green);
                    dst[(2 * inference_plane) + dst_index] = normalize_corridorkey_rgb(
                        rgb_data[(reflected_index * 3) + 2], ModelRgbChannel::Blue);
                    dst[(3 * inference_plane) + dst_index] = hint_data[reflected_index];
                }
            }
        });
        torch::Tensor cuda_input;
        common::measure_stage(on_stage, "torchtrt_prepare_upload", [&]() {
            constexpr bool kNonBlockingCopy = true;
            cuda_input =
                host_input.to(torch::Device(torch::kCUDA), m_impl->input_dtype, kNonBlockingCopy);
        });

        return forward_and_materialize(
            m_impl->module, m_impl->external_pos, m_impl->forward_graph, cuda_input, width, height,
            width, height, inference_width, inference_height, padding.top, padding.left,
            m_impl->input_dtype, output_alpha_only, OutputResizeFilter::Bilinear,
            m_impl->host_ring, m_impl->output_pool, m_impl->copy_stream,
            m_impl->capture_stream, on_stage);
    } catch (const c10::Error& e) {
        return Unexpected<Error>{
            Error{.code = ErrorCode::InferenceFailed,
                  .message = std::string("TorchTRT forward c10 error: ") + e.what()}};
    } catch (const std::exception& e) {
        return Unexpected<Error>{
            Error{.code = ErrorCode::InferenceFailed,
                  .message = std::string("TorchTRT forward std::exception: ") + e.what()}};
    }
}

Result<TorchTrtFrameResult> TorchTrtSession::infer_prepared_planar(
    const float* planar_input, int input_width, int input_height, bool output_alpha_only,
    // NOLINTNEXTLINE(performance-unnecessary-value-param)
    StageTimingCallback on_stage) {
    if (m_impl == nullptr) {
        return Unexpected<Error>{Error{.code = ErrorCode::InferenceFailed,
                                       .message = "TorchTrtSession in moved-from state"}};
    }
    if (planar_input == nullptr || input_width <= 0 || input_height <= 0) {
        return Unexpected<Error>{
            Error{.code = ErrorCode::InvalidParameters,
                  .message = "Prepared TorchTRT input must be a non-empty planar tensor"}};
    }

    try {
        const c10::cuda::CUDAStreamGuard work_guard(m_impl->work_stream);
        emit_graph_event(on_stage, "torchtrt_work_stream_guard");
        torch::Tensor host_input;
        common::measure_stage(on_stage, "torchtrt_prepare_planar_copy", [&]() {
            host_input = allocate_host_input_tensor(input_height, input_width);
            const auto input_count = static_cast<std::size_t>(4) *
                                     static_cast<std::size_t>(input_width) *
                                     static_cast<std::size_t>(input_height);
            std::memcpy(host_input.data_ptr<float>(), planar_input, input_count * sizeof(float));
        });

        torch::Tensor cuda_input;
        common::measure_stage(on_stage, "torchtrt_prepare_upload", [&]() {
            constexpr bool kNonBlockingCopy = true;
            cuda_input =
                host_input.to(torch::Device(torch::kCUDA), m_impl->input_dtype, kNonBlockingCopy);
        });

        return forward_and_materialize(
            m_impl->module, m_impl->external_pos, m_impl->forward_graph, cuda_input, input_width,
            input_height, input_width, input_height, input_width, input_height, 0, 0,
            m_impl->input_dtype, output_alpha_only, OutputResizeFilter::Bilinear,
            m_impl->host_ring, m_impl->output_pool, m_impl->copy_stream,
            m_impl->capture_stream, on_stage);
    } catch (const c10::Error& e) {
        return Unexpected<Error>{
            Error{.code = ErrorCode::InferenceFailed,
                  .message = std::string("TorchTRT prepared forward c10 error: ") + e.what()}};
    } catch (const std::exception& e) {
        return Unexpected<Error>{
            Error{.code = ErrorCode::InferenceFailed,
                  .message = std::string("TorchTRT prepared forward std::exception: ") + e.what()}};
    }
}

Result<TorchTrtFrameResult> TorchTrtSession::infer_prepared_cuda_planar(
    void* planar_device_input, int input_width, int input_height, bool output_alpha_only,
    // NOLINTNEXTLINE(performance-unnecessary-value-param)
    StageTimingCallback on_stage, void* input_ready_event, void* input_ready_start_event,
    bool input_ready_event_on_current_stream, const InferenceParams* post_process_params,
    TorchTrtDeviceSource source, FrameOutputViews output_views) {
    return infer_prepared_cuda_planar_resized(planar_device_input, input_width, input_height,
                                              input_width, input_height, output_alpha_only,
                                              false, std::move(on_stage), input_ready_event,
                                              input_ready_start_event,
                                              input_ready_event_on_current_stream,
                                              post_process_params, source, output_views);
}

Result<TorchTrtFrameResult> TorchTrtSession::infer_prepared_cuda_planar_resized(
    void* planar_device_input, int input_width, int input_height, int output_width,
    int output_height, bool output_alpha_only, bool use_lanczos_resize,
    // NOLINTNEXTLINE(performance-unnecessary-value-param)
    StageTimingCallback on_stage, void* input_ready_event, void* input_ready_start_event,
    bool input_ready_event_on_current_stream, const InferenceParams* post_process_params,
    TorchTrtDeviceSource source, FrameOutputViews output_views) {
    if (m_impl == nullptr) {
        return Unexpected<Error>{Error{.code = ErrorCode::InferenceFailed,
                                       .message = "TorchTrtSession in moved-from state"}};
    }
    if (planar_device_input == nullptr || input_width <= 0 || input_height <= 0 ||
        output_width <= 0 || output_height <= 0) {
        return Unexpected<Error>{
            Error{.code = ErrorCode::InvalidParameters,
                  .message = "Prepared CUDA TorchTRT input and output sizes must be positive"}};
    }

    try {
        const c10::cuda::CUDAStreamGuard work_guard(m_impl->work_stream);
        emit_graph_event(on_stage, "torchtrt_work_stream_guard");
        auto wait_res = wait_for_external_cuda_event(input_ready_event, input_ready_start_event,
                                                     input_ready_event_on_current_stream,
                                                     on_stage);
        if (!wait_res) {
            return Unexpected<Error>{wait_res.error()};
        }

        torch::Tensor cuda_input;
        common::measure_stage(on_stage, "torchtrt_prepare_device_wrap", [&]() {
            auto options = torch::TensorOptions().dtype(torch::kFloat32).device(torch::kCUDA);
            cuda_input =
                torch::from_blob(planar_device_input, {1, 4, input_height, input_width}, options);
        });

        if (m_impl->input_dtype != torch::kFloat32) {
            common::measure_stage(on_stage, "torchtrt_prepare_device_cast",
                                  [&]() { cuda_input = cuda_input.to(m_impl->input_dtype); });
        }

        auto frame_res = forward_and_materialize(
            m_impl->module, m_impl->external_pos, m_impl->forward_graph, cuda_input, input_width,
            input_height, output_width, output_height, input_width, input_height, 0, 0,
            m_impl->input_dtype, output_alpha_only,
            use_lanczos_resize ? OutputResizeFilter::Lanczos : OutputResizeFilter::Bilinear,
            m_impl->host_ring, m_impl->output_pool, m_impl->copy_stream,
            m_impl->capture_stream, on_stage, post_process_params, source, output_views);
        if (frame_res.has_value()) {
            emit_cuda_event_elapsed(on_stage, "gpu_prepare_device", input_ready_start_event,
                                    input_ready_event);
        }
        return frame_res;
    } catch (const c10::Error& e) {
        return Unexpected<Error>{
            Error{.code = ErrorCode::InferenceFailed,
                  .message = std::string("TorchTRT prepared CUDA forward c10 error: ") + e.what()}};
    } catch (const std::exception& e) {
        return Unexpected<Error>{Error{
            .code = ErrorCode::InferenceFailed,
            .message = std::string("TorchTRT prepared CUDA forward std::exception: ") + e.what()}};
    }
}

}  // namespace corridorkey::core
