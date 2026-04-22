#pragma once

#include <chrono>
#include <corridorkey/engine.hpp>
#include <future>
#include <memory>
#include <string>
#include <unordered_map>

#include "../common/ofx_runtime_defaults.hpp"
#include "ofx_runtime_protocol.hpp"
#include "ofx_session_policy.hpp"

namespace corridorkey::core {
class OrtProcessContext;
}

namespace corridorkey::app {

struct OfxSessionBrokerOptions {
    std::size_t max_cached_sessions = 4;
    std::chrono::milliseconds idle_session_ttl = common::kDefaultOfxIdleTimeout;
};

class OfxSessionBroker {
   public:
    explicit OfxSessionBroker(OfxSessionBrokerOptions options = {});

    Result<OfxRuntimePrepareSessionResponse> prepare_session(
        const OfxRuntimePrepareSessionRequest& request);
    Result<OfxRuntimeRenderFrameResponse> render_frame(const OfxRuntimeRenderFrameRequest& request);
    Result<void> release_session(const OfxRuntimeReleaseSessionRequest& request);

    [[nodiscard]] std::size_t session_count() const;
    [[nodiscard]] std::size_t active_session_count() const;
    [[nodiscard]] std::size_t cleanup_idle_sessions();

   private:
    struct SessionEntry {
        OfxRuntimeSessionSnapshot snapshot = {};
        // shared_ptr (not unique_ptr) so a background prewarm worker can
        // hold its own strong reference. If the broker evicts this entry
        // while prewarm is still compiling, the Engine outlives the map
        // entry until the worker finishes, keeping the MLX JIT safe from
        // use-after-free. See prewarm_with_timeout() in the .cpp.
        std::shared_ptr<Engine> engine = nullptr;
        // Shared future gated by the prewarm worker's promise. render_frame
        // blocks on this before calling process_frame so a detached prewarm
        // cannot race with inference on the same Engine (Engine is not
        // thread-safe). valid() is false when no prewarm was scheduled,
        // in which case render_frame does not wait.
        std::shared_future<void> prewarm_ready = {};
        std::chrono::steady_clock::time_point last_used_at = {};
    };

    static std::string session_key(const OfxRuntimePrepareSessionRequest& request);
    static std::vector<StageTiming> collect_stage_timings(StageTimingCallback& callback);

    Result<void> evict_idle_sessions_if_needed();

    OfxSessionBrokerOptions m_options = {};
    std::unordered_map<std::string, SessionEntry> m_sessions = {};
    std::shared_ptr<corridorkey::core::OrtProcessContext> m_ort_process_context = nullptr;
};

}  // namespace corridorkey::app
