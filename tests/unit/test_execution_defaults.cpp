#include <catch2/catch_all.hpp>

#include <filesystem>
#include <fstream>

#include "cli/execution_defaults.hpp"

using namespace corridorkey;

namespace {

class TempDirGuard {
   public:
    explicit TempDirGuard(const std::string& name)
        : m_path(std::filesystem::temp_directory_path() / name) {
        std::filesystem::remove_all(m_path);
        std::filesystem::create_directories(m_path);
    }

    ~TempDirGuard() {
        std::filesystem::remove_all(m_path);
    }

    [[nodiscard]] const std::filesystem::path& path() const {
        return m_path;
    }

   private:
    std::filesystem::path m_path;
};

void touch_file(const std::filesystem::path& path) {
    std::ofstream file(path, std::ios::binary);
    REQUIRE(file.is_open());
    file << "stub";
}

}  // namespace

TEST_CASE("packaged explicit models rewrite to a coarse sibling when coarse-to-fine is needed",
          "[unit][cli][regression]") {
    TempDirGuard temp_dir("corridorkey-cli-explicit-model");
    touch_file(temp_dir.path() / "corridorkey_fp16_1536.onnx");
    touch_file(temp_dir.path() / "corridorkey_fp16_1024.onnx");

    InferenceParams params;
    params.target_resolution = 1536;
    params.requested_quality_resolution = 1536;
    params.quality_fallback_mode = QualityFallbackMode::CoarseToFine;

    auto resolved = cli::resolve_explicit_model_quality_fallback(
        temp_dir.path() / "corridorkey_fp16_1536.onnx", params,
        DeviceInfo{"RTX 3080", 10240, Backend::TensorRT});

    REQUIRE(resolved.has_value());
    CHECK(resolved->filename() == "corridorkey_fp16_1024.onnx");
}

TEST_CASE("custom explicit models reject forced coarse-to-fine clearly",
          "[unit][cli][regression]") {
    InferenceParams params;
    params.target_resolution = 1536;
    params.requested_quality_resolution = 1536;
    params.quality_fallback_mode = QualityFallbackMode::CoarseToFine;

    auto resolved = cli::resolve_explicit_model_quality_fallback(
        "C:/models/custom_keyer.onnx", params, DeviceInfo{"RTX 3080", 10240, Backend::TensorRT});

    REQUIRE_FALSE(resolved.has_value());
    CHECK(resolved.error().code == ErrorCode::InvalidParameters);
    CHECK(resolved.error().message.find("packaged CorridorKey artifacts") != std::string::npos);
}

TEST_CASE("explicit models keep direct behavior when coarse-to-fine is not requested",
          "[unit][cli][regression]") {
    InferenceParams params;
    params.target_resolution = 1024;
    params.requested_quality_resolution = 1024;
    params.quality_fallback_mode = QualityFallbackMode::Direct;

    auto resolved = cli::resolve_explicit_model_quality_fallback(
        "C:/models/custom_keyer.onnx", params, DeviceInfo{"RTX 4090", 24576, Backend::TensorRT});

    REQUIRE(resolved.has_value());
    CHECK(*resolved == std::filesystem::path("C:/models/custom_keyer.onnx"));
}
