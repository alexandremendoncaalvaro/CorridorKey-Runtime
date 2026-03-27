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

TEST_CASE("coarse-to-fine coarse pass disables model tiling", "[unit][core][regression]") {
    InferenceParams params;
    params.target_resolution = 1536;
    params.requested_quality_resolution = 1536;
    params.enable_tiling = true;

    const auto coarse_params = core::coarse_inference_params(params, 1024);
    CHECK(coarse_params.target_resolution == 1024);
    CHECK(coarse_params.requested_quality_resolution == 1536);
    CHECK_FALSE(coarse_params.enable_tiling);
}

TEST_CASE("coarse-to-fine keeps artifact-only execution controls intact",
          "[unit][core][regression]") {
    InferenceParams params;
    params.target_resolution = 2048;
    params.requested_quality_resolution = 2048;
    params.quality_fallback_mode = QualityFallbackMode::CoarseToFine;
    params.refinement_mode = RefinementMode::Auto;
    params.coarse_resolution_override = 1024;
    params.tile_padding = 96;

    const auto coarse_params = core::coarse_inference_params(params, 1024);
    CHECK(coarse_params.target_resolution == 1024);
    CHECK(coarse_params.requested_quality_resolution == 2048);
    CHECK(coarse_params.quality_fallback_mode == QualityFallbackMode::CoarseToFine);
    CHECK(coarse_params.refinement_mode == RefinementMode::Auto);
    CHECK(coarse_params.coarse_resolution_override == 1024);
    CHECK(coarse_params.tile_padding == 96);
}
