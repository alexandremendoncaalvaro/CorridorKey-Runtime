#pragma once

#include <corridorkey/types.hpp>

namespace corridorkey {

enum class SpillMethod : int { Average = 0, DoubleLimit = 1, Neutral = 2 };

// Removes screen-color spill from an RGB image and redistributes the removed
// energy into the two non-screen channels. The screen channel index defaults to
// 1 (green) so existing callers keep their semantics; pass 2 for a blue screen.
//
// Why: upstream Niko/CorridorKey reference, the despill_opencv function in
// CorridorKeyModule/core/color_utils.py, only implements green; blue plates
// were previously supported via a green-domain canonicalization workaround.
// Routing dedicated blue-trained weights requires the same despill math
// applied to a different channel.
void despill(Image rgb, float strength, SpillMethod method = SpillMethod::Average,
             int screen_channel = 1);

}  // namespace corridorkey
