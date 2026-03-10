#include <catch2/catch_all.hpp>
#include <corridorkey/frame_io.hpp>
#include <filesystem>

using namespace corridorkey;

TEST_CASE("PNG roundtrip: write then read preserves data within 8-bit precision",
          "[integration][png]") {
    auto tmp_path = std::filesystem::temp_directory_path() / "corridorkey_test_roundtrip.png";

    // Create a small test image (4x4 RGB) in linear space
    ImageBuffer original(4, 4, 3);
    Image view = original.view();
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            view(y, x, 0) = static_cast<float>(x) / 3.0f;
            view(y, x, 1) = static_cast<float>(y) / 3.0f;
            view(y, x, 2) = 0.5f;
        }
    }

    // Write (linear -> sRGB -> 8-bit PNG)
    auto write_res = frame_io::write_frame(tmp_path, view);
    REQUIRE(write_res.has_value());

    // Read back (8-bit PNG -> sRGB -> linear)
    auto read_res = frame_io::read_frame(tmp_path);
    REQUIRE(read_res.has_value());

    Image loaded = read_res->view();
    REQUIRE(loaded.width == 4);
    REQUIRE(loaded.height == 4);
    REQUIRE(loaded.channels == 3);

    // 8-bit PNG has limited precision: allow ~1/128 margin due to sRGB<->linear roundtrip
    for (size_t i = 0; i < view.data.size(); ++i) {
        REQUIRE(loaded.data[i] == Catch::Approx(view.data[i]).margin(0.02f));
    }

    std::filesystem::remove(tmp_path);
}
