#include <corridorkey/engine.hpp>
#include "inference_session.hpp"

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
        return std::unexpected(session_res.error());
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
        return std::unexpected(Error{ ErrorCode::ModelLoadFailed, "Engine not initialized" });
    }

    return m_impl->session->run(rgb, alpha_hint, params);
}

Result<void> Engine::process_sequence(
    const std::vector<std::filesystem::path>& inputs,
    const std::vector<std::filesystem::path>& alpha_hints,
    const std::filesystem::path& output_dir,
    ProgressCallback on_progress
) {
    (void)output_dir;
    (void)on_progress;

    if (inputs.size() != alpha_hints.size()) {
        return std::unexpected(Error{ ErrorCode::InvalidParameters, "Inputs and alpha hints size mismatch" });
    }

    // TODO: Implement sequence processing with FrameIO
    return {};
}

int Engine::recommended_resolution() const {
    return m_impl->session ? m_impl->session->recommended_resolution() : 512;
}

DeviceInfo Engine::current_device() const {
    return m_impl->session ? m_impl->session->device() : DeviceInfo{ "Not Initialized", 0, Backend::Auto };
}

} // namespace corridorkey
