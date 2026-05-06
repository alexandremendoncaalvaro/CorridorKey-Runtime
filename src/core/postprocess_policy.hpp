#pragma once

#include "post_process/despill.hpp"

namespace corridorkey {

struct DespillMethodRequest {
    int requested_method = 0;
    int screen_channel = 0;
};

inline SpillMethod effective_despill_method(DespillMethodRequest request) {
    if (request.screen_channel == 2) {
        return SpillMethod::ScreenOnly;
    }
    return static_cast<SpillMethod>(request.requested_method);
}

}  // namespace corridorkey
