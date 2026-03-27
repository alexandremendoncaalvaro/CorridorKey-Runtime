#pragma once

#include <corridorkey/types.hpp>

namespace corridorkey::core {

inline bool should_skip_warmup(Backend backend, int desired_resolution) {
#if defined(_WIN32)
    return backend == Backend::TensorRT && desired_resolution > 1024;
#else
    (void)backend;
    (void)desired_resolution;
    return false;
#endif
}

}  // namespace corridorkey::core
