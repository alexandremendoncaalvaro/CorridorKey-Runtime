#pragma once

#include <corridorkey/types.hpp>

namespace corridorkey {

/**
 * @brief Green spill removal with redistribution.
 * Clamps green to avg(R,B) and redistributes removed spill equally to R and B.
 * Matches the Python CorridorKey reference implementation.
 * @param rgb RGB image to despill in-place
 * @param strength 0.0 (no effect) to 1.0 (full despill)
 */
void despill(Image rgb, float strength);

}  // namespace corridorkey
