#include <catch2/catch_all.hpp>
#include <cstdint>
#include <filesystem>
#include <fstream>

#include "core/mlx_probe.hpp"

using namespace corridorkey;
using namespace corridorkey::core;

namespace {

void write_valid_safetensors_stub(const std::filesystem::path& path) {
    constexpr char kHeader[] = "{\"__metadata__\":{}}";
    const std::uint64_t header_size = sizeof(kHeader) - 1;

    std::ofstream file(path, std::ios::binary);
    REQUIRE(file.is_open());
    file.write(reinterpret_cast<const char*>(&header_size), sizeof(header_size));
    file.write(kHeader, static_cast<std::streamsize>(header_size));
    file.write("\0", 1);
}

}  // namespace

TEST_CASE("mlx weights probe validates safetensors headers", "[integration][mlx]") {
    auto temp_dir = std::filesystem::temp_directory_path() / "corridorkey-mlx-probe";
    std::filesystem::create_directories(temp_dir);

    auto valid_path = temp_dir / "valid.safetensors";
    auto invalid_path = temp_dir / "invalid.safetensors";

    write_valid_safetensors_stub(valid_path);

    {
        std::ofstream invalid_file(invalid_path, std::ios::binary);
        const std::uint64_t invalid_header_size = 4096;
        invalid_file.write(reinterpret_cast<const char*>(&invalid_header_size),
                           sizeof(invalid_header_size));
        invalid_file.write("oops", 4);
    }

    auto valid_probe = probe_mlx_weights(valid_path);
    REQUIRE(valid_probe.has_value());

    auto invalid_probe = probe_mlx_weights(invalid_path);
    REQUIRE_FALSE(invalid_probe.has_value());
    REQUIRE(invalid_probe.error().message.find("Invalid safetensors header size") !=
            std::string::npos);

    std::filesystem::remove(valid_path);
    std::filesystem::remove(invalid_path);
    std::filesystem::remove(temp_dir);
}
