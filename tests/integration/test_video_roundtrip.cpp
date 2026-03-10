#include <catch2/catch_all.hpp>
#include <filesystem>

#include "../../src/frame_io/video_io.hpp"
#include "post_process/color_utils.hpp"

using namespace corridorkey;

TEST_CASE("VideoReader and VideoWriter roundtrip", "[integration][video]") {
    const std::filesystem::path test_video = "test_roundtrip.mp4";
    const int w = 128;
    const int h = 128;
    const double fps = 24.0;
    const int num_frames = 5;

    SECTION("Write and then read back a simple video") {
        {
            auto writer_res = VideoWriter::open(test_video, w, h, fps);
            REQUIRE(writer_res.has_value());
            auto writer = std::move(*writer_res);

            ImageBuffer frame(w, h, 3);
            for (int i = 0; i < num_frames; ++i) {
                // Fill with a recognizable color pattern
                float r = static_cast<float>(i) / num_frames;
                for (size_t j = 0; j < frame.view().data.size(); j += 3) {
                    frame.view().data[j] = r;
                    frame.view().data[j + 1] = 0.5f;
                    frame.view().data[j + 2] = 1.0f - r;
                }
                auto write_res = writer->write_frame(frame.view());
                REQUIRE(write_res.has_value());
            }
        }  // Writer closed here

        REQUIRE(std::filesystem::exists(test_video));
        REQUIRE(std::filesystem::file_size(test_video) > 0);

        {
            auto reader_res = VideoReader::open(test_video);
            REQUIRE(reader_res.has_value());
            auto reader = std::move(*reader_res);

            REQUIRE(reader->width() == w);
            REQUIRE(reader->height() == h);
            // FPS might vary slightly depending on container format
            REQUIRE(reader->fps() == Catch::Approx(fps).margin(0.1));

            for (int i = 0; i < num_frames; ++i) {
                auto frame_res = reader->read_next_frame();
                REQUIRE(frame_res.has_value());
                auto frame = std::move(*frame_res);
                REQUIRE_FALSE(frame.view().empty());
                REQUIRE(frame.view().width == w);
                REQUIRE(frame.view().height == h);
                REQUIRE(frame.view().channels == 3);

                // Check color (with tolerance due to video compression)
                float expected_r = static_cast<float>(i) / num_frames;
                // MPEG4 compression can be quite lossy, so we use a generous margin
                REQUIRE(frame.view()(h / 2, w / 2, 0) == Catch::Approx(expected_r).margin(0.15));
            }

            // Next frame should be empty (EOF)
            auto eof_res = reader->read_next_frame();
            REQUIRE(eof_res.has_value());
            REQUIRE(eof_res->view().empty());
        }

        std::filesystem::remove(test_video);
    }

    SECTION("Default encoder selection stays portable") {
        auto default_encoder = default_video_encoder_for_path("portable_output.mp4");

        REQUIRE_FALSE(default_encoder.empty());

#if defined(__APPLE__)
        if (is_videotoolbox_available()) {
            REQUIRE(default_encoder == "h264_videotoolbox");
        }
#endif
    }
}
