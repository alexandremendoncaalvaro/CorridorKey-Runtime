#pragma once

#include <corridorkey/engine.hpp>

namespace corridorkey::ofx {

// Decides whether the backend that the runtime actually used satisfies the
// backend the caller asked for. Used by the quality-switch candidate loop in
// ensure_engine_for_quality to short-circuit after the first compatible
// candidate prepares successfully.
inline bool backend_matches_request(const DeviceInfo& effective_device,
                                    const DeviceInfo& requested_device) {
    if (requested_device.backend == Backend::CPU) {
        return true;
    }
    return effective_device.backend == requested_device.backend;
}

}  // namespace corridorkey::ofx
