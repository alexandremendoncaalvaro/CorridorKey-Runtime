#pragma once

#include <corridorkey/types.hpp>

namespace corridorkey::core {

inline int intra_op_threads_for_backend(Backend backend) {
    if (backend == Backend::CPU) {
        return 0;
    }

    return 1;
}

}  // namespace corridorkey::core
