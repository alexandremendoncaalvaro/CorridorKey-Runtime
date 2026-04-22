#pragma once

#include <cstddef>

namespace corridorkey::core::mlx_memory {

// Snapshot of MLX's memory accounting at a point in time. All values are in
// bytes. Fields map directly to mlx::core::get_*_memory() / get_*_limit().
// On non-MLX builds every field is zero.
struct Snapshot {
    std::size_t active_bytes = 0;
    std::size_t cache_bytes = 0;
    std::size_t peak_bytes = 0;
    std::size_t memory_limit_bytes = 0;
    std::size_t cache_limit_bytes = 0;
    std::size_t wired_limit_bytes = 0;
    std::size_t max_recommended_working_set_bytes = 0;
};

// Policy selector for apply_policy(). Normal is the baseline applied by
// initialize_defaults(). Warn/Critical tighten the cache so MLX cooperates
// with the macOS VM compressor instead of fighting it.
enum class Policy {
    Normal,
    PressureWarn,
    PressureCritical,
};

// Apply the baseline MLX memory limits for this device. Safe to call
// repeatedly (std::once_flag underneath); only the first call installs the
// limits. Returns the snapshot captured after configuration.
//
// Why: MLX's defaults (memory_limit = 1.5x max recommended working set,
// cache_limit = memory_limit, wired_limit = 0) are tuned for 32 GB+ Apple
// Silicon machines running MLX in isolation. On 16 GB machines running
// alongside Resolve + a browser, the default cache grows into the VM
// compressor's working set and the compressor then steals GPU-queue
// bandwidth from MLX's next Metal submit (documented 50-108 s per-frame
// stalls, see mlx-lm #883 and our own Resolve log analysis).
Snapshot initialize_defaults();

// Ask MLX to release cached-but-unused allocations back to the system.
// Cheap; safe to call from a dispatch memory-pressure handler, from the
// session broker when it destroys a session, or between bridge changes.
void clear_cache();

// Re-read MLX's memory accounting without changing any limits. Intended for
// per-request telemetry in the runtime-server log.
Snapshot snapshot();

// Install an alternate policy and return the post-change snapshot. Normal
// restores the initialize_defaults() values; Warn tightens cache_limit to a
// small fraction of memory_limit and calls clear_cache(); Critical zeroes
// the cache entirely.
Snapshot apply_policy(Policy policy);

}  // namespace corridorkey::core::mlx_memory
