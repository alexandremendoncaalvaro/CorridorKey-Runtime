#pragma once

#include <cstddef>

namespace corridorkey::common {

// System-wide memory accounting sampled via host_statistics64() on macOS.
// On non-Apple hosts the fields report zero and callers treat that as
// "telemetry unavailable" rather than "zero pressure".
//
// free_bytes: pages the kernel considers immediately free. On a 16 GB Apple
//   Silicon machine, anything under ~500 MB is already pressure, under
//   ~100 MB is where we observe the catastrophic 50-108 s Metal-submit
//   stalls documented in docs/OPTIMIZATION_MEASUREMENTS.md.
// compressor_bytes: pages currently held by the VM compressor. Correlated
//   1:1 with Metal-submit stalls in our analysis: compressor > 6 GB is a
//   strong predictor of render regressions even when free_bytes looks
//   healthy, because the compressor competes with MLX for GPU-queue time.
struct HostMemoryStats {
    std::size_t free_bytes = 0;
    std::size_t compressor_bytes = 0;
};

// Snapshot system memory. Cheap (one mach_host_self() + host_statistics64()
// call); safe to invoke on the hot path but sampling once per
// PrepareSession / server-start event is the intended cadence.
HostMemoryStats query_host_memory_stats();

}  // namespace corridorkey::common
