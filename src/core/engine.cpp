#include <algorithm>
#include <cctype>
#include <corridorkey/detail/warmup_policy.hpp>
#include <corridorkey/engine.hpp>
#include <corridorkey/frame_io.hpp>
#include <deque>
#include <future>
#include <mutex>
#include <string_view>

#include "../frame_io/video_io.hpp"
#include "../post_process/color_utils.hpp"
#include "common/stage_profiler.hpp"
#include "engine_internal.hpp"
#include "inference_session.hpp"
#include "mlx_probe.hpp"
#include "ort_process_context.hpp"
#include "warmup_policy.hpp"

namespace corridorkey {

class Engine::Impl {
   public:
    std::unique_ptr<InferenceSession> session;
    std::filesystem::path model_path;
    std::optional<DeviceInfo> cpu_fallback_device;
    std::optional<BackendFallbackInfo> fallback_info;
    EngineCreateOptions create_options = {};
    std::shared_ptr<core::OrtProcessContext> ort_process_context = nullptr;
    std::mutex warmup_mutex;
    std::optional<int> last_warmup_resolution;
    std::optional<Error> warmup_error;

    Impl() = default;

    bool can_fallback_to_cpu() const {
        return create_options.allow_cpu_fallback && cpu_fallback_device.has_value() &&
               session != nullptr && session->device().backend != Backend::CPU;
    }

    Result<void> activate_cpu_fallback(std::string_view phase, const std::string& reason) {
        if (!can_fallback_to_cpu()) {
            return {};
        }

        Backend failed_backend = session->device().backend;
        SessionCreateOptions session_options;
        session_options.ort_process_context = ort_process_context;
        auto fallback_res =
            InferenceSession::create(model_path, *cpu_fallback_device, session_options);
        if (!fallback_res) {
            return Unexpected(fallback_res.error());
        }

        session = std::move(*fallback_res);
        fallback_info = BackendFallbackInfo{failed_backend, session->device().backend,
                                            std::string(phase) + ": " + reason};
        return {};
    }

    bool should_retry_on_cpu(const Error& error) const {
        return error.code == ErrorCode::InferenceFailed ||
               error.code == ErrorCode::HardwareNotSupported;
    }

    template <typename T, typename Operation>
    Result<T> run_with_cpu_fallback(std::string_view phase, Operation&& operation) {
        auto result = operation();
        if (result || !can_fallback_to_cpu() || !should_retry_on_cpu(result.error())) {
            return result;
        }

        auto fallback_res = activate_cpu_fallback(phase, result.error().message);
        if (!fallback_res) {
            return result;
        }

        return operation();
    }

    Result<void> ensure_warmup(StageTimingCallback on_stage, int target_resolution) {
        std::lock_guard<std::mutex> lock(warmup_mutex);

        int recommended = session != nullptr ? session->recommended_resolution() : 512;
        int desired_resolution = detail::resolve_warmup_resolution(target_resolution, recommended);
        int warmup_workload_resolution = std::max(desired_resolution, recommended);

        if (session != nullptr &&
            core::should_skip_warmup(session->device().backend, warmup_workload_resolution)) {
            last_warmup_resolution = desired_resolution;
            warmup_error.reset();
            return {};
        }

        if (!detail::should_run_warmup(desired_resolution, last_warmup_resolution)) {
            if (warmup_error.has_value()) {
                return Unexpected(*warmup_error);
            }
            return {};
        }

        ImageBuffer warm_rgb(64, 64, 3);
        ImageBuffer warm_hint(64, 64, 1);
        std::fill(warm_rgb.view().data.begin(), warm_rgb.view().data.end(), 0.0f);
        std::fill(warm_hint.view().data.begin(), warm_hint.view().data.end(), 0.0f);

        InferenceParams warm_params;
        warm_params.target_resolution = desired_resolution;

        auto warmup_frame = common::measure_stage(on_stage, "engine_warmup", [&]() {
            return run_with_cpu_fallback<FrameResult>("warmup", [&]() {
                return session->run(warm_rgb.view(), warm_hint.view(), warm_params, on_stage);
            });
        });

        last_warmup_resolution = desired_resolution;
        if (!warmup_frame) {
            warmup_error = warmup_frame.error();
            return Unexpected(*warmup_error);
        }

        warmup_error.reset();
        return {};
    }
};

namespace {

bool is_mlx_artifact(const std::filesystem::path& model_path) {
    auto extension = model_path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return extension == ".safetensors" || extension == ".mlxfn";
}

DeviceInfo resolve_auto_device_for_model(const std::filesystem::path& model_path) {
#if defined(__APPLE__)
    DeviceInfo detected = auto_detect();

    if (is_mlx_artifact(model_path)) {
        return DeviceInfo{"Apple Silicon MLX", detected.available_memory_mb, Backend::MLX};
    }

    auto extension = model_path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    if (extension == ".onnx") {
        return DeviceInfo{"Generic CPU", detected.available_memory_mb, Backend::CPU};
    }

    return detected;
#else
    (void)model_path;
    return auto_detect();
#endif
}

std::optional<DeviceInfo> build_cpu_fallback_device(const DeviceInfo& device) {
#if defined(__APPLE__)
    if (device.backend == Backend::CoreML || device.backend == Backend::Auto) {
        return DeviceInfo{"Generic CPU", device.available_memory_mb, Backend::CPU};
    }
#elif defined(_WIN32)
    if (device.backend == Backend::TensorRT || device.backend == Backend::Auto ||
        device.backend == Backend::WindowsML || device.backend == Backend::OpenVINO) {
        return DeviceInfo{"Generic CPU", 0, Backend::CPU};
    }
#endif
    return std::nullopt;
}

}  // namespace

Engine::Engine() : m_impl(std::make_unique<Impl>()) {}

Engine::~Engine() = default;

Engine::Engine(Engine&&) noexcept = default;
Engine& Engine::operator=(Engine&&) noexcept = default;

Result<std::unique_ptr<Engine>> Engine::create(const std::filesystem::path& model_path,
                                               DeviceInfo device, StageTimingCallback on_stage,
                                               EngineCreateOptions options) {
    return core::EngineFactory::create_with_ort_process_context(
        model_path, device, std::make_shared<core::OrtProcessContext>(), on_stage, options);
}

Result<std::unique_ptr<Engine>> core::EngineFactory::create_with_ort_process_context(
    const std::filesystem::path& model_path, DeviceInfo device,
    std::shared_ptr<core::OrtProcessContext> ort_process_context, StageTimingCallback on_stage,
    EngineCreateOptions options) {
    auto engine = std::unique_ptr<Engine>(new Engine());
    engine->m_impl->model_path = model_path;
    engine->m_impl->create_options = options;
    engine->m_impl->ort_process_context = ort_process_context
                                              ? std::move(ort_process_context)
                                              : std::make_shared<core::OrtProcessContext>();

    DeviceInfo requested_device =
        device.backend == Backend::Auto ? resolve_auto_device_for_model(model_path) : device;
    if (options.allow_cpu_fallback) {
        engine->m_impl->cpu_fallback_device = build_cpu_fallback_device(requested_device);
    }

    SessionCreateOptions session_options;
    session_options.disable_cpu_ep_fallback = options.disable_cpu_ep_fallback;
    session_options.ort_process_context = engine->m_impl->ort_process_context;

    auto session_res = common::measure_stage(on_stage, "session_create_requested", [&]() {
        return InferenceSession::create(model_path, requested_device, session_options, on_stage);
    });
    if (!session_res && engine->m_impl->cpu_fallback_device.has_value()) {
        SessionCreateOptions fallback_options;
        fallback_options.ort_process_context = engine->m_impl->ort_process_context;
        auto fallback_res = common::measure_stage(on_stage, "session_create_cpu_fallback", [&]() {
            return InferenceSession::create(model_path, *engine->m_impl->cpu_fallback_device,
                                            fallback_options, on_stage);
        });
        if (!fallback_res) {
            return Unexpected(session_res.error());
        }
        engine->m_impl->session = std::move(*fallback_res);
        engine->m_impl->fallback_info =
            BackendFallbackInfo{requested_device.backend, Backend::CPU,
                                std::string("session_create: ") + session_res.error().message};
    } else if (!session_res) {
        return Unexpected(session_res.error());
    } else {
        engine->m_impl->session = std::move(*session_res);
    }

    return engine;
}

Result<FrameResult> Engine::process_frame(const Image& rgb, const Image& alpha_hint,
                                          const InferenceParams& params,
                                          StageTimingCallback on_stage) {
    if (!m_impl->session) {
        return Unexpected(Error{ErrorCode::ModelLoadFailed, "Engine not initialized"});
    }

    auto warmup_res = m_impl->ensure_warmup(on_stage, params.target_resolution);
    if (!warmup_res) {
        return Unexpected(warmup_res.error());
    }

    return m_impl->run_with_cpu_fallback<FrameResult>(
        "render_frame", [&]() { return m_impl->session->run(rgb, alpha_hint, params, on_stage); });
}

Result<std::vector<FrameResult>> Engine::process_frame_batch(const std::vector<Image>& rgbs,
                                                             const std::vector<Image>& alpha_hints,
                                                             const InferenceParams& params,
                                                             StageTimingCallback on_stage) {
    if (!m_impl->session) {
        return Unexpected(Error{ErrorCode::ModelLoadFailed, "Engine not initialized"});
    }

    auto warmup_res = m_impl->ensure_warmup(on_stage, params.target_resolution);
    if (!warmup_res) {
        return Unexpected(warmup_res.error());
    }

    return m_impl->run_with_cpu_fallback<std::vector<FrameResult>>("render_batch", [&]() {
        return m_impl->session->run_batch(rgbs, alpha_hints, params, on_stage);
    });
}

Result<void> Engine::process_sequence(const std::vector<std::filesystem::path>& inputs,
                                      const std::vector<std::filesystem::path>& alpha_hints,
                                      const std::filesystem::path& output_dir,
                                      const InferenceParams& params, ProgressCallback on_progress,
                                      StageTimingCallback on_stage) {
    bool has_hints = !alpha_hints.empty();
    if (has_hints && inputs.size() != alpha_hints.size()) {
        return Unexpected(
            Error{ErrorCode::InvalidParameters, "Inputs and alpha hints size mismatch"});
    }

    auto warmup_res = m_impl->ensure_warmup(on_stage, params.target_resolution);
    if (!warmup_res) {
        return Unexpected(warmup_res.error());
    }

    size_t total_frames = inputs.size();
    int batch_size = std::max(1, params.batch_size);

    for (size_t i = 0; i < total_frames; i += batch_size) {
        size_t current_batch_size = std::min((size_t)batch_size, total_frames - i);

        if (on_progress && !on_progress(static_cast<float>(i) / total_frames,
                                        "Processing batch " + std::to_string(i / batch_size))) {
            return Unexpected(Error{ErrorCode::Cancelled, "Processing cancelled by user"});
        }

        std::vector<ImageBuffer> rgb_bufs;
        std::vector<ImageBuffer> hint_bufs;
        std::vector<Image> rgb_views;
        std::vector<Image> hint_views;

        for (size_t b = 0; b < current_batch_size; ++b) {
            auto rgb_res = common::measure_stage(
                on_stage, "sequence_read_input",
                [&]() { return frame_io::read_frame(inputs[i + b]); }, 1);
            if (!rgb_res) return Unexpected(rgb_res.error());

            ImageBuffer hint_buf;
            if (has_hints) {
                auto hint_res = common::measure_stage(
                    on_stage, "sequence_read_hint",
                    [&]() { return frame_io::read_frame(alpha_hints[i + b]); }, 1);
                if (!hint_res) return Unexpected(hint_res.error());
                hint_buf = std::move(*hint_res);
            } else {
                hint_buf = ImageBuffer(rgb_res->view().width, rgb_res->view().height, 1);
                common::measure_stage(
                    on_stage, "sequence_generate_hint",
                    [&]() { ColorUtils::generate_rough_matte(rgb_res->view(), hint_buf.view()); },
                    1);
            }

            rgb_views.push_back(rgb_res->view());
            hint_views.push_back(hint_buf.view());
            rgb_bufs.push_back(std::move(*rgb_res));
            hint_bufs.push_back(std::move(hint_buf));
        }

        auto results = common::measure_stage(
            on_stage, "sequence_infer_batch",
            [&]() {
                return m_impl->run_with_cpu_fallback<std::vector<FrameResult>>(
                    "sequence_infer_batch", [&]() {
                        return m_impl->session->run_batch(rgb_views, hint_views, params, on_stage);
                    });
            },
            current_batch_size);
        if (!results) return Unexpected(results.error());

        for (size_t b = 0; b < current_batch_size; ++b) {
            std::string filename = inputs[i + b].filename().string();
            auto save_res = common::measure_stage(
                on_stage, "sequence_write_output",
                [&]() { return frame_io::save_result(output_dir, filename, (*results)[b]); }, 1);
            if (!save_res) return save_res;
        }
    }

    return {};
}

Result<void> Engine::process_video(const std::filesystem::path& input_video,
                                   const std::filesystem::path& hint_video,
                                   const std::filesystem::path& output_video,
                                   const InferenceParams& params, ProgressCallback on_progress,
                                   StageTimingCallback on_stage) {
    VideoOutputOptions output_options;
    output_options.mode = VideoOutputMode::Lossless;
    return process_video(input_video, hint_video, output_video, params, output_options, on_progress,
                         on_stage);
}

Result<void> Engine::process_video(const std::filesystem::path& input_video,
                                   const std::filesystem::path& hint_video,
                                   const std::filesystem::path& output_video,
                                   const InferenceParams& params,
                                   const VideoOutputOptions& output_options,
                                   ProgressCallback on_progress, StageTimingCallback on_stage) {
    if (!m_impl->session) {
        return Unexpected(Error{ErrorCode::InferenceFailed, "Engine not initialized"});
    }

    auto warmup_res = m_impl->ensure_warmup(on_stage, params.target_resolution);
    if (!warmup_res) {
        return Unexpected(warmup_res.error());
    }

    auto reader_rgb_res = common::measure_stage(on_stage, "video_open_reader",
                                                [&]() { return VideoReader::open(input_video); });
    if (!reader_rgb_res) return Unexpected(reader_rgb_res.error());
    auto reader_rgb = std::move(*reader_rgb_res);

    bool has_hint_video = !hint_video.empty();
    std::unique_ptr<VideoReader> reader_hint;
    if (has_hint_video) {
        auto reader_hint_res = common::measure_stage(
            on_stage, "video_open_hint_reader", [&]() { return VideoReader::open(hint_video); });
        if (!reader_hint_res) return Unexpected(reader_hint_res.error());
        reader_hint = std::move(*reader_hint_res);
    }

    int out_w = reader_rgb->width();
    int out_h = reader_rgb->height();

    struct PrefetchedFrame {
        VideoFrame rgb_frame;
        ImageBuffer hint_buffer;
    };

    std::deque<PrefetchedFrame> prefetched_frames;

    auto read_frame_pair = [&]() -> Result<std::optional<PrefetchedFrame>> {
        auto rgb_res = common::measure_stage(
            on_stage, "video_decode_frame", [&]() { return reader_rgb->read_next_frame(); }, 1);
        if (!rgb_res || rgb_res->buffer.view().empty()) {
            return std::optional<PrefetchedFrame>{};
        }

        VideoFrame rgb_frame = std::move(*rgb_res);
        ImageBuffer hint_buf;
        if (has_hint_video) {
            auto hint_res = common::measure_stage(
                on_stage, "video_decode_hint", [&]() { return reader_hint->read_next_frame(); }, 1);
            if (!hint_res || hint_res->buffer.view().empty()) {
                return std::optional<PrefetchedFrame>{};
            }
            hint_buf = std::move(hint_res->buffer);
        } else {
            hint_buf =
                ImageBuffer(rgb_frame.buffer.view().width, rgb_frame.buffer.view().height, 1);
            common::measure_stage(
                on_stage, "video_generate_hint",
                [&]() {
                    ColorUtils::generate_rough_matte(rgb_frame.buffer.view(), hint_buf.view());
                },
                1);
        }

        return std::optional<PrefetchedFrame>{
            PrefetchedFrame{std::move(rgb_frame), std::move(hint_buf)}};
    };

    auto first_pair_res = read_frame_pair();
    if (!first_pair_res) {
        return Unexpected(first_pair_res.error());
    }
    if (!first_pair_res->has_value()) {
        return Unexpected(Error{ErrorCode::IoError, "FFmpeg: Input video contains no frames"});
    }
    prefetched_frames.push_back(std::move(**first_pair_res));

    auto second_pair_res = read_frame_pair();
    if (!second_pair_res) {
        return Unexpected(second_pair_res.error());
    }
    if (second_pair_res->has_value()) {
        prefetched_frames.push_back(std::move(**second_pair_res));
    }

    std::optional<double> derived_fps;
    if (prefetched_frames.size() >= 2) {
        const auto& first_pts = prefetched_frames[0].rgb_frame.pts_us;
        const auto& second_pts = prefetched_frames[1].rgb_frame.pts_us;
        if (first_pts.has_value() && second_pts.has_value()) {
            int64_t delta = *second_pts - *first_pts;
            if (delta > 0) {
                derived_fps = 1000000.0 / static_cast<double>(delta);
            }
        }
    }

    double output_fps = derived_fps.value_or(reader_rgb->fps());
    auto input_time_base = reader_rgb->time_base();

    auto writer_res = common::measure_stage(on_stage, "video_open_writer", [&]() {
        return VideoWriter::open(output_video, out_w, out_h, output_fps, reader_rgb->format(),
                                 output_options, "", input_time_base);
    });
    if (!writer_res) return Unexpected(writer_res.error());
    auto writer = std::move(*writer_res);

    int64_t total_frames = reader_rgb->total_frames();
    int batch_size = std::max(1, params.batch_size);
    int64_t frame_idx = 0;

    struct Batch {
        std::vector<ImageBuffer> rgb_bufs;
        std::vector<ImageBuffer> hint_bufs;
        std::vector<Image> rgb_views;
        std::vector<Image> hint_views;
        std::vector<std::optional<int64_t>> pts_us;
    };

    auto fetch_batch = [&](int size) -> Result<Batch> {
        Batch b;
        for (int i = 0; i < size; ++i) {
            std::optional<PrefetchedFrame> pair;
            if (!prefetched_frames.empty()) {
                pair = std::move(prefetched_frames.front());
                prefetched_frames.pop_front();
            } else {
                auto pair_res = read_frame_pair();
                if (!pair_res) {
                    return Unexpected(pair_res.error());
                }
                if (!pair_res->has_value()) {
                    break;
                }
                pair = std::move(**pair_res);
            }

            b.rgb_views.push_back(pair->rgb_frame.buffer.view());
            b.hint_views.push_back(pair->hint_buffer.view());
            b.rgb_bufs.push_back(std::move(pair->rgb_frame.buffer));
            b.hint_bufs.push_back(std::move(pair->hint_buffer));
            b.pts_us.push_back(pair->rgb_frame.pts_us);
        }
        return b;
    };

    // Initial pre-fetch
    auto current_batch_future = std::async(std::launch::async, fetch_batch, batch_size);
    std::future<Result<void>> pending_encode;
    bool has_pending_encode = false;

    auto wait_for_pending_encode = [&]() -> Result<void> {
        if (!has_pending_encode) {
            return {};
        }

        auto encode_res = common::measure_stage(on_stage, "video_wait_for_encode",
                                                [&]() { return pending_encode.get(); });
        has_pending_encode = false;
        if (!encode_res) {
            return Unexpected(encode_res.error());
        }
        return {};
    };

    while (true) {
        auto current_batch_res = current_batch_future.get();
        if (!current_batch_res) return Unexpected(current_batch_res.error());
        Batch current_batch = std::move(*current_batch_res);

        if (current_batch.rgb_views.empty()) break;

        // Start pre-fetching the NEXT batch immediately
        current_batch_future = std::async(std::launch::async, fetch_batch, batch_size);

        if (on_progress) {
            float p = total_frames > 0 ? static_cast<float>(frame_idx) / total_frames : 0.0f;
            if (!on_progress(p, "Inference frames " + std::to_string(frame_idx))) {
                return Unexpected(Error{ErrorCode::Cancelled, "Processing cancelled by user"});
            }
        }

        // GPU Inference on the CURRENT batch
        auto results = common::measure_stage(
            on_stage, "video_infer_batch",
            [&]() {
                return m_impl->run_with_cpu_fallback<std::vector<FrameResult>>(
                    "video_infer_batch", [&]() {
                        return m_impl->session->run_batch(
                            current_batch.rgb_views, current_batch.hint_views, params, on_stage);
                    });
            },
            current_batch.rgb_views.size());
        if (!results) return Unexpected(results.error());

        auto pending_res = wait_for_pending_encode();
        if (!pending_res) {
            return Unexpected(pending_res.error());
        }

        auto frames_to_encode = std::move(*results);
        auto pts_to_encode = std::move(current_batch.pts_us);
        has_pending_encode = true;
        pending_encode = std::async(
            std::launch::async,
            [frames = std::move(frames_to_encode), pts = std::move(pts_to_encode), &writer,
             on_stage]() mutable -> Result<void> {
                for (size_t index = 0; index < frames.size(); ++index) {
                    auto pts_value = index < pts.size() ? pts[index] : std::nullopt;
                    auto write_res = common::measure_stage(
                        on_stage, "video_encode_frame",
                        [&]() {
                            return writer->write_frame(frames[index].composite.view(), pts_value);
                        },
                        1);
                    if (!write_res) {
                        return Unexpected(write_res.error());
                    }
                }
                return {};
            });

        frame_idx += current_batch.rgb_views.size();
    }

    auto final_encode_res = wait_for_pending_encode();
    if (!final_encode_res) {
        return Unexpected(final_encode_res.error());
    }

    auto flush_res =
        common::measure_stage(on_stage, "video_flush_writer", [&]() { return writer->finalize(); });
    if (!flush_res) {
        return Unexpected(flush_res.error());
    }
    writer.reset();

    if (on_progress) {
        on_progress(1.0f, "Done");
    }

    return {};
}

int Engine::recommended_resolution() const {
    return m_impl->session ? m_impl->session->recommended_resolution() : 512;
}

DeviceInfo Engine::current_device() const {
    return m_impl->session ? m_impl->session->device()
                           : DeviceInfo{"Not Initialized", 0, Backend::Auto};
}

std::optional<BackendFallbackInfo> Engine::backend_fallback() const {
    return m_impl ? m_impl->fallback_info : std::nullopt;
}

}  // namespace corridorkey
