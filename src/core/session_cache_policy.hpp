#pragma once

#include <corridorkey/types.hpp>

namespace corridorkey::core {

inline bool use_optimized_model_cache_for_backend(Backend backend) {
    return backend == Backend::CPU;
}

}  // namespace corridorkey::core
