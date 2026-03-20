#pragma once

#include <chrono>
#include <corridorkey/engine.hpp>
#include <memory>
#include <string>
#include <unordered_map>

#include "ofx_runtime_protocol.hpp"

namespace corridorkey::app {

struct OfxSessionBrokerOptions {
    std::size_t max_cached_sessions = 4;
    std::chrono::milliseconds idle_session_ttl = std::chrono::minutes(2);
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
    void cleanup_idle_sessions();

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
};

}  // namespace corridorkey::app
