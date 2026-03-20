#include <catch2/catch_all.hpp>
#include <filesystem>
#include <system_error>

#include "common/shared_memory_transport.hpp"

using namespace corridorkey::common;

TEST_CASE("shared frame transport can be opened while the creator is still alive",
          "[integration][ofx][runtime][regression]") {
    const auto transport_path =
        std::filesystem::temp_directory_path() / "corridorkey_shared_frame_test.ckfx";
    std::error_code error;
    std::filesystem::remove(transport_path, error);

    {
        auto created = SharedFrameTransport::create(transport_path, 64, 32);
        REQUIRE(created.has_value());

        auto opened = SharedFrameTransport::open(transport_path);
        REQUIRE(opened.has_value());
        CHECK(opened->width() == 64);
        CHECK(opened->height() == 32);
    }

    std::filesystem::remove(transport_path, error);
}
