#include "ofx_session_broker.hpp"

#include <algorithm>
#include <filesystem>
#include <string_view>

#include "../common/host_memory.hpp"
#include "../common/parallel_for.hpp"
#include "../common/runtime_paths.hpp"
#include "../common/shared_memory_transport.hpp"
#include "../common/stage_profiler.hpp"
#include "../core/engine_internal.hpp"
#include "../core/mlx_memory_governor.hpp"
#include "../core/ort_process_context.hpp"
#include "ofx_session_policy.hpp"

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

// Derive a safe bridge-resolution ceiling from the current host memory state.
// The thresholds come from the v0.7.6-mac.1 log analysis: 50-108 s Metal-submit
// stalls correlate with compressor > 6 GB and free_bytes < 100 MB. Under those
// conditions, loading a large bridge (1536 or 2048) is almost guaranteed to
// push MLX's working set into the compressor's path and fall into the slow
// regime. The ceiling returned by this helper is advisory: the broker
// reports it back in the response snapshot so the plugin / UI can honor the
// downshift on the next render; it does not force the current engine to a
// smaller bridge because the model_path is already resolution-specific.
//
// 0 means "no ceiling" (either the telemetry is unavailable or the host has
// abundant headroom). Any positive value is a bridge ceiling in pixels.
int safe_bridge_ceiling_px(const common::HostMemoryStats& stats) {
    if (stats.free_bytes == 0 && stats.compressor_bytes == 0) {
        return 0;
    }
    constexpr std::size_t kMegabyte = 1024ULL * 1024ULL;
    const std::size_t free_mb = stats.free_bytes / kMegabyte;
    const std::size_t compressor_mb = stats.compressor_bytes / kMegabyte;

    // Critical: host is actively compressing and free is small. The machine
    // is already in the regime where a fresh 1.5-2 GB bridge allocation will
    // trigger compressor thrash; cap at 512 so any new session picks the
    // smallest viable bridge.
    if (free_mb < 256 || compressor_mb > 8192) {
        return 512;
    }
    // Warn: free is low or compressor is elevated. Cap at 768 -- measurably
    // better quality than 512 while still keeping the working set small.
    if (free_mb < 768 || compressor_mb > 4096) {
        return 768;
    }
    // Moderate: free is tight but the compressor is still idle. Cap at 1024,
    // which fits comfortably on a 16 GB machine with Resolve running.
    if (free_mb < 1536 || compressor_mb > 2048) {
        return 1024;
    }
    return 0;
}

const char* bridge_pressure_level(const common::HostMemoryStats& stats) {
    if (stats.free_bytes == 0 && stats.compressor_bytes == 0) {
        return "unknown";
    }
    constexpr std::size_t kMegabyte = 1024ULL * 1024ULL;
    const std::size_t free_mb = stats.free_bytes / kMegabyte;
    const std::size_t compressor_mb = stats.compressor_bytes / kMegabyte;
    if (free_mb < 256 || compressor_mb > 8192) {
        return "critical";
    }
    if (free_mb < 768 || compressor_mb > 4096) {
        return "warn";
    }
    if (free_mb < 1536 || compressor_mb > 2048) {
        return "moderate";
    }
    return "normal";
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

    // Sample host memory before loading the engine so we can (a) release any
    // stale MLX cache back to the kernel if the machine is already compressed,
    // and (b) downshift the effective bridge resolution to one that fits the
    // current headroom. Both decisions are advisory -- the model_path is
    // resolution-specific so we cannot retarget the engine here -- but the
    // downshift is recorded in effective_resolution and propagates to the
    // plugin's next render so it can pick a smaller target_resolution.
    const auto host_stats = common::query_host_memory_stats();
    const char* pressure_level = bridge_pressure_level(host_stats);
    const int safe_ceiling = safe_bridge_ceiling_px(host_stats);
    if (std::string_view(pressure_level) == "critical" ||
        std::string_view(pressure_level) == "warn") {
        // Dropping cached-but-unused MLX allocations here gives the upcoming
        // engine load room to allocate without tripping the compressor. No-op
        // on non-MLX builds.
        corridorkey::core::mlx_memory::clear_cache();
    }

    int effective_resolution = request.effective_resolution;
    if (safe_ceiling > 0 && effective_resolution > safe_ceiling) {
        effective_resolution = safe_ceiling;
    }

    std::vector<StageTiming> timings;
    StageTimingCallback on_stage = [&](const StageTiming& timing) {
        append_timing(timings, timing);
    };

    auto engine = corridorkey::core::EngineFactory::create_with_ort_process_context(
        request.model_path, request.requested_device, m_ort_process_context, on_stage,
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
    entry.snapshot.requested_quality_mode = request.requested_quality_mode;
    entry.snapshot.requested_resolution = request.requested_resolution;
    entry.snapshot.effective_resolution = effective_resolution;
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
