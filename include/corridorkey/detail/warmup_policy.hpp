#pragma once

#include <optional>

namespace corridorkey::detail {

inline int resolve_warmup_resolution(int target_resolution, int recommended_resolution) {
    return target_resolution > 0 ? target_resolution : recommended_resolution;
}

inline bool should_run_warmup(int desired_resolution,
                              const std::optional<int>& last_warmup_resolution) {
    if (!last_warmup_resolution.has_value()) {
        return true;
    }
    return desired_resolution > *last_warmup_resolution;
}

}  // namespace corridorkey::detail
