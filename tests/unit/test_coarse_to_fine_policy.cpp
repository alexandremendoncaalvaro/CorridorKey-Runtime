#include <catch2/catch_all.hpp>

#include "core/coarse_to_fine_policy.hpp"

using namespace corridorkey;

TEST_CASE("coarse-to-fine policy uses requested quality against model resolution",
          "[unit][core][regression]") {
    InferenceParams params;
    params.target_resolution = 1024;
    params.requested_quality_resolution = 1536;
    CHECK(core::should_use_coarse_to_fine_path(params, 1024));

    params.quality_fallback_mode = QualityFallbackMode::Direct;
    CHECK_FALSE(core::should_use_coarse_to_fine_path(params, 1024));
}

TEST_CASE("local refinement tile region clamps undersized inputs safely",
          "[unit][core][regression]") {
    const auto region = core::local_refinement_tile_region(320, 240, 1024, 960, 0, 0);
    CHECK(region.x_start == 0);
    CHECK(region.y_start == 0);
    CHECK(region.width == 320);
    CHECK(region.height == 240);

    InferenceParams params;
    params.refinement_mode = RefinementMode::Tiled;
    CHECK(core::should_tile_local_refinement(params, 1024, 320, 240));
}
