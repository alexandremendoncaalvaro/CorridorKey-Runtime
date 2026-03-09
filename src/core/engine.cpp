#include <corridorkey/engine.hpp>
#include <corridorkey/frame_io.hpp>
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
    ProgressCallback on_progress
) {
    if (inputs.size() != alpha_hints.size()) {
        return unexpected(Error{ ErrorCode::InvalidParameters, "Inputs and alpha hints size mismatch" });
    }

    size_t total_frames = inputs.size();
    InferenceParams params;
    
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

int Engine::recommended_resolution() const {
    return m_impl->session ? m_impl->session->recommended_resolution() : 512;
}

DeviceInfo Engine::current_device() const {
    return m_impl->session ? m_impl->session->device() : DeviceInfo{ "Not Initialized", 0, Backend::Auto };
}

} // namespace corridorkey
