#pragma once

#include <corridorkey/types.hpp>

namespace corridorkey {

enum class SpillMethod : int { Average = 0, DoubleLimit = 1, Neutral = 2 };

void despill(Image rgb, float strength, SpillMethod method = SpillMethod::Average);

}  // namespace corridorkey
