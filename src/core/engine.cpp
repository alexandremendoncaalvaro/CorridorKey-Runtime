#include <corridorkey/engine.hpp>
#include <corridorkey/frame_io.hpp>
#include <deque>
#include <future>

#include "../frame_io/video_io.hpp"
#include "../post_process/color_utils.hpp"
#include "inference_session.hpp"

namespace corridorkey {

class Engine::Impl {
   public:
    std::unique_ptr<InferenceSession> session;
    std::filesystem::path model_path;
    std::optional<DeviceInfo> cpu_fallback_device;
    std::optional<BackendFallbackInfo> fallback_info;

    Impl() = default;

    bool can_fallback_to_cpu() const {
        return cpu_fallback_device.has_value() && session != nullptr &&
               session->device().backend != Backend::CPU;
    }

    Result<void> activate_cpu_fallback(const std::string& reason) {
        if (!can_fallback_to_cpu()) {
            return {};
        }

        Backend failed_backend = session->device().backend;
        auto fallback_res = InferenceSession::create(model_path, *cpu_fallback_device);
        if (!fallback_res) {
            return Unexpected(fallback_res.error());
        }

        session = std::move(*fallback_res);
        fallback_info = BackendFallbackInfo{failed_backend, session->device().backend, reason};
        return {};
    }

    bool should_retry_on_cpu(const Error& error) const {
        return error.code == ErrorCode::InferenceFailed ||
               error.code == ErrorCode::HardwareNotSupported;
    }

    template <typename T, typename Operation>
    Result<T> run_with_cpu_fallback(Operation&& operation) {
        auto result = operation();
        if (result || !can_fallback_to_cpu() || !should_retry_on_cpu(result.error())) {
            return result;
        }

        auto fallback_res = activate_cpu_fallback(result.error().message);
        if (!fallback_res) {
            return result;
        }

        return operation();
    }
};

namespace {

std::optional<DeviceInfo> build_cpu_fallback_device(const DeviceInfo& device) {
#if defined(__APPLE__)
    if (device.backend == Backend::CoreML || device.backend == Backend::Auto) {
        return DeviceInfo{"Generic CPU", device.available_memory_mb, Backend::CPU};
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
                                               DeviceInfo device) {
    auto engine = std::unique_ptr<Engine>(new Engine());
    engine->m_impl->model_path = model_path;

    DeviceInfo requested_device = device.backend == Backend::Auto ? auto_detect() : device;
    engine->m_impl->cpu_fallback_device = build_cpu_fallback_device(requested_device);

    auto session_res = InferenceSession::create(model_path, requested_device);
    if (!session_res && engine->m_impl->cpu_fallback_device.has_value()) {
        auto fallback_res =
            InferenceSession::create(model_path, *engine->m_impl->cpu_fallback_device);
        if (!fallback_res) {
            return Unexpected(session_res.error());
        }
        engine->m_impl->session = std::move(*fallback_res);
        engine->m_impl->fallback_info = BackendFallbackInfo{requested_device.backend, Backend::CPU,
                                                            session_res.error().message};
    } else if (!session_res) {
        return Unexpected(session_res.error());
    } else {
        engine->m_impl->session = std::move(*session_res);
    }

    // Warm-up run (especially important for CoreML/TensorRT JIT compilation)
    int res = engine->recommended_resolution();
    ImageBuffer warm_rgb(res, res, 3);
    ImageBuffer warm_hint(res, res, 1);
    std::fill(warm_rgb.view().data.begin(), warm_rgb.view().data.end(), 0.0f);
    std::fill(warm_hint.view().data.begin(), warm_hint.view().data.end(), 0.0f);

    // Non-blocking warm-up
    engine->process_frame(warm_rgb.view(), warm_hint.view());

    return engine;
}

Result<FrameResult> Engine::process_frame(const Image& rgb, const Image& alpha_hint,
                                          const InferenceParams& params) {
    if (!m_impl->session) {
        return Unexpected(Error{ErrorCode::ModelLoadFailed, "Engine not initialized"});
    }

    return m_impl->run_with_cpu_fallback<FrameResult>(
        [&]() { return m_impl->session->run(rgb, alpha_hint, params); });
}

Result<std::vector<FrameResult>> Engine::process_frame_batch(const std::vector<Image>& rgbs,
                                                             const std::vector<Image>& alpha_hints,
                                                             const InferenceParams& params) {
    if (!m_impl->session) {
        return Unexpected(Error{ErrorCode::ModelLoadFailed, "Engine not initialized"});
    }

    return m_impl->run_with_cpu_fallback<std::vector<FrameResult>>(
        [&]() { return m_impl->session->run_batch(rgbs, alpha_hints, params); });
}

Result<void> Engine::process_sequence(const std::vector<std::filesystem::path>& inputs,
                                      const std::vector<std::filesystem::path>& alpha_hints,
                                      const std::filesystem::path& output_dir,
                                      const InferenceParams& params, ProgressCallback on_progress) {
    bool has_hints = !alpha_hints.empty();
    if (has_hints && inputs.size() != alpha_hints.size()) {
        return Unexpected(
            Error{ErrorCode::InvalidParameters, "Inputs and alpha hints size mismatch"});
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
            auto rgb_res = frame_io::read_frame(inputs[i + b]);
            if (!rgb_res) return Unexpected(rgb_res.error());

            ImageBuffer hint_buf;
            if (has_hints) {
                auto hint_res = frame_io::read_frame(alpha_hints[i + b]);
                if (!hint_res) return Unexpected(hint_res.error());
                hint_buf = std::move(*hint_res);
            } else {
                hint_buf = ImageBuffer(rgb_res->view().width, rgb_res->view().height, 1);
                ColorUtils::generate_rough_matte(rgb_res->view(), hint_buf.view());
            }

            rgb_views.push_back(rgb_res->view());
            hint_views.push_back(hint_buf.view());
            rgb_bufs.push_back(std::move(*rgb_res));
            hint_bufs.push_back(std::move(hint_buf));
        }

        auto results = m_impl->run_with_cpu_fallback<std::vector<FrameResult>>(
            [&]() { return m_impl->session->run_batch(rgb_views, hint_views, params); });
        if (!results) return Unexpected(results.error());

        for (size_t b = 0; b < current_batch_size; ++b) {
            std::string filename = inputs[i + b].filename().string();
            auto save_res = frame_io::save_result(output_dir, filename, (*results)[b]);
            if (!save_res) return save_res;
        }
    }

    return {};
}

Result<void> Engine::process_video(const std::filesystem::path& input_video,
                                   const std::filesystem::path& hint_video,
                                   const std::filesystem::path& output_video,
                                   const InferenceParams& params, ProgressCallback on_progress) {
    if (!m_impl->session) {
        return Unexpected(Error{ErrorCode::InferenceFailed, "Engine not initialized"});
    }

    auto reader_rgb_res = VideoReader::open(input_video);
    if (!reader_rgb_res) return Unexpected(reader_rgb_res.error());
    auto reader_rgb = std::move(*reader_rgb_res);

    bool has_hint_video = !hint_video.empty();
    std::unique_ptr<VideoReader> reader_hint;
    if (has_hint_video) {
        auto reader_hint_res = VideoReader::open(hint_video);
        if (!reader_hint_res) return Unexpected(reader_hint_res.error());
        reader_hint = std::move(*reader_hint_res);
    }

    int out_w = reader_rgb->width();
    int out_h = reader_rgb->height();

    auto writer_res = VideoWriter::open(output_video, out_w, out_h, reader_rgb->fps());
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
    };

    auto fetch_batch = [&](int size) -> Result<Batch> {
        Batch b;
        for (int i = 0; i < size; ++i) {
            auto rgb_res = reader_rgb->read_next_frame();
            if (!rgb_res || rgb_res->view().empty()) break;

            ImageBuffer hint_buf;
            if (has_hint_video) {
                auto hint_res = reader_hint->read_next_frame();
                if (!hint_res || hint_res->view().empty()) break;
                hint_buf = std::move(*hint_res);
            } else {
                hint_buf = ImageBuffer(rgb_res->view().width, rgb_res->view().height, 1);
                ColorUtils::generate_rough_matte(rgb_res->view(), hint_buf.view());
            }

            b.rgb_views.push_back(rgb_res->view());
            b.hint_views.push_back(hint_buf.view());
            b.rgb_bufs.push_back(std::move(*rgb_res));
            b.hint_bufs.push_back(std::move(hint_buf));
        }
        return b;
    };

    // Initial pre-fetch
    auto current_batch_future = std::async(std::launch::async, fetch_batch, batch_size);

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
        auto results = m_impl->run_with_cpu_fallback<std::vector<FrameResult>>([&]() {
            return m_impl->session->run_batch(current_batch.rgb_views, current_batch.hint_views,
                                              params);
        });
        if (!results) return Unexpected(results.error());

        // Sequential Write (FFmpeg encoding is usually CPU-bound but fast enough)
        for (auto& res : *results) {
            auto write_res = writer->write_frame(res.composite.view());
            if (!write_res) return Unexpected(write_res.error());
        }

        frame_idx += current_batch.rgb_views.size();
    }

    writer.reset();  // Final flush

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
