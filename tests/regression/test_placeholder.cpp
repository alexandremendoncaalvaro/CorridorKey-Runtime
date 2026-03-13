#include <catch2/catch_all.hpp>
#include <string>

#include "../../src/frame_io/video_io.hpp"

using namespace corridorkey;

TEST_CASE("Regression: MP4 lossless output does not silently downgrade", "[regression]") {
    VideoFrameFormat input_format;
    VideoOutputOptions options;
    options.mode = VideoOutputMode::Lossless;

    auto encoder = default_video_encoder_for_path("regression_output.mp4");
    if (encoder.empty()) {
        auto plan = resolve_video_output_plan("regression_output.mp4", options, input_format);
        REQUIRE_FALSE(plan.has_value());
        REQUIRE(plan.error().message.find("Lossless output is not available") !=
                std::string::npos);
    } else {
        REQUIRE((encoder == "libx264rgb" || encoder == "libx264"));
    }
}
