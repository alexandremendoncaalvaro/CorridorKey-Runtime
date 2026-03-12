#include <catch2/catch_all.hpp>
#include <filesystem>

#include "app/output_path_utils.hpp"

using namespace corridorkey::app;

TEST_CASE("runtime output normalization anchors flat file outputs to the working directory",
          "[unit][app][regression]") {
    const std::filesystem::path working_directory = "bundle_root";

    REQUIRE(normalize_runtime_output_path("output.mp4", working_directory) ==
            working_directory / "output.mp4");
}

TEST_CASE("runtime output normalization preserves nested relative file paths",
          "[unit][app][regression]") {
    const std::filesystem::path working_directory = "bundle_root";

    REQUIRE(normalize_runtime_output_path("outputs/output.mp4", working_directory) ==
            std::filesystem::path("outputs/output.mp4"));
}

TEST_CASE("runtime output normalization preserves directory-style outputs",
          "[unit][app][regression]") {
    const std::filesystem::path working_directory = "bundle_root";

    REQUIRE(normalize_runtime_output_path("outputs", working_directory) ==
            std::filesystem::path("outputs"));
}
