#include <catch2/catch_all.hpp>

#include "../../src/frame_io/video_io.hpp"

using namespace corridorkey;

TEST_CASE("Regression: default video encoder is never empty for MP4 outputs", "[regression]") {
    REQUIRE_FALSE(default_video_encoder_for_path("regression_output.mp4").empty());
}
