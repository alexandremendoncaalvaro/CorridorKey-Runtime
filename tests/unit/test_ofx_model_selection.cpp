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

RuntimeCapabilities windows_universal_capabilities() {
    RuntimeCapabilities capabilities;
    capabilities.platform = "windows";
    capabilities.supported_backends = {Backend::DirectML, Backend::WindowsML, Backend::CPU};
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

    auto selection = select_quality_artifact(temp_dir.path(), Backend::MLX, kQualityMaximum, 3840,
                                             2160, kQuantizationFp16);

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

    auto selection = select_quality_artifact(temp_dir.path(), Backend::MLX, kQualityMaximum, 3840,
                                             2160, kQuantizationFp16);

    REQUIRE_FALSE(selection.has_value());
}

TEST_CASE("auto mlx quality may fall back to the highest available lower bridge",
          "[unit][ofx][regression]") {
    TempDirGuard temp_dir("corridorkey-ofx-quality-auto");
    touch_file(temp_dir.path() / "corridorkey_mlx_bridge_512.mlxfn");
    touch_file(temp_dir.path() / "corridorkey_mlx_bridge_1024.mlxfn");
    touch_file(temp_dir.path() / "corridorkey_mlx_bridge_1536.mlxfn");

    auto selection = select_quality_artifact(temp_dir.path(), Backend::MLX, kQualityAuto, 4096,
                                             2160, kQuantizationFp16);

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

    auto selection = select_quality_artifact(temp_dir.path(), Backend::TensorRT, kQualityMaximum,
                                             4096, 2160, kQuantizationFp16);

    REQUIRE_FALSE(selection.has_value());
}

TEST_CASE("fixed windows tensorRT preview resolves the exact 512 model when packaged",
          "[unit][ofx][regression]") {
    TempDirGuard temp_dir("corridorkey-ofx-windows-quality-preview");
    touch_file(temp_dir.path() / "corridorkey_fp16_512.onnx");
    touch_file(temp_dir.path() / "corridorkey_fp16_768.onnx");

    auto selection = select_quality_artifact(temp_dir.path(), Backend::TensorRT, kQualityPreview,
                                             1920, 1080, kQuantizationFp16);

    REQUIRE(selection.has_value());
    REQUIRE(selection->requested_resolution == 512);
    REQUIRE(selection->effective_resolution == 512);
    REQUIRE_FALSE(selection->used_fallback);
    REQUIRE(selection->executable_model_path.filename() == "corridorkey_fp16_512.onnx");
}

TEST_CASE("ofx bootstrap honors fixed preview quality on windows tensorRT",
          "[unit][ofx][regression]") {
    TempDirGuard temp_dir("corridorkey-ofx-bootstrap-preview");
    touch_file(temp_dir.path() / "corridorkey_fp16_512.onnx");
    touch_file(temp_dir.path() / "corridorkey_fp16_1024.onnx");
    touch_file(temp_dir.path() / "corridorkey_int8_512.onnx");

    auto candidates = build_bootstrap_candidates(
        windows_capabilities(), DeviceInfo{"NVIDIA GeForce RTX 3080", 10240, Backend::TensorRT},
        temp_dir.path(), kQualityPreview);

    REQUIRE_FALSE(candidates.empty());
    REQUIRE(candidates.front().device.backend == Backend::TensorRT);
    REQUIRE(candidates.front().requested_model_path.filename() == "corridorkey_fp16_512.onnx");
    REQUIRE(candidates.front().executable_model_path.filename() == "corridorkey_fp16_512.onnx");
    REQUIRE(candidates.front().requested_resolution == 512);
    REQUIRE(candidates.front().effective_resolution == 512);
}

TEST_CASE("fixed windows tensorRT ultra and maximum resolve exact packaged models",
          "[unit][ofx][regression]") {
    TempDirGuard temp_dir("corridorkey-ofx-windows-quality-exact-high-end");
    touch_file(temp_dir.path() / "corridorkey_fp16_1024.onnx");
    touch_file(temp_dir.path() / "corridorkey_fp16_1536.onnx");
    touch_file(temp_dir.path() / "corridorkey_fp16_2048.onnx");

    auto ultra = select_quality_artifact(temp_dir.path(), Backend::TensorRT, kQualityUltra, 2560,
                                         1440, kQuantizationFp16);
    REQUIRE(ultra.has_value());
    REQUIRE(ultra->requested_resolution == 1536);
    REQUIRE(ultra->effective_resolution == 1536);
    REQUIRE_FALSE(ultra->used_fallback);
    REQUIRE(ultra->executable_model_path.filename() == "corridorkey_fp16_1536.onnx");

    auto maximum = select_quality_artifact(temp_dir.path(), Backend::TensorRT, kQualityMaximum,
                                           4096, 2160, kQuantizationFp16);
    REQUIRE(maximum.has_value());
    REQUIRE(maximum->requested_resolution == 2048);
    REQUIRE(maximum->effective_resolution == 2048);
    REQUIRE_FALSE(maximum->used_fallback);
    REQUIRE(maximum->executable_model_path.filename() == "corridorkey_fp16_2048.onnx");
}

TEST_CASE("ofx quality mode labels expose fixed resolutions in the UI", "[unit][ofx][regression]") {
    REQUIRE(std::string(quality_mode_ui_label(kQualityAuto)) == "Recommended");
    REQUIRE(std::string(quality_mode_ui_label(kQualityPreview)) == "Draft (512)");
    REQUIRE(std::string(quality_mode_ui_label(kQualityHigh)) == "High (1024)");
    REQUIRE(std::string(quality_mode_ui_label(kQualityUltra)) == "Ultra (1536)");
    REQUIRE(std::string(quality_mode_ui_label(kQualityMaximum)) == "Maximum (2048)");
}

TEST_CASE("quality fallback warning clears when selection matches the requested resolution",
          "[unit][ofx][regression]") {
    QualityArtifactSelection exact_selection{};
    exact_selection.requested_resolution = 1024;
    exact_selection.effective_resolution = 1024;
    exact_selection.used_fallback = false;

    QualityArtifactSelection fallback_selection{};
    fallback_selection.requested_resolution = 1536;
    fallback_selection.effective_resolution = 1024;
    fallback_selection.used_fallback = true;

    REQUIRE(quality_fallback_warning(kQualityHigh, exact_selection).empty());
    REQUIRE(quality_fallback_warning(kQualityUltra, fallback_selection) ==
            "Ultra (1536) (1536px) unavailable on this hardware -- using 1024px");
}

TEST_CASE("automatic coarse-to-fine selection chooses a safer coarse artifact",
          "[unit][ofx][regression]") {
    TempDirGuard temp_dir("corridorkey-ofx-quality-coarse-to-fine");
    touch_file(temp_dir.path() / "corridorkey_fp16_1024.onnx");

    auto selection =
        select_quality_artifact(temp_dir.path(), Backend::TensorRT, kQualityMaximum, 4096, 2160,
                                kQuantizationFp16, 10240, QualityFallbackMode::Auto);

    REQUIRE(selection.has_value());
    REQUIRE(selection->requested_resolution == 2048);
    REQUIRE(selection->effective_resolution == 1024);
    REQUIRE(selection->used_fallback);
    REQUIRE(selection->coarse_to_fine);
}

TEST_CASE("automatic coarse-to-fine selection falls back to lower packaged coarse artifacts",
          "[unit][ofx][regression]") {
    TempDirGuard temp_dir("corridorkey-ofx-quality-coarse-to-fine-lower-packaged");
    touch_file(temp_dir.path() / "corridorkey_fp16_768.onnx");
    touch_file(temp_dir.path() / "corridorkey_fp16_512.onnx");

    auto selection =
        select_quality_artifact(temp_dir.path(), Backend::TensorRT, kQualityMaximum, 4096, 2160,
                                kQuantizationFp16, 10240, QualityFallbackMode::Auto);

    REQUIRE(selection.has_value());
    REQUIRE(selection->requested_resolution == 2048);
    REQUIRE(selection->effective_resolution == 512);
    REQUIRE(selection->used_fallback);
    REQUIRE(selection->coarse_to_fine);
    REQUIRE(selection->executable_model_path.filename() == "corridorkey_fp16_512.onnx");
}

TEST_CASE("fixed windows tensorRT manual override may attempt 1536 on a 10 GB RTX tier",
          "[unit][ofx][regression]") {
    TempDirGuard temp_dir("corridorkey-ofx-quality-ultra-10gb-direct");
    touch_file(temp_dir.path() / "corridorkey_fp16_1536.onnx");

    auto selection =
        select_quality_artifact(temp_dir.path(), Backend::TensorRT, kQualityUltra, 3200, 1800,
                                kQuantizationFp16, 10240, QualityFallbackMode::Auto, 0, true);

    REQUIRE(selection.has_value());
    REQUIRE(selection->requested_resolution == 1536);
    REQUIRE(selection->effective_resolution == 1536);
    REQUIRE_FALSE(selection->used_fallback);
    REQUIRE_FALSE(selection->coarse_to_fine);
    REQUIRE(selection->executable_model_path.filename() == "corridorkey_fp16_1536.onnx");
}

TEST_CASE("coarse-to-fine override requires the exact requested coarse artifact",
          "[unit][ofx][regression]") {
    TempDirGuard temp_dir("corridorkey-ofx-quality-coarse-to-fine-exact-override");
    touch_file(temp_dir.path() / "corridorkey_fp16_768.onnx");

    auto selection =
        select_quality_artifact(temp_dir.path(), Backend::TensorRT, kQualityUltra, 3200, 1800,
                                kQuantizationFp16, 10240, QualityFallbackMode::Auto, 1024);

    REQUIRE_FALSE(selection.has_value());
}

TEST_CASE("coarse-to-fine warning explains the coarse artifact path", "[unit][ofx][regression]") {
    QualityArtifactSelection selection{};
    selection.requested_resolution = 1536;
    selection.effective_resolution = 1024;
    selection.used_fallback = true;
    selection.coarse_to_fine = true;

    REQUIRE(quality_fallback_warning(kQualityUltra, selection) ==
            "Ultra (1536) (1536px) will run coarse-to-fine using the 1024px packaged artifact");
}

TEST_CASE("fixed windows tensorRT quality keeps lower packaged fallbacks after the exact model",
          "[unit][ofx][regression]") {
    TempDirGuard temp_dir("corridorkey-ofx-windows-quality-fixed-fallbacks");
    touch_file(temp_dir.path() / "corridorkey_fp16_1024.onnx");
    touch_file(temp_dir.path() / "corridorkey_fp16_1536.onnx");
    touch_file(temp_dir.path() / "corridorkey_fp16_2048.onnx");

    auto candidates = quality_artifact_candidates(temp_dir.path(), Backend::TensorRT,
                                                  kQualityMaximum, 4096, 2160, kQuantizationFp16);

    REQUIRE(candidates.size() == 3);
    REQUIRE(candidates[0].requested_resolution == 2048);
    REQUIRE(candidates[0].effective_resolution == 2048);
    REQUIRE_FALSE(candidates[0].used_fallback);
    REQUIRE(candidates[0].executable_model_path.filename() == "corridorkey_fp16_2048.onnx");
    REQUIRE(candidates[1].effective_resolution == 1536);
    REQUIRE(candidates[1].used_fallback);
    REQUIRE(candidates[1].executable_model_path.filename() == "corridorkey_fp16_1536.onnx");
    REQUIRE(candidates[2].effective_resolution == 1024);
    REQUIRE(candidates[2].used_fallback);
    REQUIRE(candidates[2].executable_model_path.filename() == "corridorkey_fp16_1024.onnx");
}

TEST_CASE("fixed TensorRT compile failures block exact retries and lower fallback",
          "[unit][ofx][regression]") {
    const QualityCompileFailureCacheContext context{
        .models_root = "C:/models",
        .models_bundle_token = 11,
        .backend = Backend::TensorRT,
        .device_index = 2,
        .available_memory_mb = 24576,
        .quantization_mode = kQuantizationFp16,
    };

    std::vector<QualityArtifactSelection> candidates{
        {std::filesystem::path("corridorkey_fp16_2048.onnx"), 2048, 2048, false},
        {std::filesystem::path("corridorkey_fp16_1536.onnx"), 2048, 1536, true},
        {std::filesystem::path("corridorkey_fp16_1024.onnx"), 2048, 1024, true},
    };

    QualityCompileFailureCache cache;
    record_quality_compile_failure(
        cache, context, candidates.front(),
        "Failed to create engine for Maximum (2048) using corridorkey_fp16_2048.onnx: compile "
        "failed");

    auto cached = cached_quality_compile_failure(cache, context, candidates.front());
    REQUIRE(cached.has_value());
    REQUIRE(cached->error_message.find("2048") != std::string::npos);
    REQUIRE(should_abort_quality_fallback_after_compile_failure(Backend::TensorRT, kQualityMaximum,
                                                                false, candidates.front()));

    auto filtered = filter_quality_artifacts_with_compile_cache(candidates, cache, context);
    REQUIRE(filtered.size() == 2);
    REQUIRE(filtered.front().effective_resolution == 1536);
    REQUIRE(filtered[1].effective_resolution == 1024);
}

TEST_CASE("fixed TensorRT abort predicate only trips on the exact requested artifact",
          "[unit][ofx][regression]") {
    QualityArtifactSelection exact_selection{std::filesystem::path("corridorkey_fp16_2048.onnx"),
                                             2048, 2048, false};
    QualityArtifactSelection fallback_selection{std::filesystem::path("corridorkey_fp16_1536.onnx"),
                                                2048, 1536, true};

    REQUIRE(should_abort_quality_fallback_after_compile_failure(Backend::TensorRT, kQualityMaximum,
                                                                false, exact_selection));
    REQUIRE_FALSE(should_abort_quality_fallback_after_compile_failure(
        Backend::TensorRT, kQualityMaximum, false, fallback_selection));
    REQUIRE_FALSE(should_abort_quality_fallback_after_compile_failure(
        Backend::TensorRT, kQualityAuto, false, exact_selection));
}

TEST_CASE("auto TensorRT quality skips cached compile failures and keeps working fallback",
          "[unit][ofx][regression]") {
    const QualityCompileFailureCacheContext context{
        .models_root = "C:/models",
        .models_bundle_token = 12,
        .backend = Backend::TensorRT,
        .device_index = 0,
        .available_memory_mb = 16384,
        .quantization_mode = kQuantizationFp16,
    };

    std::vector<QualityArtifactSelection> candidates{
        {std::filesystem::path("corridorkey_fp16_2048.onnx"), 2048, 2048, false},
        {std::filesystem::path("corridorkey_fp16_1536.onnx"), 2048, 1536, true},
        {std::filesystem::path("corridorkey_fp16_1024.onnx"), 2048, 1024, true},
    };

    QualityCompileFailureCache cache;
    record_quality_compile_failure(cache, context, candidates[0], "2048 compile failed");
    record_quality_compile_failure(cache, context, candidates[1], "1536 compile failed");

    auto filtered = filter_quality_artifacts_with_compile_cache(candidates, cache, context);
    REQUIRE(filtered.size() == 1);
    REQUIRE(filtered.front().effective_resolution == 1024);
    REQUIRE(filtered.front().used_fallback);
}

TEST_CASE("quality compile failure cache invalidates when backend device or model bundle changes",
          "[unit][ofx][regression]") {
    const QualityCompileFailureCacheContext initial_context{
        .models_root = "C:/models",
        .models_bundle_token = 21,
        .backend = Backend::TensorRT,
        .device_index = 1,
        .available_memory_mb = 16384,
        .quantization_mode = kQuantizationFp16,
    };

    QualityCompileFailureCache cache;
    record_quality_compile_failure(
        cache, initial_context,
        QualityArtifactSelection{std::filesystem::path("corridorkey_fp16_2048.onnx"), 2048, 2048,
                                 false},
        "compile failed");
    REQUIRE(cache.entries.size() == 1);

    QualityCompileFailureCacheContext changed_context = initial_context;
    changed_context.device_index = 3;
    prepare_quality_compile_failure_cache(cache, changed_context);
    REQUIRE(cache.entries.empty());

    record_quality_compile_failure(
        cache, changed_context,
        QualityArtifactSelection{std::filesystem::path("corridorkey_fp16_1536.onnx"), 1536, 1536,
                                 false},
        "compile failed");
    REQUIRE(cache.entries.size() == 1);

    changed_context.models_bundle_token = 22;
    prepare_quality_compile_failure_cache(cache, changed_context);
    REQUIRE(cache.entries.empty());
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
    REQUIRE(candidates.front().requested_model_path.filename() == "corridorkey_fp16_1024.onnx");
    REQUIRE(candidates.front().executable_model_path.filename() == "corridorkey_fp16_1024.onnx");

    auto selection = select_quality_artifact(temp_dir.path(), Backend::TensorRT, kQualityAuto, 4096,
                                             2160, kQuantizationFp16);

    REQUIRE(selection.has_value());
    REQUIRE(selection->requested_resolution == 2048);
    REQUIRE(selection->effective_resolution == 1536);
    REQUIRE(selection->used_fallback);
    REQUIRE(selection->executable_model_path.filename() == "corridorkey_fp16_1536.onnx");
}

TEST_CASE("auto windows tensorRT resolves small inputs to the 512 rung",
          "[unit][ofx][regression]") {
    TempDirGuard temp_dir("corridorkey-ofx-windows-quality-auto-small-input");
    touch_file(temp_dir.path() / "corridorkey_fp16_512.onnx");
    touch_file(temp_dir.path() / "corridorkey_fp16_1024.onnx");
    touch_file(temp_dir.path() / "corridorkey_fp16_1536.onnx");

    auto selection = select_quality_artifact(temp_dir.path(), Backend::TensorRT, kQualityAuto, 960,
                                             540, kQuantizationFp16, 10240);

    REQUIRE(selection.has_value());
    REQUIRE(selection->requested_resolution == 512);
    REQUIRE(selection->effective_resolution == 512);
    REQUIRE_FALSE(selection->used_fallback);
    REQUIRE(selection->executable_model_path.filename() == "corridorkey_fp16_512.onnx");
}

TEST_CASE("auto windows tensorRT quality respects the device VRAM ceiling",
          "[unit][ofx][regression]") {
    TempDirGuard temp_dir("corridorkey-ofx-windows-quality-auto-vram-guardrail");
    touch_file(temp_dir.path() / "corridorkey_fp16_1024.onnx");
    touch_file(temp_dir.path() / "corridorkey_fp16_1536.onnx");
    touch_file(temp_dir.path() / "corridorkey_fp16_2048.onnx");

    auto selection = select_quality_artifact(temp_dir.path(), Backend::TensorRT, kQualityAuto, 4096,
                                             2160, kQuantizationFp16, 10240);

    REQUIRE(selection.has_value());
    REQUIRE(selection->requested_resolution == 2048);
    REQUIRE(selection->effective_resolution == 1024);
    REQUIRE(selection->used_fallback);
    REQUIRE(selection->executable_model_path.filename() == "corridorkey_fp16_1024.onnx");
}

TEST_CASE("auto windows tensorRT quality keeps direct 2048 only for fully supported tiers",
          "[unit][ofx][regression]") {
    TempDirGuard temp_dir("corridorkey-ofx-windows-quality-auto-strong-gpu");
    touch_file(temp_dir.path() / "corridorkey_fp16_1024.onnx");
    touch_file(temp_dir.path() / "corridorkey_fp16_1536.onnx");
    touch_file(temp_dir.path() / "corridorkey_fp16_2048.onnx");

    auto selection_16gb = select_quality_artifact(temp_dir.path(), Backend::TensorRT, kQualityAuto,
                                                  3200, 1800, kQuantizationFp16, 16384);
    REQUIRE(selection_16gb.has_value());
    REQUIRE(selection_16gb->requested_resolution == 2048);
    REQUIRE(selection_16gb->effective_resolution == 1024);
    REQUIRE(selection_16gb->used_fallback);
    REQUIRE(selection_16gb->coarse_to_fine);
    REQUIRE(selection_16gb->executable_model_path.filename() == "corridorkey_fp16_1024.onnx");

    auto selection_24gb = select_quality_artifact(temp_dir.path(), Backend::TensorRT, kQualityAuto,
                                                  4096, 2160, kQuantizationFp16, 24576);
    REQUIRE(selection_24gb.has_value());
    REQUIRE(selection_24gb->requested_resolution == 2048);
    REQUIRE(selection_24gb->effective_resolution == 2048);
    REQUIRE_FALSE(selection_24gb->used_fallback);
    REQUIRE_FALSE(selection_24gb->coarse_to_fine);
    REQUIRE(selection_24gb->executable_model_path.filename() == "corridorkey_fp16_2048.onnx");
}

TEST_CASE("fixed windows tensorRT quality reports unsupported tiers before engine creation",
          "[unit][ofx][regression]") {
    auto removed_rung_message = unsupported_quality_message(
        DeviceInfo{"RTX 4090", 24576, Backend::TensorRT}, kQualityHigh, 768);
    REQUIRE(removed_rung_message.has_value());
    REQUIRE(removed_rung_message->find("768px") != std::string::npos);
    REQUIRE(removed_rung_message->find("High (1024)") != std::string::npos);

    auto message = unsupported_quality_message(DeviceInfo{"RTX 3080", 10240, Backend::TensorRT},
                                               kQualityMaximum, 2048);

    REQUIRE(message.has_value());
    REQUIRE(message->find("24 GB") != std::string::npos);
    REQUIRE(message->find("High (1024)") != std::string::npos);
    REQUIRE(unsupported_quality_message(DeviceInfo{"RTX 3080", 10240, Backend::TensorRT},
                                        kQualityUltra, 1536)
                .has_value());
    REQUIRE_FALSE(unsupported_quality_message(DeviceInfo{"RTX 3080", 10240, Backend::TensorRT},
                                              kQualityUltra, 1536, true)
                      .has_value());
    REQUIRE_FALSE(unsupported_quality_message(DeviceInfo{"RTX 4090", 24576, Backend::TensorRT},
                                              kQualityMaximum, 2048)
                      .has_value());
}

TEST_CASE("missing quality artifact message names the expected model and folder",
          "[unit][ofx][regression]") {
    TempDirGuard temp_dir("corridorkey-ofx-missing-quality-message");

    auto message =
        missing_quality_artifact_message(temp_dir.path(), Backend::TensorRT, kQualityUltra, 2560,
                                         1440, kQuantizationFp16, false, 16384);

    REQUIRE(message.find("Ultra (1536)") != std::string::npos);
    REQUIRE(message.find("corridorkey_fp16_1536.onnx") != std::string::npos);
    REQUIRE(message.find(temp_dir.path().string()) != std::string::npos);
}

TEST_CASE("missing bootstrap artifact message lists the expected bootstrap files",
          "[unit][ofx][regression]") {
    TempDirGuard temp_dir("corridorkey-ofx-missing-bootstrap-message");

    auto message = missing_bootstrap_artifact_message(
        windows_capabilities(), DeviceInfo{"RTX 3080", 10240, Backend::TensorRT}, temp_dir.path());

    REQUIRE(message.find("corridorkey_fp16_1024.onnx") != std::string::npos);
    REQUIRE(message.find("corridorkey_int8_512.onnx") != std::string::npos);
    REQUIRE(message.find(temp_dir.path().string()) != std::string::npos);
}
TEST_CASE("auto windows tensorRT ignores the deprecated 768 fp16 artifact",
          "[unit][ofx][regression]") {
    TempDirGuard temp_dir("corridorkey-ofx-windows-quality-auto-prefers-fp16");
    touch_file(temp_dir.path() / "corridorkey_fp16_768.onnx");
    touch_file(temp_dir.path() / "corridorkey_int8_768.onnx");

    auto selection = select_quality_artifact(temp_dir.path(), Backend::TensorRT, kQualityAuto, 1920,
                                             1080, kQuantizationFp16);

    REQUIRE_FALSE(selection.has_value());
}

TEST_CASE("windows universal bootstrap aligns int8 artifact selection with device memory",
          "[unit][ofx][regression]") {
    TempDirGuard temp_dir("corridorkey-ofx-windows-universal-bootstrap");
    touch_file(temp_dir.path() / "corridorkey_int8_512.onnx");
    touch_file(temp_dir.path() / "corridorkey_int8_768.onnx");
    touch_file(temp_dir.path() / "corridorkey_int8_1024.onnx");

    auto candidates = build_bootstrap_candidates(windows_universal_capabilities(),
                                                 DeviceInfo{"AMD Radeon", 16384, Backend::DirectML},
                                                 temp_dir.path());

    REQUIRE_FALSE(candidates.empty());
    REQUIRE(candidates.front().device.backend == Backend::DirectML);
    REQUIRE(candidates.front().requested_model_path.filename() == "corridorkey_int8_1024.onnx");
    REQUIRE(candidates.front().executable_model_path.filename() == "corridorkey_int8_1024.onnx");
    REQUIRE(candidates.front().requested_resolution == 1024);
    REQUIRE(candidates.front().effective_resolution == 1024);
}

TEST_CASE("auto windows universal quality falls back to the highest packaged int8 model",
          "[unit][ofx][regression]") {
    TempDirGuard temp_dir("corridorkey-ofx-windows-universal-quality-auto");
    touch_file(temp_dir.path() / "corridorkey_int8_512.onnx");
    touch_file(temp_dir.path() / "corridorkey_int8_768.onnx");

    auto selection = select_quality_artifact(temp_dir.path(), Backend::DirectML, kQualityAuto, 4096,
                                             2160, kQuantizationFp16);

    REQUIRE(selection.has_value());
    REQUIRE(selection->requested_resolution == 2048);
    REQUIRE(selection->effective_resolution == 512);
    REQUIRE(selection->used_fallback);
    REQUIRE(selection->executable_model_path.filename() == "corridorkey_int8_512.onnx");
}

TEST_CASE("unsupported OFX quantization combinations report product-safe guidance",
          "[unit][ofx][regression]") {
    auto tensorrt_message = unsupported_quantization_message(Backend::TensorRT, kQuantizationInt8);
    REQUIRE(tensorrt_message.has_value());
    REQUIRE(tensorrt_message->find("Windows RTX track") != std::string::npos);
    REQUIRE(tensorrt_message->find("Allow CPU Fallback") != std::string::npos);

    auto directml_message = unsupported_quantization_message(Backend::DirectML, kQuantizationInt8);
    REQUIRE(directml_message.has_value());
    REQUIRE(directml_message->find("selected Windows GPU backend") != std::string::npos);
    REQUIRE(directml_message->find("FP16") != std::string::npos);

    REQUIRE_FALSE(
        unsupported_quantization_message(Backend::TensorRT, kQuantizationInt8, true).has_value());
    REQUIRE_FALSE(
        unsupported_quantization_message(Backend::DirectML, kQuantizationInt8, true).has_value());
    REQUIRE_FALSE(
        unsupported_quantization_message(Backend::DirectML, kQuantizationFp16).has_value());
    REQUIRE_FALSE(unsupported_quantization_message(Backend::CPU, kQuantizationInt8).has_value());
}

TEST_CASE("cpu quality guardrail clamps manual qualities above preview",
          "[unit][ofx][regression]") {
    REQUIRE(clamp_quality_mode_for_cpu_backend(Backend::CPU, kQualityPreview) == kQualityPreview);
    REQUIRE(clamp_quality_mode_for_cpu_backend(Backend::CPU, kQualityMaximum) == kQualityPreview);
    REQUIRE(clamp_quality_mode_for_cpu_backend(Backend::TensorRT, kQualityMaximum) ==
            kQualityMaximum);
}

TEST_CASE("out-of-process ofx instances defer bootstrap until first render",
          "[unit][ofx][regression]") {
    REQUIRE(should_prepare_bootstrap_during_instance_create(false));
    REQUIRE_FALSE(should_prepare_bootstrap_during_instance_create(true));
}

TEST_CASE("unloaded quality state only resolves fixed manual resolutions",
          "[unit][ofx][regression]") {
    REQUIRE(initial_requested_resolution_for_quality_mode(kQualityAuto) == 0);
    REQUIRE(initial_requested_resolution_for_quality_mode(kQualityPreview) == 512);
    REQUIRE(initial_requested_resolution_for_quality_mode(kQualityMaximum) == 2048);
}

TEST_CASE("ofx defaults open new instances with source passthrough enabled",
          "[unit][ofx][regression]") {
    REQUIRE(kDefaultSourcePassthroughEnabled == 1);
    REQUIRE(kDefaultEdgeErode == 3);
    REQUIRE(kDefaultEdgeBlur == 7);
    REQUIRE(kMaxEdgeErode == 100);
    REQUIRE(kMaxEdgeBlur == 100);
    REQUIRE(kDefaultInputColorSpace == kInputColorAutoHostManaged);
}

TEST_CASE("ofx runtime panel fields are read-only dynamic strings", "[unit][ofx][regression]") {
    REQUIRE(std::string_view{kRuntimeStatusStringMode} == kOfxParamStringIsSingleLine);
    REQUIRE(kRuntimeStatusEnabled == 0);
}
