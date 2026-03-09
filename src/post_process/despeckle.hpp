#pragma once

#include <corridorkey/types.hpp>

namespace corridorkey {

/**
 * @brief Morphological erosion + dilation for alpha cleanup.
 * Removes small specks (connected components below size_threshold area).
 */
void despeckle(Image alpha, int size_threshold);

} // namespace corridorkey
