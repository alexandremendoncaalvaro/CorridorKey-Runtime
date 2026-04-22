#pragma once

#include <cstddef>

namespace corridorkey::core::metal_memory {

// Report MTLDevice.recommendedMaxWorkingSetSize for the system-default device.
// Returns 0 when Metal is unavailable (non-Apple builds, no default device).
//
// The value is the Metal driver's dynamic estimate of how much memory can be
// resident without evicting system resources. It shrinks under memory pressure
// and is the canonical "working set ceiling" used by PyTorch MPS, llama.cpp's
// Metal backend, and MLX's own default memory_limit derivation. Our prewarm
// decision subtracts MLX's active + cache bytes from this value to reason about
// headroom before compiling a new JIT shape.
//
// Cheap to call (one MTLCreateSystemDefaultDevice() + one property read). Safe
// from any thread; the probe holds no state.
std::size_t recommended_max_working_set_bytes();

}  // namespace corridorkey::core::metal_memory
