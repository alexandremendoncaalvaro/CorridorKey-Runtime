#include <catch2/catch_all.hpp>
#include <filesystem>
#include <fstream>
#include <limits>
#include <vector>

#include "core/inference_output_validation.hpp"
#include "core/inference_session.hpp"
#include "core/inference_session_metadata.hpp"

using namespace corridorkey;

TEST_CASE("InferenceSession::create validation", "[unit][inference]") {
    DeviceInfo cpu_device = {"CPU", 0, Backend::CPU};

    SECTION("Fails when model file does not exist") {
        auto result = InferenceSession::create("non_existent_model.onnx", cpu_device);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(result.error().code == ErrorCode::ModelLoadFailed);
    }

    SECTION("Fails when model file is a Git LFS placeholder") {
        const auto temp_dir =
            std::filesystem::temp_directory_path() / "corridorkey-lfs-placeholder";
        std::filesystem::create_directories(temp_dir);
        const auto placeholder_path = temp_dir / "placeholder.onnx";

        {
            std::ofstream stream(placeholder_path, std::ios::binary | std::ios::trunc);
            REQUIRE(stream.is_open());
            stream << "version https://git-lfs.github.com/spec/v1\n"
                      "oid sha256:1234567890abcdef\n"
                      "size 42\n";
        }

        auto result = InferenceSession::create(placeholder_path, cpu_device);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(result.error().code == ErrorCode::ModelLoadFailed);
        REQUIRE(result.error().message.find("Git LFS pointer placeholder") != std::string::npos);

        std::filesystem::remove_all(temp_dir);
    }

    // Note: We cannot easily test successful load without a real (even tiny) ONNX file.
    // That would be an integration test.
}

TEST_CASE("Packaged CorridorKey output contract selection", "[unit][inference]") {
    SECTION("Uses the packaged contract for Windows TensorRT fp16 artifacts") {
#if defined(_WIN32)
        REQUIRE(core::should_use_packaged_corridorkey_output_contract(
            "models/corridorkey_fp16_1536.onnx", Backend::TensorRT, true));
        REQUIRE(core::should_use_packaged_corridorkey_output_contract(
            "models/corridorkey_fp16_1536_ctx.onnx", Backend::TensorRT, true));
        REQUIRE(core::should_use_packaged_corridorkey_output_contract(
            "models/corridorkey_fp16_2048.onnx", Backend::TensorRT, true));
#else
        REQUIRE_FALSE(core::should_use_packaged_corridorkey_output_contract(
            "models/corridorkey_fp16_1536.onnx", Backend::TensorRT, true));
#endif
    }

    SECTION("Does not use the packaged contract for non-targeted cases") {
        REQUIRE_FALSE(core::should_use_packaged_corridorkey_output_contract(
            "models/corridorkey_fp16_1536.onnx", Backend::CPU, true));
        REQUIRE_FALSE(core::should_use_packaged_corridorkey_output_contract(
            "models/corridorkey_fp16_1024.onnx", Backend::TensorRT, true));
        REQUIRE_FALSE(core::should_use_packaged_corridorkey_output_contract(
            "models/corridorkey_fp32_1536.onnx", Backend::TensorRT, true));
        REQUIRE_FALSE(core::should_use_packaged_corridorkey_output_contract(
            "models/custom_model.onnx", Backend::TensorRT, true));
        REQUIRE_FALSE(core::should_use_packaged_corridorkey_output_contract(
            "models/corridorkey_fp16_1536.onnx", Backend::TensorRT, false));
    }
}

TEST_CASE("I/O binding policy parsing and eligibility", "[unit][inference][regression]") {
    REQUIRE(core::parse_io_binding_mode("auto") == std::optional(core::IoBindingMode::Auto));
    REQUIRE(core::parse_io_binding_mode("ON") == std::optional(core::IoBindingMode::On));
    REQUIRE(core::parse_io_binding_mode("0") == std::optional(core::IoBindingMode::Off));
    REQUIRE_FALSE(core::parse_io_binding_mode("maybe").has_value());

#if defined(_WIN32)
    REQUIRE(core::supports_windows_rtx_io_binding("models/corridorkey_fp16_1536.onnx",
                                                  Backend::TensorRT));
    REQUIRE(core::should_enable_io_binding("models/corridorkey_fp16_1536.onnx",
                                           Backend::TensorRT, core::IoBindingMode::Auto));
    REQUIRE_FALSE(core::should_enable_io_binding("models/corridorkey_fp16_1536.onnx",
                                                 Backend::TensorRT, core::IoBindingMode::Off));
#else
    REQUIRE_FALSE(core::supports_windows_rtx_io_binding("models/corridorkey_fp16_1536.onnx",
                                                        Backend::TensorRT));
#endif

    REQUIRE_FALSE(core::supports_windows_rtx_io_binding("models/corridorkey_int8_512.onnx",
                                                        Backend::TensorRT));
    REQUIRE_FALSE(core::supports_windows_rtx_io_binding("models/corridorkey_fp16_1536.onnx",
                                                        Backend::CPU));
}

TEST_CASE("Model resolution inference from input shape", "[unit][inference][regression]") {
    REQUIRE(core::infer_model_resolution({1, 4, 1536, 1536}) == std::optional<int>(1536));
    REQUIRE(core::infer_model_resolution({-1, 4, 1024, 1024}) == std::optional<int>(1024));
    REQUIRE_FALSE(core::infer_model_resolution({1, 4, 2048, 1024}).has_value());
    REQUIRE_FALSE(core::infer_model_resolution({1, 4, -1, -1}).has_value());
    REQUIRE_FALSE(core::infer_model_resolution({1, 4, 2048}).has_value());
}

TEST_CASE("Model resolution inference falls back to artifact filename",
          "[unit][inference][regression]") {
    REQUIRE(core::infer_model_resolution_from_path("corridorkey_fp16_1536.onnx") ==
            std::optional<int>(1536));
    REQUIRE(core::infer_model_resolution_from_path("corridorkey_fp16_1536_ctx.onnx") ==
            std::optional<int>(1536));
    REQUIRE(core::infer_model_resolution_from_path("corridorkey_fp16_1024.onnx") ==
            std::optional<int>(1024));
    REQUIRE_FALSE(core::infer_model_resolution_from_path("corridorkey_fp16.onnx").has_value());
}

TEST_CASE("Output validation rejects non-finite model output", "[unit][inference][regression]") {
    SECTION("Finite values pass with correct stats") {
        const std::vector<float> values = {0.0F, 0.5F, 1.0F};
        const auto stats = core::compute_numeric_stats(values);
        const auto analysis = core::analyze_finite_values(values, "alpha_raw_output");

        REQUIRE(stats.total_count == 3);
        REQUIRE(stats.finite_count == 3);
        REQUIRE(stats.min_value == Catch::Approx(0.0F));
        REQUIRE(stats.max_value == Catch::Approx(1.0F));
        REQUIRE(stats.mean_value == Catch::Approx(0.5));
        REQUIRE(analysis.has_value());
        REQUIRE(core::all_values_finite(*analysis));
        REQUIRE(analysis->mean_value == Catch::Approx(0.5));

        auto validation = core::validate_finite_values(values, "alpha_raw_output");
        REQUIRE(validation.has_value());
    }

    SECTION("NaN and infinity fail with diagnostic stats") {
        const std::vector<float> values = {
            0.25F,
            std::numeric_limits<float>::quiet_NaN(),
            std::numeric_limits<float>::infinity(),
        };
        const auto stats = core::compute_numeric_stats(values);
        const auto analysis = core::analyze_finite_values(values, "alpha_raw_output");

        REQUIRE(stats.total_count == 3);
        REQUIRE(stats.finite_count == 1);
        REQUIRE(stats.min_value == Catch::Approx(0.25F));
        REQUIRE(stats.max_value == Catch::Approx(0.25F));
        REQUIRE(stats.mean_value == Catch::Approx(0.25));
        REQUIRE_FALSE(analysis.has_value());
        REQUIRE(analysis.error().message.find("finite=1") != std::string::npos);

        auto validation = core::validate_finite_values(values, "alpha_raw_output");
        REQUIRE_FALSE(validation.has_value());
        REQUIRE(validation.error().code == ErrorCode::InferenceFailed);
        REQUIRE(validation.error().message.find("alpha_raw_output") != std::string::npos);
        REQUIRE(validation.error().message.find("finite=1") != std::string::npos);
    }
}
