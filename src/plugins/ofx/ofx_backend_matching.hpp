#pragma once

#include <corridorkey/engine.hpp>

namespace corridorkey::ofx {

// Decides whether the backend that the runtime actually used satisfies the
// backend the caller asked for. Used by the quality-switch candidate loop in
// ensure_engine_for_quality to short-circuit after the first compatible
// candidate prepares successfully.
//
// Backend::Auto is treated as "the caller has not committed to a specific
// backend" and therefore matches any effective backend the runtime returns.
// This wildcard semantic is required by the Path B out-of-process refactor
// (commit 1a17033), which populates the .ofx-side DeviceInfo with
// Backend::Auto until the runtime server reports the real backend on the
// first prepare_session response. Without the wildcard, every server
// response is treated as a backend mismatch, the candidate loop iterates
// past the first fp16 success, and the runtime server eventually crashes
// while preparing an incompatible fallback artifact.
//
// Backend::CPU is treated as "I asked for CPU and got CPU" regardless of
// device-name equality, because some backends bind CPU through different
// DeviceInfo fields.
inline bool backend_matches_request(const DeviceInfo& effective_device,
                                    const DeviceInfo& requested_device) {
    if (requested_device.backend == Backend::CPU) {
        return true;
    }
    if (requested_device.backend == Backend::Auto) {
        return true;
    }
    return effective_device.backend == requested_device.backend;
}

}  // namespace corridorkey::ofx
