#include <catch2/catch_all.hpp>

#include "plugins/ofx/ofx_constants.hpp"

using namespace corridorkey::ofx;

TEST_CASE("processed-like outputs stay linear regardless of input color space",
          "[unit][ofx][regression]") {
    REQUIRE(output_mode_uses_linear_premultiplied_rgba(kOutputProcessed));
    REQUIRE(output_mode_uses_linear_premultiplied_rgba(kOutputFGMatte));

    REQUIRE_FALSE(should_apply_srgb_to_output(kOutputProcessed, false));
    REQUIRE_FALSE(should_apply_srgb_to_output(kOutputProcessed, true));
    REQUIRE_FALSE(should_apply_srgb_to_output(kOutputFGMatte, false));
    REQUIRE_FALSE(should_apply_srgb_to_output(kOutputFGMatte, true));
}

TEST_CASE("display-referred conversion only applies to non-processed outputs with sRGB input",
          "[unit][ofx][regression]") {
    REQUIRE(should_apply_srgb_to_output(kOutputForegroundOnly, false));
    REQUIRE(should_apply_srgb_to_output(kOutputSourceMatte, false));

    REQUIRE_FALSE(should_apply_srgb_to_output(kOutputForegroundOnly, true));
    REQUIRE_FALSE(should_apply_srgb_to_output(kOutputSourceMatte, true));
    REQUIRE_FALSE(should_apply_srgb_to_output(kOutputMatteOnly, false));
}
