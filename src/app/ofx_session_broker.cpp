#include "ofx_session_broker.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <future>
#include <memory>
#include <string>
#include <thread>

#include "../common/parallel_for.hpp"
#include "../common/runtime_paths.hpp"
#include "../common/shared_memory_transport.hpp"
#include "../common/stage_profiler.hpp"
#include "../core/engine_internal.hpp"
#include "../core/mlx_memory_governor.hpp"
#include "../core/ort_process_context.hpp"
#include "ofx_session_policy.hpp"

// NOLINTBEGIN(modernize-use-designated-initializers,readability-function-size)
//
// ofx_session_broker.cpp tidy-suppression rationale.
//
// The broker stitches together OFX RPC envelopes, MLX memory telemetry,
// the engine-prewarm gate, and the shared-frame transport. Aggregate
// returns of OfxRuntime*Response use the project-wide positional style
// shared by the rest of src/app/. prepare_session() is intentionally
// long because it sequences admission, prewarm, and entry construction
// in one transaction; splitting it would scatter the rollback ordering
// across multiple TUs without reducing branch count.
namespace corridorkey::app {

namespace {

void refresh_engine_snapshot(OfxRuntimeSessionSnapshot& snapshot, const Engine& engine) {
    snapshot.effective_device = engine.current_device();
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

// Capture the current MLX / Metal memory state for admission and prewarm
// gating. On non-Apple builds the snapshot is empty (all zeros) and the
// policy helpers treat that as "telemetry unavailable -> allow."
detail::PrewarmMemorySnapshot capture_prewarm_snapshot() {
    detail::PrewarmMemorySnapshot out;
    const auto mlx_snap = corridorkey::core::mlx_memory::snapshot();
    out.recommended_working_set_bytes = mlx_snap.max_recommended_working_set_bytes;
    out.mlx_active_bytes = mlx_snap.active_bytes;
    out.mlx_cache_bytes = mlx_snap.cache_bytes;
    const auto policy = corridorkey::core::mlx_memory::current_policy();
    out.system_pressure_warn_or_critical =
        policy == corridorkey::core::mlx_memory::Policy::PressureWarn ||
        policy == corridorkey::core::mlx_memory::Policy::PressureCritical;
    return out;
}

// Outcome of prewarm_with_timeout(). Detached means the budget expired
// but the worker is still compiling in the background; the returned
// shared_future lets render_frame() wait for the compile to finish
// before touching the Engine (Engine is not thread-safe).
struct PrewarmRun {
    std::shared_future<void> ready = {};
    enum class Status {  // NOLINT(performance-enum-size)
        Completed,
        Detached,
        Skipped
    } status = Status::Skipped;
};

// Run engine->prewarm(shape) under a wall-clock budget. If the prewarm
// does not complete within `timeout`, we return without waiting and the
// worker keeps running in the background. The worker holds its own
// shared_ptr to the Engine so the MLX JIT can finish safely even if the
// broker later evicts the session entry.
PrewarmRun prewarm_with_timeout(const std::shared_ptr<Engine>& engine, int shape_px,
                                std::chrono::milliseconds timeout,
                                const StageTimingCallback& on_stage) {
    PrewarmRun out;
    if (!engine || shape_px <= 0) {
        out.status = PrewarmRun::Status::Skipped;
        return out;
    }
    // std::promise + std::shared_future gives us both the timed-wait
    // primitive we need and a rendezvous point the render path can park
    // on. Heap-allocating the promise keeps it alive if the worker
    // continues after we return (detach path).
    auto promise = std::make_shared<std::promise<void>>();
    out.ready = promise->get_future().share();
    std::thread worker([promise, engine, shape_px, on_stage]() mutable {
        try {
            (void)engine->prewarm(shape_px, on_stage);
        } catch (...) {  // NOLINT(bugprone-empty-catch)
            // Swallow; the caller's next render will surface any real
            // failure via the normal Result<> channel.
        }
        promise->set_value();
    });
    if (timeout <= std::chrono::milliseconds(0)) {
        worker.join();
        out.status = PrewarmRun::Status::Completed;
        return out;
    }
    if (out.ready.wait_for(timeout) == std::future_status::ready) {
        worker.join();
        out.status = PrewarmRun::Status::Completed;
        return out;
    }
    // Timed out: let the worker finish in the background. The captured
    // shared_ptr<Engine> keeps the Engine alive; the captured
    // shared_ptr<promise> outlives this stack frame too. render_frame
    // will park on `out.ready` before touching the Engine.
    worker.detach();
    out.status = PrewarmRun::Status::Detached;
    return out;
}

void copy_image_rows(Image source, Image destination) {
    if (source.empty() || destination.empty()) {
        return;
    }

    const size_t copy_size = std::min(source.data.size(), destination.data.size());
    if (copy_size == 0) {
        return;
    }

    if (source.width != destination.width || source.height != destination.height ||
        source.channels != destination.channels) {
        std::copy_n(source.data.begin(), copy_size, destination.data.begin());
        return;
    }

    const size_t row_size =
        static_cast<size_t>(source.width) * static_cast<size_t>(source.channels);
    common::parallel_for_rows(source.height, [&](int y_begin, int y_end) {
        for (int y_pos = y_begin; y_pos < y_end; ++y_pos) {
            const size_t offset = static_cast<size_t>(y_pos) * row_size;
            std::copy_n(source.data.begin() + static_cast<std::ptrdiff_t>(offset), row_size,
                        destination.data.begin() + static_cast<std::ptrdiff_t>(offset));
        }
    });
}

}  // namespace

OfxSessionBroker::OfxSessionBroker(OfxSessionBrokerOptions options)
    : m_options(options),
      m_ort_process_context(std::make_shared<corridorkey::core::OrtProcessContext>()) {}

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

    // Capture the current MLX / Metal memory state. This is the input to
    // both the admission check (does the requested shape even fit?) and
    // the prewarm gate (is it safe to pay the JIT compile up front?).
    // If the system is already under Warn / Critical pressure we first
    // ask MLX to release its cache -- cheap, safe, and buys us back
    // whatever the buffer pool was hoarding before we compute headroom.
    if (corridorkey::core::mlx_memory::current_policy() !=
        corridorkey::core::mlx_memory::Policy::Normal) {
        corridorkey::core::mlx_memory::clear_cache();
    }
    const auto memory_snapshot = capture_prewarm_snapshot();

    // Admission: the model_path is resolution-specific (the plugin-side
    // selection already picked the packaged artifact closest to the user's
    // Target Resolution). If that shape won't fit on this device right now,
    // we refuse rather than swap in a different resolution silently --
    // matting edge quality depends on the user's choice of bridge, and
    // quietly running at 768 when the user asked for 1024 is a contract
    // violation. The plugin surfaces InsufficientMemory as a user-visible
    // alert with next-step guidance.
    const int target_shape = request.effective_resolution > 0 ? request.effective_resolution
                                                              : request.requested_resolution;
    if (!detail::can_admit_session(memory_snapshot, target_shape)) {
        return Unexpected<Error>(
            broker_error(ErrorCode::InsufficientMemory,
                         "Not enough GPU memory available for the requested resolution. "
                         "Close other GPU-intensive apps or lower Target Resolution / Quality."));
    }

    std::vector<StageTiming> timings;
    StageTimingCallback on_stage = [&](const StageTiming& timing) {
        append_timing(timings, timing);
    };

    auto engine_result = corridorkey::core::EngineFactory::create_with_ort_process_context(
        request.model_path, request.requested_device, m_ort_process_context, on_stage,
        request.engine_options);
    if (!engine_result) {
        return Unexpected<Error>(engine_result.error());
    }

    SessionEntry entry;
    // Convert the factory's unique_ptr into a shared_ptr so the prewarm
    // worker can hold its own strong reference. The std::shared_ptr
    // constructor from std::unique_ptr&& adopts ownership cleanly.
    entry.engine = std::shared_ptr<Engine>(std::move(*engine_result));
    entry.last_used_at = now();
    entry.snapshot.session_id = key;
    entry.snapshot.model_path = request.model_path;
    entry.snapshot.artifact_name = detail::canonical_ofx_artifact_name(request.model_path);
    entry.snapshot.requested_device = request.requested_device;
    entry.snapshot.requested_quality_mode = request.requested_quality_mode;
    entry.snapshot.requested_resolution = request.requested_resolution;
    entry.snapshot.effective_resolution = request.effective_resolution;
    entry.snapshot.ref_count = 1;
    entry.snapshot.reused_existing_session = false;
    refresh_engine_snapshot(entry.snapshot, *entry.engine);

    // Prewarm gate: even when the shape fits at steady state, the JIT
    // compile peak can blow past the Metal ceiling. Skip (do not downshift)
    // when headroom is tight or the system is under pressure; the first
    // render_frame will pay the JIT the old way, but prepare_session
    // stays fast and the UI does not freeze.
    const auto decision = detail::evaluate_prewarm_decision(memory_snapshot, target_shape);
    if (decision == detail::PrewarmDecision::Prewarm ||
        decision == detail::PrewarmDecision::SkipTelemetryUnavailable) {
        // Honor the UI-selected prepare timeout so a misbehaving JIT (e.g.
        // a 2048-bridge 191 s compile under load) cannot wedge the RPC.
        // The worker keeps running in the background after timeout so the
        // user's next render still benefits from the warm kernels.
        const std::chrono::milliseconds timeout{std::max(0, request.prepare_timeout_ms)};
        auto prewarm_run = prewarm_with_timeout(entry.engine, target_shape, timeout, on_stage);
        entry.prewarm_ready = prewarm_run.ready;
    } else {
        // Emit a marker stage so the runtime log can correlate skipped
        // prewarm with steady-state first-frame latency.
        StageTiming marker;
        marker.name = std::string("prewarm_skipped_") + detail::prewarm_decision_label(decision);
        marker.total_ms = 0.0;
        marker.sample_count = 1;
        marker.work_units = 0;
        append_timing(timings, marker);
    }

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

    // Wait for any background prewarm to finish before touching the Engine.
    // When prepare_session detached the worker on timeout, the first
    // render_frame arrives while MLX is still JIT-compiling the shape.
    // Engine state is not thread-safe, so we park here until the worker
    // is done. The measure_stage() wrapper surfaces the wait in telemetry
    // so the source of the apparent first-frame latency is obvious.
    if (session->second.prewarm_ready.valid()) {
        common::measure_stage(
            on_stage, "prewarm_wait", [&]() { session->second.prewarm_ready.wait(); }, 1);
    }

    auto result = session->second.engine->process_frame(
        transport->rgb_view(), transport->hint_view(), request.params, on_stage);
    if (!result) {
        m_sessions.erase(session);
        return Unexpected<Error>(result.error());
    }

    auto alpha = transport->alpha_view();
    auto foreground = transport->foreground_view();
    auto result_alpha = result->alpha.const_view();
    common::measure_stage(
        on_stage, "ofx_broker_writeback",
        [&]() {
            copy_image_rows(result_alpha, alpha);
            if (!request.params.output_alpha_only) {
                auto result_foreground = result->foreground.const_view();
                copy_image_rows(result_foreground, foreground);
            }
        },
        1);

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
        std::to_string(static_cast<int>(request.engine_options.allow_cpu_fallback)) + "|" +
        std::to_string(static_cast<int>(request.engine_options.disable_cpu_ep_fallback))));
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
// NOLINTEND(modernize-use-designated-initializers,readability-function-size)
