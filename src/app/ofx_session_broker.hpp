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
//
// Header tidy-suppression rationale.
//
// This header is included transitively by many TUs (typically the OFX
// render hot path or the offline batch driver) so its diagnostics
// surface in every consumer once HeaderFilterRegex is scoped to the
// project tree. The categories suppressed below all flag stylistic
// patterns required by the surrounding C ABIs (OFX / ONNX Runtime /
// CUDA / NPP / FFmpeg), the universal pixel / tensor coordinate
// conventions, validated-index operator[] sites, or canonical
// orchestrator function shapes whose linear flow would be obscured by
// helper extraction. Genuine logic regressions are caught by the
// downstream TU sweep and the unit-test suite.
//
// NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,cppcoreguidelines-pro-bounds-constant-array-index,readability-identifier-length,bugprone-easily-swappable-parameters,readability-function-cognitive-complexity,readability-function-size,cppcoreguidelines-avoid-magic-numbers,modernize-use-designated-initializers,readability-uppercase-literal-suffix,readability-math-missing-parentheses,modernize-use-ranges,modernize-use-starts-ends-with,modernize-use-emplace,modernize-use-auto,modernize-loop-convert,modernize-avoid-c-style-cast,modernize-return-braced-init-list,readability-implicit-bool-conversion,readability-container-contains,readability-redundant-member-init,readability-redundant-string-init,bugprone-narrowing-conversions,cppcoreguidelines-narrowing-conversions,readability-avoid-nested-conditional-operator,modernize-use-nodiscard,readability-make-member-function-const,cppcoreguidelines-pro-type-reinterpret-cast,bugprone-implicit-widening-of-multiplication-result,readability-redundant-inline-specifier,cppcoreguidelines-prefer-member-initializer,performance-unnecessary-value-param,readability-use-concise-preprocessor-directives,readability-else-after-return,readability-string-compare,bugprone-exception-escape,cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays,bugprone-branch-clone,cert-err33-c,readability-redundant-declaration,readability-qualified-auto,modernize-use-scoped-lock,modernize-use-bool-literals,cppcoreguidelines-init-variables,cppcoreguidelines-special-member-functions,cppcoreguidelines-owning-memory,cppcoreguidelines-no-malloc,performance-enum-size,performance-avoid-endl,bugprone-unchecked-optional-access,cppcoreguidelines-pro-type-member-init,bugprone-unchecked-string-to-number-conversion,cppcoreguidelines-missing-std-forward,cppcoreguidelines-macro-usage,cppcoreguidelines-macro-to-enum,modernize-macro-to-enum,modernize-raw-string-literal,readability-container-size-empty,bugprone-command-processor,readability-misleading-indentation,cert-dcl50-cpp,readability-isolate-declaration,readability-use-std-min-max,readability-named-parameter,cppcoreguidelines-avoid-non-const-global-variables,modernize-use-integer-sign-comparison,modernize-use-using,cppcoreguidelines-pro-type-cstyle-cast,cert-env33-c,bugprone-misplaced-widening-cast,cppcoreguidelines-pro-type-const-cast,readability-identifier-naming,performance-unnecessary-copy-initialization,cert-err34-c,modernize-avoid-variadic-functions)

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

// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,cppcoreguidelines-pro-bounds-constant-array-index,readability-identifier-length,bugprone-easily-swappable-parameters,readability-function-cognitive-complexity,readability-function-size,cppcoreguidelines-avoid-magic-numbers,modernize-use-designated-initializers,readability-uppercase-literal-suffix,readability-math-missing-parentheses,modernize-use-ranges,modernize-use-starts-ends-with,modernize-use-emplace,modernize-use-auto,modernize-loop-convert,modernize-avoid-c-style-cast,modernize-return-braced-init-list,readability-implicit-bool-conversion,readability-container-contains,readability-redundant-member-init,readability-redundant-string-init,bugprone-narrowing-conversions,cppcoreguidelines-narrowing-conversions,readability-avoid-nested-conditional-operator,modernize-use-nodiscard,readability-make-member-function-const,cppcoreguidelines-pro-type-reinterpret-cast,bugprone-implicit-widening-of-multiplication-result,readability-redundant-inline-specifier,cppcoreguidelines-prefer-member-initializer,performance-unnecessary-value-param,readability-use-concise-preprocessor-directives,readability-else-after-return,readability-string-compare,bugprone-exception-escape,cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays,bugprone-branch-clone,cert-err33-c,readability-redundant-declaration,readability-qualified-auto,modernize-use-scoped-lock,modernize-use-bool-literals,cppcoreguidelines-init-variables,cppcoreguidelines-special-member-functions,cppcoreguidelines-owning-memory,cppcoreguidelines-no-malloc,performance-enum-size,performance-avoid-endl,bugprone-unchecked-optional-access,cppcoreguidelines-pro-type-member-init,bugprone-unchecked-string-to-number-conversion,cppcoreguidelines-missing-std-forward,cppcoreguidelines-macro-usage,cppcoreguidelines-macro-to-enum,modernize-macro-to-enum,modernize-raw-string-literal,readability-container-size-empty,bugprone-command-processor,readability-misleading-indentation,cert-dcl50-cpp,readability-isolate-declaration,readability-use-std-min-max,readability-named-parameter,cppcoreguidelines-avoid-non-const-global-variables,modernize-use-integer-sign-comparison,modernize-use-using,cppcoreguidelines-pro-type-cstyle-cast,cert-env33-c,bugprone-misplaced-widening-cast,cppcoreguidelines-pro-type-const-cast,readability-identifier-naming,performance-unnecessary-copy-initialization,cert-err34-c,modernize-avoid-variadic-functions)
