#pragma once

#include <corridorkey/types.hpp>

namespace corridorkey {

/**
 * @brief Luminance-preserving green spill removal.
 * Reduces green channel based on alpha and average of R/B.
 */
void despill(Image rgb, const Image alpha, float strength);

} // namespace corridorkey
