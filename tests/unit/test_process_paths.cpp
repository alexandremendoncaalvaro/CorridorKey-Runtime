#include <catch2/catch_all.hpp>
#include <filesystem>

#include "cli/process_paths.hpp"

using namespace corridorkey;

TEST_CASE("process path resolution preserves the documented positional workflow",
          "[unit][cli][regression]") {
    auto resolved =
        cli::resolve_process_paths(std::nullopt, std::nullopt, {"input.mp4", "output.mp4"});

    REQUIRE(resolved.has_value());
    REQUIRE(resolved->input_path == std::filesystem::path("input.mp4"));
    REQUIRE(resolved->output_path == std::filesystem::path("output.mp4"));
}

TEST_CASE("process path resolution accepts a positional output with explicit input",
          "[unit][cli][regression]") {
    auto resolved = cli::resolve_process_paths(std::filesystem::path("input.mp4"), std::nullopt,
                                               {"output.mp4"});

    REQUIRE(resolved.has_value());
    REQUIRE(resolved->input_path == std::filesystem::path("input.mp4"));
    REQUIRE(resolved->output_path == std::filesystem::path("output.mp4"));
}

TEST_CASE("process path resolution rejects ambiguous extra positional paths",
          "[unit][cli][regression]") {
    auto resolved = cli::resolve_process_paths(std::filesystem::path("input.mp4"), std::nullopt,
                                               {"output.mp4", "extra.mp4"});

    REQUIRE_FALSE(resolved.has_value());
    REQUIRE(resolved.error().code == ErrorCode::InvalidParameters);
}
