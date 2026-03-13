#include <catch2/catch_all.hpp>
#include <filesystem>

#include "../../src/frame_io/video_io.hpp"

using namespace corridorkey;

TEST_CASE("VideoReader decodes all frames with B-frames", "[integration][video][regression]") {
    auto sample_path =
        std::filesystem::path(PROJECT_ROOT) / "assets/video_samples/greenscreen_1769569320.mp4";

    auto reader_res = VideoReader::open(sample_path);
    REQUIRE(reader_res.has_value());
    auto reader = std::move(*reader_res);

    int frame_count = 0;
    while (true) {
        auto frame_res = reader->read_next_frame();
        REQUIRE(frame_res.has_value());
        if (frame_res->buffer.view().empty()) {
            break;
        }
        ++frame_count;
    }

    REQUIRE(frame_count == 40);
}

TEST_CASE("VideoReader reports stable fps for sample clip", "[integration][video][regression]") {
    auto sample_path =
        std::filesystem::path(PROJECT_ROOT) / "assets/video_samples/greenscreen_1769569320.mp4";

    auto reader_res = VideoReader::open(sample_path);
    REQUIRE(reader_res.has_value());
    auto reader = std::move(*reader_res);

    REQUIRE(reader->fps() == Catch::Approx(20.0).margin(0.5));
}
