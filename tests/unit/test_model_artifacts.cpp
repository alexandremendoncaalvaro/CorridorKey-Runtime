#include <catch2/catch_all.hpp>
#include <filesystem>
#include <fstream>

#include "common/runtime_paths.hpp"

using namespace corridorkey;

namespace {

std::filesystem::path artifact_test_root() {
    return std::filesystem::temp_directory_path() / "corridorkey-model-artifact-tests";
}

void write_file(const std::filesystem::path& path, const std::string& contents) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    REQUIRE(stream.is_open());
    stream << contents;
}

}  // namespace

TEST_CASE("model artifact inspection distinguishes missing placeholders and usable files",
          "[unit][runtime][regression]") {
    auto root = artifact_test_root();
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);

    const auto missing = root / "missing.onnx";
    const auto placeholder = root / "placeholder.onnx";
    const auto usable = root / "usable.onnx";

    write_file(placeholder,
               "version https://git-lfs.github.com/spec/v1\n"
               "oid sha256:1234567890abcdef\n"
               "size 42\n");
    write_file(usable, "not an lfs pointer");

    SECTION("missing artifacts are reported as missing") {
        const auto inspection = common::inspect_model_artifact(missing);
        REQUIRE_FALSE(inspection.found);
        REQUIRE_FALSE(inspection.usable);
        REQUIRE(inspection.status == common::ModelArtifactStatus::Missing);
    }

    SECTION("git lfs placeholders are reported explicitly") {
        const auto inspection = common::inspect_model_artifact(placeholder);
        REQUIRE(inspection.found);
        REQUIRE_FALSE(inspection.usable);
        REQUIRE(inspection.status == common::ModelArtifactStatus::LfsPlaceholder);
        REQUIRE(inspection.detail.find("Git LFS pointer placeholder") != std::string::npos);
    }

    SECTION("non-pointer files are treated as usable inputs") {
        const auto inspection = common::inspect_model_artifact(usable);
        REQUIRE(inspection.found);
        REQUIRE(inspection.usable);
        REQUIRE(inspection.status == common::ModelArtifactStatus::Usable);
    }

    std::filesystem::remove_all(root);
}
