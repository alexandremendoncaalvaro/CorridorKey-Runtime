#include <corridorkey/engine.hpp>
#include <corridorkey/frame_io.hpp>
#include "../frame_io/video_io.hpp"
#include "inference_session.hpp"
#include <future>
#include <deque>

namespace corridorkey {

class Engine::Impl {
public:
    std::unique_ptr<InferenceSession> session;

    Impl() = default;
};

Engine::Engine() : m_impl(std::make_unique<Impl>()) {}

Engine::~Engine() = default;

Engine::Engine(Engine&&) noexcept = default;
Engine& Engine::operator=(Engine&&) noexcept = default;

Result<std::unique_ptr<Engine>> Engine::create(
    const std::filesystem::path& model_path,
    DeviceInfo device
) {
    auto session_res = InferenceSession::create(model_path, device);
    if (!session_res) {
        return unexpected(session_res.error());
    }

    auto engine = std::unique_ptr<Engine>(new Engine());
    engine->m_impl->session = std::move(*session_res);

    return engine;
}

Result<FrameResult> Engine::process_frame(
    const Image& rgb, 
    const Image& alpha_hint,
    const InferenceParams& params
) {
    if (!m_impl->session) {
        return unexpected(Error{ ErrorCode::ModelLoadFailed, "Engine not initialized" });
    }

    return m_impl->session->run(rgb, alpha_hint, params);
}

Result<void> Engine::process_sequence(
    const std::vector<std::filesystem::path>& inputs,
    const std::vector<std::filesystem::path>& alpha_hints,
    const std::filesystem::path& output_dir,
    const InferenceParams& params,
    ProgressCallback on_progress
) {
    if (inputs.size() != alpha_hints.size()) {
        return unexpected(Error{ ErrorCode::InvalidParameters, "Inputs and alpha hints size mismatch" });
    }

    size_t total_frames = inputs.size();

    
    // Asynchronous Pipeline Settings
    const size_t max_in_flight = 3; // Keep 3 frames in the pipeline
    std::deque<std::future<Result<FrameResult>>> pipeline;

    for (size_t i = 0; i < total_frames; ++i) {
        // 1. Check for cancellation
        if (on_progress && !on_progress(static_cast<float>(i) / total_frames, "Processing frame " + std::to_string(i))) {
            return unexpected(Error{ ErrorCode::Cancelled, "Processing cancelled by user" });
        }

        // 2. Manage Pipeline (Limit concurrent frames to avoid RAM bloat)
        if (pipeline.size() >= max_in_flight) {
            auto res = pipeline.front().get();
            pipeline.pop_front();
            if (!res) return unexpected(res.error());
            
            // Save results of the finished frame (Writing)
            // The index of the finished frame is (i - max_in_flight)
            std::string filename = inputs[i - max_in_flight].filename().string();
            auto save_res = frame_io::save_result(output_dir, filename, *res);
            if (!save_res) return save_res;
        }

        // 3. Dispatch next frame (Async Reading + Inference)
        pipeline.push_back(std::async(std::launch::async, [&, i]() -> Result<FrameResult> {
            auto rgb_res = frame_io::read_frame(inputs[i]);
            if (!rgb_res) return unexpected(rgb_res.error());

            auto hint_res = frame_io::read_frame(alpha_hints[i]);
            if (!hint_res) return unexpected(hint_res.error());

            return m_impl->session->run(rgb_res->view(), hint_res->view(), params);
        }));
    }

    // 4. Drain the remaining pipeline
    size_t finished_idx = total_frames - pipeline.size();
    while (!pipeline.empty()) {
        auto res = pipeline.front().get();
        pipeline.pop_front();
        if (!res) return unexpected(res.error());

        std::string filename = inputs[finished_idx++].filename().string();
        auto save_res = frame_io::save_result(output_dir, filename, *res);
        if (!save_res) return save_res;
    }

    return {};
}

Result<void> Engine::process_video(
    const std::filesystem::path& input_video,
    const std::filesystem::path& hint_video,
    const std::filesystem::path& output_video,
    const InferenceParams& params,
    ProgressCallback on_progress
) {
    if (!m_impl->session) {
        return unexpected(Error{ ErrorCode::InferenceFailed, "Engine not initialized" });
    }

    auto reader_rgb_res = VideoReader::open(input_video);
    if (!reader_rgb_res) return unexpected(reader_rgb_res.error());
    auto reader_rgb = std::move(*reader_rgb_res);

    auto reader_hint_res = VideoReader::open(hint_video);
    if (!reader_hint_res) return unexpected(reader_hint_res.error());
    auto reader_hint = std::move(*reader_hint_res);

    // Default to processing target resolution or original resolution
    int out_w = reader_rgb->width();
    int out_h = reader_rgb->height();

    auto writer_res = VideoWriter::open(output_video, out_w, out_h, reader_rgb->fps());
    if (!writer_res) return unexpected(writer_res.error());
    auto writer = std::move(*writer_res);

    int64_t total_frames = reader_rgb->total_frames();
    int64_t frame_idx = 0;

    const size_t max_in_flight = 3;
    std::deque<std::future<Result<FrameResult>>> pipeline;

    while (true) {
        // Read next frame asynchronously
        auto rgb_res = reader_rgb->read_next_frame();
        if (!rgb_res) return unexpected(rgb_res.error());
        if (rgb_res->view().empty()) break; // EOF

        auto hint_res = reader_hint->read_next_frame();
        if (!hint_res) return unexpected(hint_res.error());
        if (hint_res->view().empty()) break; // EOF

        if (on_progress) {
            float p = total_frames > 0 ? static_cast<float>(frame_idx) / total_frames : 0.0f;
            if (!on_progress(p, "Processing frame " + std::to_string(frame_idx))) {
                return unexpected(Error{ ErrorCode::Cancelled, "Processing cancelled by user" });
            }
        }

        // We must move the ImageBuffers into the async lambda to keep them alive
        pipeline.push_back(std::async(std::launch::async, 
            [this, rgb_buf = std::move(*rgb_res), hint_buf = std::move(*hint_res), params]() mutable -> Result<FrameResult> {
                return m_impl->session->run(rgb_buf.view(), hint_buf.view(), params);
            }
        ));

        if (pipeline.size() >= max_in_flight) {
            auto res = pipeline.front().get();
            pipeline.pop_front();
            if (!res) return unexpected(res.error());

            auto write_res = writer->write_frame(res->composite.view());
            if (!write_res) return unexpected(write_res.error());
        }

        frame_idx++;
    }

    // Drain pipeline
    while (!pipeline.empty()) {
        auto res = pipeline.front().get();
        pipeline.pop_front();
        if (!res) return unexpected(res.error());

        auto write_res = writer->write_frame(res->composite.view());
        if (!write_res) return unexpected(write_res.error());
    }

    if (on_progress) {
        on_progress(1.0f, "Done");
    }

    return {};
}

int Engine::recommended_resolution() const {
    return m_impl->session ? m_impl->session->recommended_resolution() : 512;
}

DeviceInfo Engine::current_device() const {
    return m_impl->session ? m_impl->session->device() : DeviceInfo{ "Not Initialized", 0, Backend::Auto };
}

} // namespace corridorkey
