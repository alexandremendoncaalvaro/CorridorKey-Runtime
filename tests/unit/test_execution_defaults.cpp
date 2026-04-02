#include <catch2/catch_all.hpp>
#include <filesystem>
#include <fstream>

#include "app/runtime_contracts.hpp"

using namespace corridorkey;
using namespace corridorkey::app;

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

    auto resolved =
        resolve_model_artifact_for_request(temp_dir.path() / "corridorkey_fp16_1536.onnx", params,
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

    auto resolved = resolve_model_artifact_for_request(
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

    auto resolved = resolve_model_artifact_for_request(
        "C:/models/custom_keyer.onnx", params, DeviceInfo{"RTX 4090", 24576, Backend::TensorRT});

    REQUIRE(resolved.has_value());
    CHECK(*resolved == std::filesystem::path("C:/models/custom_keyer.onnx"));
}

TEST_CASE("coarse-to-fine rejects equal coarse overrides clearly", "[unit][runtime][regression]") {
    TempDirGuard temp_dir("corridorkey-runtime-equal-coarse-override");
    touch_file(temp_dir.path() / "corridorkey_fp16_1024.onnx");

    InferenceParams params;
    params.target_resolution = 1024;
    params.requested_quality_resolution = 1024;
    params.quality_fallback_mode = QualityFallbackMode::CoarseToFine;
    params.coarse_resolution_override = 1024;

    auto resolved =
        resolve_model_artifact_for_request(temp_dir.path() / "corridorkey_fp16_1024.onnx", params,
                                           DeviceInfo{"RTX 4090", 24576, Backend::TensorRT});

    REQUIRE_FALSE(resolved.has_value());
    CHECK(resolved.error().code == ErrorCode::InvalidParameters);
    CHECK(resolved.error().message.find("smaller than the requested quality") != std::string::npos);
}

TEST_CASE("packaged coarse-to-fine fails clearly when the coarse artifact is missing",
          "[unit][runtime][regression]") {
    TempDirGuard temp_dir("corridorkey-runtime-missing-coarse-artifact");
    touch_file(temp_dir.path() / "corridorkey_fp16_1536.onnx");

    InferenceParams params;
    params.target_resolution = 1536;
    params.requested_quality_resolution = 1536;
    params.quality_fallback_mode = QualityFallbackMode::CoarseToFine;

    auto resolved =
        resolve_model_artifact_for_request(temp_dir.path() / "corridorkey_fp16_1536.onnx", params,
                                           DeviceInfo{"RTX 3080", 10240, Backend::TensorRT});

    REQUIRE_FALSE(resolved.has_value());
    CHECK(resolved.error().code == ErrorCode::ModelLoadFailed);
    CHECK(resolved.error().message.find("corridorkey_fp16_1024.onnx") != std::string::npos);
}

TEST_CASE("non-auto refinement overrides fail clearly for current packaged artifacts",
          "[unit][runtime][regression]") {
    TempDirGuard temp_dir("corridorkey-runtime-refinement-override");
    touch_file(temp_dir.path() / "corridorkey_fp16_1024.onnx");

    InferenceParams params;
    params.target_resolution = 1024;
    params.requested_quality_resolution = 1024;
    params.quality_fallback_mode = QualityFallbackMode::Direct;
    params.refinement_mode = RefinementMode::Tiled;

    auto resolved =
        resolve_model_artifact_for_request(temp_dir.path() / "corridorkey_fp16_1024.onnx", params,
                                           DeviceInfo{"RTX 4090", 24576, Backend::TensorRT});

    REQUIRE_FALSE(resolved.has_value());
    CHECK(resolved.error().code == ErrorCode::InvalidParameters);
    CHECK(resolved.error().message.find("refinement strategy") != std::string::npos);
}
