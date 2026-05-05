#pragma once

#include "post_process/despill.hpp"

namespace corridorkey {

inline SpillMethod effective_despill_method(int requested_method, int screen_channel) {
    if (screen_channel == 2) {
        return SpillMethod::ScreenOnly;
    }
    return static_cast<SpillMethod>(requested_method);
}

}  // namespace corridorkey
