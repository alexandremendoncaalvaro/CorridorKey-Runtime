#pragma once

#include <chrono>
#include <corridorkey/types.hpp>
#include <filesystem>
#include <string>

namespace corridorkey::app::detail {

inline std::string canonical_ofx_artifact_name(const std::filesystem::path& model_path) {
    return model_path.filename().string();
}

inline bool should_destroy_zero_ref_session(Backend backend) {
    return backend == Backend::TensorRT;
}

struct StickyBridgeCeilingState {
    int ceiling_px = 0;
    std::chrono::steady_clock::time_point set_at = {};
};

// Hold a tightened bridge-resolution ceiling across successive prepare_session
// calls until a cooldown expires, so flickering host memory pressure does not
// bounce new sessions between bridges and pay a fresh MLX JIT compile for
// each. Tightenings (raw lower than the current sticky, or first ever) apply
// immediately; relaxations (raw higher or 0) wait out the cooldown. Returns
// the ceiling to use for this request; 0 means "no ceiling".
inline int resolve_sticky_bridge_ceiling(StickyBridgeCeilingState& state, int raw_ceiling_px,
                                         std::chrono::steady_clock::time_point now_point,
                                         std::chrono::milliseconds cooldown) {
    const bool tighten =
        raw_ceiling_px != 0 && (state.ceiling_px == 0 || raw_ceiling_px < state.ceiling_px);
    if (tighten) {
        state.ceiling_px = raw_ceiling_px;
        state.set_at = now_point;
        return raw_ceiling_px;
    }

    if (state.ceiling_px == 0) {
        return raw_ceiling_px;
    }

    if (raw_ceiling_px == state.ceiling_px) {
        // Pressure unchanged at the current sticky level; bump the stamp so
        // the cooldown window measures time since pressure last cleared.
        state.set_at = now_point;
        return state.ceiling_px;
    }

    if (now_point - state.set_at >= cooldown) {
        state.ceiling_px = raw_ceiling_px;
        state.set_at = now_point;
        return raw_ceiling_px;
    }

    return state.ceiling_px;
}

}  // namespace corridorkey::app::detail
