#include "mlx_memory_governor.hpp"

#include <atomic>
#include <mutex>

#ifdef __APPLE__
#include <sys/sysctl.h>
#include <sys/types.h>

#include "metal_memory_probe.hpp"
#endif

#if CORRIDORKEY_WITH_MLX
#include <mlx/memory.h>
#endif

namespace corridorkey::core::mlx_memory {

namespace {

#ifdef __APPLE__
std::size_t query_host_memsize_bytes() {
    std::size_t value = 0;
    std::size_t size = sizeof(value);
    // hw.memsize is the canonical 64-bit total-RAM sysctl on Darwin; hw.memsize_usable
    // exists but is private SPI. Failure falls back to 0 and the caller picks a
    // conservative default that does not rely on this number.
    if (sysctlbyname("hw.memsize", &value, &size, nullptr, 0) != 0) {
        return 0;
    }
    return value;
}
#else
std::size_t query_host_memsize_bytes() {
    return 0;
}
#endif

#if CORRIDORKEY_WITH_MLX
struct BaselineLimits {
    std::size_t memory_limit_bytes = 0;
    std::size_t cache_limit_bytes = 0;
};

BaselineLimits derive_baseline_limits() {
    const std::size_t memsize = query_host_memsize_bytes();

    // Policy: memory_limit is the upper ceiling MLX refuses to cross. MLX's own
    // default is 1.5x the Metal-recommended working set, which on a 16 GB M4 is
    // effectively unbounded. Cap at 75% of total RAM (12 GB on 16 GB) so we
    // cannot starve Resolve and trigger the VM compressor's cascade.
    const std::size_t memory_limit =
        memsize > 0 ? static_cast<std::size_t>(static_cast<double>(memsize) * 0.75) : 0;

    // Policy: cache_limit is the soft ceiling for MLX's internal buffer cache.
    // MLX's default equals memory_limit, which on 16 GB lets MLX retain ~8 GB of
    // unused allocations. Cap at 15% of total RAM (~2.4 GB on 16 GB) so the
    // cache cannot crowd out Resolve's working set and trigger compressor
    // thrash on the next Metal submit.
    const std::size_t cache_limit =
        memsize > 0 ? static_cast<std::size_t>(static_cast<double>(memsize) * 0.15) : 0;

    return BaselineLimits{memory_limit, cache_limit};
}
#endif

// std::once_flag and the MLX baseline-limits cache need to be at namespace
// scope so initialize_defaults() can rerun against the same flag and the
// session broker can read the current policy on the hot prepare path
// without taking a mutex. Marking either as const would defeat
// std::call_once / std::atomic.
// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables)
std::once_flag g_initialize_once;
#if CORRIDORKEY_WITH_MLX
BaselineLimits g_baseline_limits;
#endif

// Last Policy installed by apply_policy(). Read by current_policy() and by
// the session broker when deciding whether to prewarm a new shape. std::atomic
// rather than a mutex because the read is on the hot prepare path and the
// write fires from a dedicated serial dispatch queue.
std::atomic<Policy> g_current_policy{Policy::Normal};
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)

Snapshot current_snapshot() {
    Snapshot out;
#if CORRIDORKEY_WITH_MLX
    out.active_bytes = mlx::core::get_active_memory();
    out.cache_bytes = mlx::core::get_cache_memory();
    out.peak_bytes = mlx::core::get_peak_memory();
    out.memory_limit_bytes = mlx::core::get_memory_limit();
    // set_cache_limit / set_wired_limit return the previous limit when given
    // the same value. There is no getter, so we cache the last applied values
    // on the module and read them back from the baseline / most-recent policy.
#endif
#ifdef __APPLE__
    // Metal driver's dynamic working-set ceiling. This is the same number
    // PyTorch MPS and llama.cpp consult; it shrinks automatically when the
    // system is under pressure. libmlx does not export the device_info()
    // symbol that would surface it, so we read it directly via a Metal probe.
    out.max_recommended_working_set_bytes = metal_memory::recommended_max_working_set_bytes();
#endif
    return out;
}

}  // namespace

Snapshot initialize_defaults() {
#if CORRIDORKEY_WITH_MLX
    std::call_once(g_initialize_once, []() {
        try {
            g_baseline_limits = derive_baseline_limits();
            // Order matters: wired first (cheapest, protects against kernel-panic
            // class mlx-lm #883), then memory limit (upper ceiling), then cache
            // limit. reset_peak_memory() so a fresh snapshot reflects this run.
            mlx::core::set_wired_limit(0);
            if (g_baseline_limits.memory_limit_bytes > 0) {
                mlx::core::set_memory_limit(g_baseline_limits.memory_limit_bytes);
            }
            if (g_baseline_limits.cache_limit_bytes > 0) {
                mlx::core::set_cache_limit(g_baseline_limits.cache_limit_bytes);
            }
            mlx::core::reset_peak_memory();
        } catch (...) {
            // If MLX rejects one of the limits (e.g. wired larger than kernel
            // budget, though we pass 0) we leave defaults in place rather than
            // crashing the server. The snapshot still reports MLX's own state.
        }
    });
    Snapshot snap = current_snapshot();
    snap.cache_limit_bytes = g_baseline_limits.cache_limit_bytes;
    snap.wired_limit_bytes = 0;
    return snap;
#else
    return Snapshot{};
#endif
}

void clear_cache() {
#if CORRIDORKEY_WITH_MLX
    mlx::core::clear_cache();
#endif
}

Snapshot snapshot() {
#if CORRIDORKEY_WITH_MLX
    Snapshot snap = current_snapshot();
    snap.cache_limit_bytes = g_baseline_limits.cache_limit_bytes;
    snap.wired_limit_bytes = 0;
    return snap;
#else
    return Snapshot{};
#endif
}

Snapshot apply_policy(Policy policy) {
#if CORRIDORKEY_WITH_MLX
    initialize_defaults();
    const std::size_t baseline_cache = g_baseline_limits.cache_limit_bytes;
    std::size_t cache_target = baseline_cache;
    switch (policy) {
        case Policy::Normal:
            cache_target = baseline_cache;
            break;
        case Policy::PressureWarn:
            cache_target = baseline_cache / 4;
            break;
        case Policy::PressureCritical:
            cache_target = 0;
            break;
    }
    mlx::core::set_cache_limit(cache_target);
    if (policy != Policy::Normal) {
        mlx::core::clear_cache();
    }
    g_current_policy.store(policy, std::memory_order_release);
    Snapshot snap = current_snapshot();
    snap.cache_limit_bytes = cache_target;
    snap.wired_limit_bytes = 0;
    return snap;
#else
    g_current_policy.store(policy, std::memory_order_release);
    return Snapshot{};
#endif
}

Policy current_policy() {
    return g_current_policy.load(std::memory_order_acquire);
}

}  // namespace corridorkey::core::mlx_memory
