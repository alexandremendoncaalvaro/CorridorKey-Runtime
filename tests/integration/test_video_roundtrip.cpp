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

#if defined(__APPLE__)
    SECTION("macOS prefers ProRes 4444 via VideoToolbox for .mov lossless") {
        // When the VideoToolbox ProRes encoder is present in the FFmpeg build the
        // default lossless encoder for .mov must be prores_videotoolbox: it runs on
        // the Media Engine instead of the CPU and stays mathematically lossless
        // for alpha. Builds that do not ship prores_videotoolbox fall back to
        // qtrle or png, so this test is guarded on the encoder's availability.
        if (is_videotoolbox_encoder_available("prores_videotoolbox")) {
            auto encoder = default_video_encoder_for_path("prores_probe.mov");
            REQUIRE(encoder == "prores_videotoolbox");

            auto candidates = available_video_encoders_for_path("prores_probe.mov");
            REQUIRE(!candidates.empty());
            REQUIRE(candidates.front() == "prores_videotoolbox");
        }
    }

    SECTION("ProRes 4444 roundtrip preserves RGB content above 8-bit tolerance") {
        if (!is_videotoolbox_encoder_available("prores_videotoolbox")) {
            SUCCEED("prores_videotoolbox not available in this build");
            return;
        }

        const std::filesystem::path prores_video = "test_prores_roundtrip.mov";
        {
            VideoOutputOptions output_options;
            output_options.mode = VideoOutputMode::Lossless;
            VideoFrameFormat input_format;
            input_format.bits_per_component = 10;
            auto writer_res =
                VideoWriter::open(prores_video, w, h, fps, input_format, output_options);
            REQUIRE(writer_res.has_value());
            auto writer = std::move(*writer_res);

            ImageBuffer frame(w, h, 3);
            for (int i = 0; i < num_frames; ++i) {
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
        }

        REQUIRE(std::filesystem::exists(prores_video));
        REQUIRE(std::filesystem::file_size(prores_video) > 0);

        {
            auto reader_res = VideoReader::open(prores_video);
            REQUIRE(reader_res.has_value());
            auto reader = std::move(*reader_res);
            REQUIRE(reader->width() == w);
            REQUIRE(reader->height() == h);

            for (int i = 0; i < num_frames; ++i) {
                auto frame_res = reader->read_next_frame();
                REQUIRE(frame_res.has_value());
                auto frame = std::move(*frame_res);
                REQUIRE_FALSE(frame.buffer.view().empty());
                float expected_r = static_cast<float>(i) / num_frames;
                // ProRes 4444 uses 10-bit 4:4:4 chroma so the roundtrip is well
                // below the 8-bit tolerance used for the default lossless path.
                REQUIRE(frame.buffer.view()(h / 2, w / 2, 0) ==
                        Catch::Approx(expected_r).margin(2.0f / 1023.0f));
            }
        }

        std::filesystem::remove(prores_video);
    }
#endif

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
