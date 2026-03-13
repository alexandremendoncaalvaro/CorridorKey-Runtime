#include <catch2/catch_all.hpp>
#include <cstdint>
#include <filesystem>
#include <vector>

#include "../../src/frame_io/video_io.hpp"
#include "post_process/color_utils.hpp"

using namespace corridorkey;

TEST_CASE("VideoReader and VideoWriter roundtrip", "[integration][video]") {
    const std::filesystem::path test_video = "test_roundtrip.mov";
    const int w = 128;
    const int h = 128;
    const double fps = 24.0;
    const int num_frames = 5;

    SECTION("Write and then read back a simple video") {
        {
            VideoOutputOptions output_options;
            output_options.mode = VideoOutputMode::Lossless;
            VideoFrameFormat input_format;
            auto writer_res =
                VideoWriter::open(test_video, w, h, fps, input_format, output_options);
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
            auto finalize_res = writer->finalize();
            REQUIRE(finalize_res.has_value());
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
                REQUIRE_FALSE(frame.buffer.view().empty());
                REQUIRE(frame.buffer.view().width == w);
                REQUIRE(frame.buffer.view().height == h);
                REQUIRE(frame.buffer.view().channels == 3);

                // Check color (lossless roundtrip should be precise)
                float expected_r = static_cast<float>(i) / num_frames;
                REQUIRE(frame.buffer.view()(h / 2, w / 2, 0) ==
                        Catch::Approx(expected_r).margin(1.0f / 255.0f));
            }

            // Next frame should be empty (EOF)
            auto eof_res = reader->read_next_frame();
            REQUIRE(eof_res.has_value());
            REQUIRE(eof_res->buffer.view().empty());
        }

        std::filesystem::remove(test_video);
    }

    SECTION("Default encoder selection stays portable") {
        auto default_encoder = default_video_encoder_for_path("portable_output.mp4");

        if (!default_encoder.empty()) {
            REQUIRE(default_encoder != "mpeg4");
            REQUIRE(default_encoder != "h264_videotoolbox");
        }
    }

    SECTION("Writer preserves provided timestamps") {
        {
            VideoOutputOptions output_options;
            output_options.mode = VideoOutputMode::Lossless;
            VideoFrameFormat input_format;
            auto writer_res =
                VideoWriter::open(test_video, w, h, fps, input_format, output_options);
            REQUIRE(writer_res.has_value());
            auto writer = std::move(*writer_res);

            std::vector<int64_t> pts_us = {0, 500000, 1000000, 1500000};
            ImageBuffer frame(w, h, 3);
            for (size_t i = 0; i < pts_us.size(); ++i) {
                float value = static_cast<float>(i) / pts_us.size();
                for (size_t j = 0; j < frame.view().data.size(); j += 3) {
                    frame.view().data[j] = value;
                    frame.view().data[j + 1] = 0.25f;
                    frame.view().data[j + 2] = 1.0f - value;
                }
                auto write_res = writer->write_frame(frame.view(), pts_us[i]);
                REQUIRE(write_res.has_value());
            }
            auto finalize_res = writer->finalize();
            REQUIRE(finalize_res.has_value());
        }

        {
            auto reader_res = VideoReader::open(test_video);
            REQUIRE(reader_res.has_value());
            auto reader = std::move(*reader_res);

            std::vector<int64_t> read_pts;
            while (true) {
                auto frame_res = reader->read_next_frame();
                REQUIRE(frame_res.has_value());
                if (frame_res->buffer.view().empty()) break;
                REQUIRE(frame_res->pts_us.has_value());
                read_pts.push_back(*frame_res->pts_us);
            }

            REQUIRE(read_pts.size() == 4);
            for (size_t i = 1; i < read_pts.size(); ++i) {
                int64_t delta = read_pts[i] - read_pts[i - 1];
                REQUIRE(delta == Catch::Approx(500000).margin(5000));
            }
        }

        std::filesystem::remove(test_video);
    }
}
