#include <catch2/catch_all.hpp>

#include "plugins/ofx/ofx_constants.hpp"

using namespace corridorkey::ofx;

TEST_CASE("processed-like outputs stay linear regardless of input color space",
          "[unit][ofx][regression]") {
    REQUIRE(output_mode_uses_linear_premultiplied_rgba(kOutputProcessed));
    REQUIRE(output_mode_uses_linear_premultiplied_rgba(kOutputFGMatte));

    REQUIRE_FALSE(should_apply_srgb_to_output(kOutputProcessed, false, false));
    REQUIRE_FALSE(should_apply_srgb_to_output(kOutputProcessed, false, true));
    REQUIRE_FALSE(should_apply_srgb_to_output(kOutputFGMatte, false, false));
    REQUIRE_FALSE(should_apply_srgb_to_output(kOutputFGMatte, false, true));
}

TEST_CASE("matte-focused outputs do not require model foreground buffers",
          "[unit][ofx][regression]") {
    REQUIRE_FALSE(output_mode_requires_model_foreground(kOutputMatteOnly));
    REQUIRE_FALSE(output_mode_requires_model_foreground(kOutputSourceMatte));
    REQUIRE(output_mode_requires_model_foreground(kOutputProcessed));
    REQUIRE(output_mode_requires_model_foreground(kOutputForegroundOnly));
    REQUIRE(output_mode_requires_model_foreground(kOutputFGMatte));
}

TEST_CASE("display-referred conversion only applies to non-processed outputs with sRGB input",
          "[unit][ofx][regression]") {
    REQUIRE(should_apply_srgb_to_output(kOutputForegroundOnly, false, false));
    REQUIRE(should_apply_srgb_to_output(kOutputSourceMatte, false, false));

    REQUIRE_FALSE(should_apply_srgb_to_output(kOutputForegroundOnly, false, true));
    REQUIRE_FALSE(should_apply_srgb_to_output(kOutputSourceMatte, false, true));
    REQUIRE_FALSE(should_apply_srgb_to_output(kOutputMatteOnly, false, false));
}

TEST_CASE("host-managed outputs stay linear for every colour output mode",
          "[unit][ofx][regression]") {
    REQUIRE_FALSE(should_apply_srgb_to_output(kOutputProcessed, true, false));
    REQUIRE_FALSE(should_apply_srgb_to_output(kOutputFGMatte, true, false));
    REQUIRE_FALSE(should_apply_srgb_to_output(kOutputForegroundOnly, true, false));
    REQUIRE_FALSE(should_apply_srgb_to_output(kOutputSourceMatte, true, false));
    REQUIRE_FALSE(should_apply_srgb_to_output(kOutputMatteOnly, true, false));
}

TEST_CASE("host-managed output colourspace is explicit by output mode", "[unit][ofx][regression]") {
    REQUIRE(std::string_view(output_colourspace_for_output_mode(kOutputProcessed)) ==
            kOfxColourspaceLinRec709Srgb);
    REQUIRE(std::string_view(output_colourspace_for_output_mode(kOutputFGMatte)) ==
            kOfxColourspaceLinRec709Srgb);
    REQUIRE(std::string_view(output_colourspace_for_output_mode(kOutputForegroundOnly)) ==
            kOfxColourspaceLinRec709Srgb);
    REQUIRE(std::string_view(output_colourspace_for_output_mode(kOutputSourceMatte)) ==
            kOfxColourspaceLinRec709Srgb);
    REQUIRE(std::string_view(output_colourspace_for_output_mode(kOutputMatteOnly)) ==
            kOfxColourspaceRaw);
}
