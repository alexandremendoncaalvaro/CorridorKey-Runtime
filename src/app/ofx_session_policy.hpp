#pragma once

#include <corridorkey/types.hpp>
#include <cstddef>
#include <filesystem>
#include <string>

namespace corridorkey::app::detail {

inline std::string canonical_ofx_artifact_name(const std::filesystem::path& model_path) {
    return model_path.filename().string();
}

inline bool should_destroy_zero_ref_session(Backend backend) {
    return backend == Backend::TensorRT;
}

// --------------------------------------------------------------------------
// Prewarm admission policy.
//
// Principle: the broker does NOT silently downshift the caller's requested
// resolution. Running at 768 after the user asked for 1024 degrades matting
// edge quality in a way the user cannot see in the UI, so our contract is
// "we honor your choice or we refuse, never quietly compromise."
//
// Instead of a downshift we do two things:
//   1. Admission: if the current MLX resident working-set estimate for the
//      requested shape exceeds what Metal says the GPU can comfortably hold
//      (MTLDevice.recommendedMaxWorkingSetSize minus what MLX is already
//      using), reject with ErrorCode::InsufficientMemory. The OFX plugin
//      surfaces this to the user, who can change Quality / Target Resolution
//      explicitly, or close other GPU-heavy apps.
//   2. Prewarm gating: even when the shape fits for steady-state inference,
//      the JIT compile peak is roughly 3x the resident size. If that peak
//      would blow past the Metal ceiling, or the system is already in
//      dispatch memory-pressure Warn/Critical, we skip the prewarm step
//      rather than lock the UI for tens of seconds. The first user render
//      then pays the JIT the old way, but the UI stays responsive and the
//      prepare_session RPC returns promptly.
// --------------------------------------------------------------------------

// Point-in-time memory state consulted for admission and prewarm decisions.
// All fields are bytes; 0 means "not available on this platform / build."
struct PrewarmMemorySnapshot {
    // MTLDevice.recommendedMaxWorkingSetSize. This is the same dynamic
    // ceiling PyTorch MPS and llama.cpp consult; it shrinks under system
    // pressure, so it implicitly reflects what other apps are using.
    std::size_t recommended_working_set_bytes = 0;
    // mlx::core::get_active_memory(): bytes currently allocated and in use
    // by MLX on the Metal device.
    std::size_t mlx_active_bytes = 0;
    // mlx::core::get_cache_memory(): bytes MLX is holding as a buffer
    // cache. Subtracted from the working-set ceiling with a discount since
    // clear_cache() can free most of it on demand.
    std::size_t mlx_cache_bytes = 0;
    // True when the dispatch memory-pressure source is currently signalling
    // Warn or Critical. Used as a veto on JIT compile.
    bool system_pressure_warn_or_critical = false;
};

// Conservative estimate of the MLX resident working set for a bridge at
// `shape_px`, in bytes. The constant is fit to v0.7.6-mac.3 telemetry:
// at 1024 bridge we observed ~296 MB active + cache growing toward 2.5 GB,
// so the formula 320 * shape_px^2 (~320 B/pixel^2) gives us:
//   512   -> ~84 MB
//   768   -> ~188 MB
//   1024  -> ~335 MB   (measured ~296 MB active; ~12% headroom)
//   1536  -> ~754 MB
//   2048  -> ~1342 MB
// It is deliberately on the pessimistic side so admission refuses before
// the kernel's VM compressor starts swapping.
inline std::size_t estimate_mlx_resident_bytes(int shape_px) {
    if (shape_px <= 0) {
        return 0;
    }
    const std::size_t s = static_cast<std::size_t>(shape_px);
    return 320ULL * s * s;
}

// Peak memory during JIT compile is substantially larger than steady-state
// resident -- MLX keeps intermediate graph buffers alive while it traces
// and compiles. 3x matches the 2048-bridge crash in v0.7.6-mac.3 where
// prewarm hit ~4 GB peak against a ~1.3 GB resident target. When this peak
// would blow past the Metal ceiling we skip prewarm (but still admit the
// session, since the smaller steady-state allocation is survivable).
inline std::size_t estimate_mlx_peak_compile_bytes(int shape_px) {
    return estimate_mlx_resident_bytes(shape_px) * 3ULL;
}

// Safety margin on top of the raw resident estimate for admission. The
// factor leaves room for (a) per-session overhead not captured in the
// 320 * shape^2 fit, (b) MLX cache growth after warmup, (c) Resolve
// allocating additional GPU buffers while we render. 1.5x is conservative
// but keeps 1024 bridge admissible on a 16 GB machine with Resolve active
// (1024 -> ~500 MB required; Metal typically reports 10-11 GB recommended
// working set on 16 GB M-series once other apps are running).
inline constexpr double kPrewarmSafetyMargin = 1.5;

// Does the estimated resident working set for `shape_px` fit within the
// headroom implied by `snapshot`? Cache is discounted at 50% because a
// pre-admission clear_cache() typically releases more than half of it.
// Returns true when the snapshot is unavailable (recommended == 0) so
// that non-Apple builds and test environments do not spuriously reject.
inline bool can_admit_session(const PrewarmMemorySnapshot& snapshot, int shape_px) {
    if (snapshot.recommended_working_set_bytes == 0) {
        return true;
    }
    const std::size_t required = static_cast<std::size_t>(
        static_cast<double>(estimate_mlx_resident_bytes(shape_px)) * kPrewarmSafetyMargin);
    const std::size_t reclaimable_cache = snapshot.mlx_cache_bytes / 2;
    const std::size_t in_use =
        snapshot.mlx_active_bytes + (snapshot.mlx_cache_bytes > reclaimable_cache
                                         ? snapshot.mlx_cache_bytes - reclaimable_cache
                                         : 0);
    const std::size_t ceiling = snapshot.recommended_working_set_bytes;
    if (in_use >= ceiling) {
        return false;
    }
    return required <= ceiling - in_use;
}

// Outcome of the prewarm gate inside prepare_session. Prewarm is preferred
// (first render is fast) but we skip rather than lock the UI when the
// current system state says the JIT compile peak is risky.
enum class PrewarmDecision {
    Prewarm,                   // headroom is adequate; pay JIT up front.
    SkipInsufficientHeadroom,  // JIT peak would exceed Metal ceiling.
    SkipSystemPressure,        // dispatch source reporting Warn/Critical.
    SkipTelemetryUnavailable,  // non-Apple build; caller decides.
};

inline const char* prewarm_decision_label(PrewarmDecision decision) {
    switch (decision) {
        case PrewarmDecision::Prewarm:
            return "prewarm";
        case PrewarmDecision::SkipInsufficientHeadroom:
            return "skip_insufficient_headroom";
        case PrewarmDecision::SkipSystemPressure:
            return "skip_system_pressure";
        case PrewarmDecision::SkipTelemetryUnavailable:
            return "skip_telemetry_unavailable";
    }
    return "unknown";
}

// Decide whether to prewarm a session at `shape_px` given the current
// memory snapshot. Assumes admission has already succeeded for `shape_px`.
inline PrewarmDecision evaluate_prewarm_decision(const PrewarmMemorySnapshot& snapshot,
                                                 int shape_px) {
    if (snapshot.recommended_working_set_bytes == 0) {
        return PrewarmDecision::SkipTelemetryUnavailable;
    }
    if (snapshot.system_pressure_warn_or_critical) {
        return PrewarmDecision::SkipSystemPressure;
    }
    const std::size_t peak = estimate_mlx_peak_compile_bytes(shape_px);
    const std::size_t reclaimable_cache = snapshot.mlx_cache_bytes / 2;
    const std::size_t in_use =
        snapshot.mlx_active_bytes + (snapshot.mlx_cache_bytes > reclaimable_cache
                                         ? snapshot.mlx_cache_bytes - reclaimable_cache
                                         : 0);
    const std::size_t ceiling = snapshot.recommended_working_set_bytes;
    if (in_use >= ceiling || peak > ceiling - in_use) {
        return PrewarmDecision::SkipInsufficientHeadroom;
    }
    return PrewarmDecision::Prewarm;
}

}  // namespace corridorkey::app::detail
