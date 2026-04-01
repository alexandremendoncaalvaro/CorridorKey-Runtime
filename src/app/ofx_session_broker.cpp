#include "ofx_session_broker.hpp"

#include <algorithm>
#include <filesystem>

#include "../common/runtime_paths.hpp"
#include "../common/shared_memory_transport.hpp"
#include "ofx_session_policy.hpp"

namespace corridorkey::app {

namespace {

void refresh_engine_snapshot(OfxRuntimeSessionSnapshot& snapshot, const Engine& engine) {
    snapshot.effective_device = engine.current_device();
    snapshot.effective_engine = engine.execution_engine();
    snapshot.backend_fallback = engine.backend_fallback();
    snapshot.recommended_resolution = engine.recommended_resolution();
}

OfxRuntimeSessionSnapshot response_snapshot(const OfxRuntimeSessionSnapshot& snapshot,
                                            bool reused_existing_session) {
    auto response = snapshot;
    response.reused_existing_session = reused_existing_session;
    return response;
}

std::chrono::steady_clock::time_point now() {
    return std::chrono::steady_clock::now();
}

Error broker_error(ErrorCode code, const std::string& message) {
    return Error{code, message};
}

void append_timing(std::vector<StageTiming>& timings, const StageTiming& timing) {
    timings.push_back(timing);
}

}  // namespace

OfxSessionBroker::OfxSessionBroker(OfxSessionBrokerOptions options) : m_options(options) {}

Result<OfxRuntimePrepareSessionResponse> OfxSessionBroker::prepare_session(
    const OfxRuntimePrepareSessionRequest& request) {
    (void)cleanup_idle_sessions();
    auto eviction_result = evict_idle_sessions_if_needed();
    if (!eviction_result) {
        return Unexpected<Error>(eviction_result.error());
    }

    const std::string key = session_key(request);
    if (auto existing = m_sessions.find(key); existing != m_sessions.end()) {
        refresh_engine_snapshot(existing->second.snapshot, *existing->second.engine);
        existing->second.snapshot.ref_count += 1;
        existing->second.last_used_at = now();
        return OfxRuntimePrepareSessionResponse{response_snapshot(existing->second.snapshot, true),
                                                {}};
    }

    std::vector<StageTiming> timings;
    StageTimingCallback on_stage = [&](const StageTiming& timing) {
        append_timing(timings, timing);
    };

    auto engine = Engine::create(request.model_path, request.requested_device, on_stage,
                                 request.engine_options);
    if (!engine) {
        return Unexpected<Error>(engine.error());
    }

    SessionEntry entry;
    entry.engine = std::move(*engine);
    entry.last_used_at = now();
    entry.snapshot.session_id = key;
    entry.snapshot.model_path = request.model_path;
    entry.snapshot.artifact_name = detail::canonical_ofx_artifact_name(request.model_path);
    entry.snapshot.requested_device = request.requested_device;
    entry.snapshot.requested_engine = request.engine_options.execution_engine;
    entry.snapshot.requested_quality_mode = request.requested_quality_mode;
    entry.snapshot.requested_resolution = request.requested_resolution;
    entry.snapshot.effective_resolution = request.effective_resolution;
    entry.snapshot.ref_count = 1;
    entry.snapshot.reused_existing_session = false;
    refresh_engine_snapshot(entry.snapshot, *entry.engine);

    auto response =
        OfxRuntimePrepareSessionResponse{response_snapshot(entry.snapshot, false), timings};
    m_sessions.emplace(key, std::move(entry));
    return response;
}

Result<OfxRuntimeRenderFrameResponse> OfxSessionBroker::render_frame(
    const OfxRuntimeRenderFrameRequest& request) {
    auto session = m_sessions.find(request.session_id);
    if (session == m_sessions.end()) {
        return Unexpected<Error>(
            broker_error(ErrorCode::InvalidParameters,
                         "Runtime session is not prepared: " + request.session_id));
    }

    auto transport = common::SharedFrameTransport::open(request.shared_frame_path);
    if (!transport) {
        return Unexpected<Error>(transport.error());
    }
    if (transport->width() != request.width || transport->height() != request.height) {
        return Unexpected<Error>(broker_error(ErrorCode::InvalidParameters,
                                              "Shared frame size does not match render request."));
    }

    std::vector<StageTiming> timings;
    StageTimingCallback on_stage = [&](const StageTiming& timing) {
        append_timing(timings, timing);
    };

    auto result = session->second.engine->process_frame(
        transport->rgb_view(), transport->hint_view(), request.params, on_stage);
    if (!result) {
        m_sessions.erase(session);
        return Unexpected<Error>(result.error());
    }

    auto alpha = transport->alpha_view();
    auto foreground = transport->foreground_view();
    auto result_alpha = result->alpha.const_view();
    auto result_foreground = result->foreground.const_view();
    std::copy(result_alpha.data.begin(), result_alpha.data.end(), alpha.data.begin());
    std::copy(result_foreground.data.begin(), result_foreground.data.end(),
              foreground.data.begin());

    refresh_engine_snapshot(session->second.snapshot, *session->second.engine);
    session->second.last_used_at = now();
    return OfxRuntimeRenderFrameResponse{response_snapshot(session->second.snapshot, false),
                                         timings};
}

Result<void> OfxSessionBroker::release_session(const OfxRuntimeReleaseSessionRequest& request) {
    auto session = m_sessions.find(request.session_id);
    if (session == m_sessions.end()) {
        return {};
    }

    if (session->second.snapshot.ref_count > 0) {
        session->second.snapshot.ref_count -= 1;
    }
    if (session->second.snapshot.ref_count == 0 &&
        detail::should_destroy_zero_ref_session(
            session->second.snapshot.effective_device.backend)) {
        m_sessions.erase(session);
        return {};
    }
    session->second.last_used_at = now();
    return {};
}

std::size_t OfxSessionBroker::session_count() const {
    return m_sessions.size();
}

std::size_t OfxSessionBroker::active_session_count() const {
    return static_cast<std::size_t>(
        std::count_if(m_sessions.begin(), m_sessions.end(),
                      [](const auto& pair) { return pair.second.snapshot.ref_count > 0; }));
}

std::size_t OfxSessionBroker::cleanup_idle_sessions() {
    const auto threshold = now() - m_options.idle_session_ttl;
    std::size_t removed_sessions = 0;
    for (auto it = m_sessions.begin(); it != m_sessions.end();) {
        if (it->second.snapshot.ref_count == 0 && it->second.last_used_at < threshold) {
            it = m_sessions.erase(it);
            removed_sessions += 1;
            continue;
        }
        ++it;
    }
    return removed_sessions;
}

std::string OfxSessionBroker::session_key(const OfxRuntimePrepareSessionRequest& request) {
    std::error_code error;
    auto canonical_model_path = std::filesystem::weakly_canonical(request.model_path, error);
    if (error) {
        canonical_model_path = request.model_path;
    }
    return std::to_string(common::detail::fnv1a_64(
        canonical_model_path.string() + "|" +
        std::to_string(static_cast<int>(request.requested_device.backend)) + "|" +
        std::to_string(static_cast<int>(request.engine_options.execution_engine)) + "|" +
        std::to_string(request.engine_options.allow_cpu_fallback) + "|" +
        std::to_string(request.engine_options.disable_cpu_ep_fallback)));
}

std::vector<StageTiming> OfxSessionBroker::collect_stage_timings(StageTimingCallback& callback) {
    (void)callback;
    return {};
}

Result<void> OfxSessionBroker::evict_idle_sessions_if_needed() {
    if (m_sessions.size() < m_options.max_cached_sessions) {
        return {};
    }

    auto eviction_candidate = m_sessions.end();
    for (auto it = m_sessions.begin(); it != m_sessions.end(); ++it) {
        if (it->second.snapshot.ref_count != 0) {
            continue;
        }
        if (eviction_candidate == m_sessions.end() ||
            it->second.last_used_at < eviction_candidate->second.last_used_at) {
            eviction_candidate = it;
        }
    }

    if (eviction_candidate == m_sessions.end()) {
        return Unexpected<Error>(
            broker_error(ErrorCode::HardwareNotSupported,
                         "All runtime sessions are active; refusing to evict a live OFX session."));
    }

    m_sessions.erase(eviction_candidate);
    return {};
}

}  // namespace corridorkey::app
