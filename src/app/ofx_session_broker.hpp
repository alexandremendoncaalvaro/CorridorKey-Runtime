#pragma once

#include <chrono>
#include <corridorkey/engine.hpp>
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
    // After a memory-pressure-driven bridge ceiling is applied, hold that
    // ceiling for this long before letting a subsequent relaxation (lower
    // pressure) take effect. Prevents oscillation between bridges where each
    // flicker would trigger a fresh MLX JIT compile. See
    // ofx_session_policy.hpp :: resolve_sticky_bridge_ceiling.
    std::chrono::milliseconds bridge_ceiling_cooldown = std::chrono::seconds(10);
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
        std::unique_ptr<Engine> engine = nullptr;
        std::chrono::steady_clock::time_point last_used_at = {};
    };

    static std::string session_key(const OfxRuntimePrepareSessionRequest& request);
    static std::vector<StageTiming> collect_stage_timings(StageTimingCallback& callback);

    Result<void> evict_idle_sessions_if_needed();

    OfxSessionBrokerOptions m_options = {};
    std::unordered_map<std::string, SessionEntry> m_sessions = {};
    std::shared_ptr<corridorkey::core::OrtProcessContext> m_ort_process_context = nullptr;
    detail::StickyBridgeCeilingState m_sticky_bridge_ceiling = {};
};

}  // namespace corridorkey::app
