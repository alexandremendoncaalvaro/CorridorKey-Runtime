#include <catch2/catch_all.hpp>
#include <corridorkey/frame_io.hpp>
#include <filesystem>

using namespace corridorkey;

TEST_CASE("EXR roundtrip: write then read preserves data", "[integration][exr]") {
    auto tmp_path = std::filesystem::temp_directory_path() / "corridorkey_test_roundtrip.exr";

    // Create a small test image (4x4 RGBA)
    ImageBuffer original(4, 4, 4);
    Image view = original.view();
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            view(y, x, 0) = static_cast<float>(x) / 3.0f;  // R gradient
            view(y, x, 1) = static_cast<float>(y) / 3.0f;  // G gradient
            view(y, x, 2) = 0.5f;                          // B constant
            view(y, x, 3) = 1.0f;                          // A opaque
        }
    }

    // Write
    auto write_res = frame_io::write_frame(tmp_path, view);
    REQUIRE(write_res.has_value());

    // Read back
    auto read_res = frame_io::read_frame(tmp_path);
    REQUIRE(read_res.has_value());

    Image loaded = read_res->view();
    REQUIRE(loaded.width == 4);
    REQUIRE(loaded.height == 4);
    REQUIRE(loaded.channels == 4);

    // Compare values (EXR uses half-float, so allow half-float precision)
    for (size_t i = 0; i < view.data.size(); ++i) {
        REQUIRE(loaded.data[i] == Catch::Approx(view.data[i]).margin(0.001f));
    }

    // Cleanup
    std::filesystem::remove(tmp_path);
}
