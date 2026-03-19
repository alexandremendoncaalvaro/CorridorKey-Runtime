#include <catch2/catch_all.hpp>
#include <filesystem>
#include <fstream>

#include "plugins/ofx/ofx_model_selection.hpp"
#include "plugins/ofx/ofx_shared.hpp"

using namespace corridorkey;
using namespace corridorkey::ofx;

namespace {

void touch_file(const std::filesystem::path& path) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary);
    REQUIRE(file.is_open());
    file << "stub";
}

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

RuntimeCapabilities mac_capabilities() {
    RuntimeCapabilities capabilities;
    capabilities.platform = "macos";
    capabilities.apple_silicon = true;
    capabilities.mlx_probe_available = true;
    capabilities.supported_backends = {Backend::MLX, Backend::CoreML, Backend::CPU};
    return capabilities;
}

RuntimeCapabilities windows_capabilities() {
    RuntimeCapabilities capabilities;
    capabilities.platform = "windows";
    capabilities.supported_backends = {Backend::TensorRT, Backend::CUDA, Backend::CPU};
    return capabilities;
}

}  // namespace

TEST_CASE("ofx bootstrap prefers mlx on apple silicon when bootstrap artifacts are present",
          "[unit][ofx][regression]") {
    TempDirGuard temp_dir("corridorkey-ofx-bootstrap-mlx");
    touch_file(temp_dir.path() / "corridorkey_mlx.safetensors");
    touch_file(temp_dir.path() / "corridorkey_mlx_bridge_512.mlxfn");
    touch_file(temp_dir.path() / "corridorkey_int8_512.onnx");

    auto candidates = build_bootstrap_candidates(
        mac_capabilities(), DeviceInfo{"Apple Silicon", 65536, Backend::CoreML}, temp_dir.path());

    REQUIRE_FALSE(candidates.empty());
#if defined(__APPLE__)
    REQUIRE(candidates.front().device.backend == Backend::MLX);
    REQUIRE(candidates.front().requested_model_path.filename() == "corridorkey_mlx.safetensors");
    REQUIRE(candidates.front().executable_model_path.filename() ==
            "corridorkey_mlx_bridge_512.mlxfn");
#else
    REQUIRE(candidates.front().device.backend == Backend::CoreML);
#endif
}

TEST_CASE("ofx bootstrap falls back to detected backend when mlx bootstrap artifacts are missing",
          "[unit][ofx][regression]") {
    TempDirGuard temp_dir("corridorkey-ofx-bootstrap-fallback");
    touch_file(temp_dir.path() / "corridorkey_int8_512.onnx");

    auto candidates = build_bootstrap_candidates(
        mac_capabilities(), DeviceInfo{"Apple Silicon", 65536, Backend::CoreML}, temp_dir.path());

    REQUIRE_FALSE(candidates.empty());
    REQUIRE(candidates.front().device.backend == Backend::CoreML);
    REQUIRE(candidates.front().requested_model_path.filename() == "corridorkey_int8_512.onnx");
    REQUIRE(candidates.front().executable_model_path.filename() == "corridorkey_int8_512.onnx");
}

TEST_CASE("fixed mlx quality resolves the exact requested bridge", "[unit][ofx][regression]") {
    TempDirGuard temp_dir("corridorkey-ofx-quality-fixed");
    touch_file(temp_dir.path() / "corridorkey_mlx_bridge_512.mlxfn");
    touch_file(temp_dir.path() / "corridorkey_mlx_bridge_768.mlxfn");
    touch_file(temp_dir.path() / "corridorkey_mlx_bridge_1024.mlxfn");
    touch_file(temp_dir.path() / "corridorkey_mlx_bridge_1536.mlxfn");
    touch_file(temp_dir.path() / "corridorkey_mlx_bridge_2048.mlxfn");

    auto selection =
        select_quality_artifact(temp_dir.path(), Backend::MLX, kQualityMaximum, 3840, 2160);

    REQUIRE(selection.has_value());
    REQUIRE(selection->requested_resolution == 2048);
    REQUIRE(selection->effective_resolution == 2048);
    REQUIRE_FALSE(selection->used_fallback);
    REQUIRE(selection->executable_model_path.filename() == "corridorkey_mlx_bridge_2048.mlxfn");
}

TEST_CASE("fixed mlx quality fails when the exact bridge is unavailable",
          "[unit][ofx][regression]") {
    TempDirGuard temp_dir("corridorkey-ofx-quality-missing");
    touch_file(temp_dir.path() / "corridorkey_mlx_bridge_512.mlxfn");
    touch_file(temp_dir.path() / "corridorkey_mlx_bridge_1536.mlxfn");

    auto selection =
        select_quality_artifact(temp_dir.path(), Backend::MLX, kQualityMaximum, 3840, 2160);

    REQUIRE_FALSE(selection.has_value());
}

TEST_CASE("auto mlx quality may fall back to the highest available lower bridge",
          "[unit][ofx][regression]") {
    TempDirGuard temp_dir("corridorkey-ofx-quality-auto");
    touch_file(temp_dir.path() / "corridorkey_mlx_bridge_512.mlxfn");
    touch_file(temp_dir.path() / "corridorkey_mlx_bridge_1024.mlxfn");
    touch_file(temp_dir.path() / "corridorkey_mlx_bridge_1536.mlxfn");

    auto selection =
        select_quality_artifact(temp_dir.path(), Backend::MLX, kQualityAuto, 4096, 2160);

    REQUIRE(selection.has_value());
    REQUIRE(selection->requested_resolution == 2048);
    REQUIRE(selection->effective_resolution == 1536);
    REQUIRE(selection->used_fallback);
    REQUIRE(selection->executable_model_path.filename() == "corridorkey_mlx_bridge_1536.mlxfn");
}

TEST_CASE("fixed windows tensorRT quality fails when the exact model is unavailable",
          "[unit][ofx][regression]") {
    TempDirGuard temp_dir("corridorkey-ofx-windows-quality-missing");
    touch_file(temp_dir.path() / "corridorkey_fp16_768.onnx");
    touch_file(temp_dir.path() / "corridorkey_fp16_1024.onnx");
    touch_file(temp_dir.path() / "corridorkey_fp16_1536.onnx");

    auto selection =
        select_quality_artifact(temp_dir.path(), Backend::TensorRT, kQualityMaximum, 4096, 2160);

    REQUIRE_FALSE(selection.has_value());
}

TEST_CASE("auto windows tensorRT quality falls back to the highest packaged model",
          "[unit][ofx][regression]") {
    TempDirGuard temp_dir("corridorkey-ofx-windows-quality-auto");
    touch_file(temp_dir.path() / "corridorkey_fp16_768.onnx");
    touch_file(temp_dir.path() / "corridorkey_fp16_1024.onnx");
    touch_file(temp_dir.path() / "corridorkey_fp16_1536.onnx");
    touch_file(temp_dir.path() / "corridorkey_int8_512.onnx");

    auto candidates = build_bootstrap_candidates(
        windows_capabilities(), DeviceInfo{"RTX", 24576, Backend::TensorRT}, temp_dir.path());
    REQUIRE_FALSE(candidates.empty());
    REQUIRE(candidates.front().device.backend == Backend::TensorRT);
    REQUIRE(candidates.front().requested_model_path.filename() == "corridorkey_fp16_768.onnx");
    REQUIRE(candidates.front().executable_model_path.filename() == "corridorkey_fp16_768.onnx");

    auto selection =
        select_quality_artifact(temp_dir.path(), Backend::TensorRT, kQualityAuto, 4096, 2160);

    REQUIRE(selection.has_value());
    REQUIRE(selection->requested_resolution == 2048);
    REQUIRE(selection->effective_resolution == 1536);
    REQUIRE(selection->used_fallback);
    REQUIRE(selection->executable_model_path.filename() == "corridorkey_fp16_1536.onnx");
}

TEST_CASE("ofx defaults open new instances with source passthrough disabled",
          "[unit][ofx][regression]") {
    REQUIRE(kDefaultSourcePassthroughEnabled == 0);
    REQUIRE(kDefaultEdgeErode == 3);
    REQUIRE(kDefaultEdgeBlur == 7);
}

TEST_CASE("ofx runtime panel fields are read-only dynamic strings", "[unit][ofx][regression]") {
    REQUIRE(std::string_view{kRuntimeStatusStringMode} == kOfxParamStringIsSingleLine);
    REQUIRE(kRuntimeStatusEnabled == 0);
}
