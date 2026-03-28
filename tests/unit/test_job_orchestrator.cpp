#include <catch2/catch_all.hpp>

#include "app/job_orchestrator.hpp"

using namespace corridorkey::app;

TEST_CASE("doctor summary honors packaged bundle inventory for RTX lite",
          "[unit][doctor][regression]") {
    nlohmann::json report;
    report["system"]["capabilities"]["platform"] = "windows";
    report["bundle"]["healthy"] = true;
    report["bundle"]["packaged_layout_detected"] = true;
    report["bundle"]["packaged_models"] = nlohmann::json::array(
        {{{"filename", "corridorkey_fp16_512.onnx"}, {"found", true}},
         {{"filename", "corridorkey_fp16_768.onnx"}, {"found", true}},
         {{"filename", "corridorkey_fp16_1024.onnx"}, {"found", true}},
         {{"filename", "corridorkey_int8_512.onnx"}, {"found", true}},
         {{"filename", "corridorkey_int8_768.onnx"}, {"found", true}},
         {{"filename", "corridorkey_int8_1024.onnx"}, {"found", true}}});
    report["video"]["healthy"] = true;
    report["cache"]["healthy"] = true;
    report["coreml"]["applicable"] = false;
    report["mlx"]["applicable"] = false;
    report["mlx"]["probe_available"] = false;
    report["mlx"]["primary_pack_ready"] = false;
    report["mlx"]["bridge_ready"] = false;
    report["mlx"]["backend_integrated"] = false;
    report["mlx"]["healthy"] = false;
    report["windows_universal"]["applicable"] = true;
    report["windows_universal"]["provider_available"] = true;
    report["windows_universal"]["backend_integrated"] = true;
    report["windows_universal"]["healthy"] = true;
    report["windows_universal"]["recommended_backend"] = "tensorrt";
    report["windows_universal"]["recommended_model"] = "corridorkey_fp16_512.onnx";
    report["models"] = nlohmann::json::array(
        {{{"filename", "corridorkey_fp16_512.onnx"},
          {"packaged_for_windows", true},
          {"found", true},
          {"validated_platforms", nlohmann::json::array()},
          {"packaged_for_macos", false}},
         {{"filename", "corridorkey_fp16_768.onnx"},
          {"packaged_for_windows", true},
          {"found", true},
          {"validated_platforms", nlohmann::json::array()},
          {"packaged_for_macos", false}},
         {{"filename", "corridorkey_fp16_1024.onnx"},
          {"packaged_for_windows", true},
          {"found", true},
          {"validated_platforms", nlohmann::json::array()},
          {"packaged_for_macos", false}},
         {{"filename", "corridorkey_fp16_1536.onnx"},
          {"packaged_for_windows", true},
          {"found", false},
          {"validated_platforms", nlohmann::json::array()},
          {"packaged_for_macos", false}},
         {{"filename", "corridorkey_fp16_2048.onnx"},
          {"packaged_for_windows", true},
          {"found", false},
          {"validated_platforms", nlohmann::json::array()},
          {"packaged_for_macos", false}}});

    auto summary = summarize_doctor_report(report);

    REQUIRE(summary["validated_models_present"].get<bool>());
    REQUIRE(summary["windows_universal_packaged_models_present"].get<bool>());
    REQUIRE(summary["healthy"].get<bool>());
}

TEST_CASE("doctor summary fails when an expected packaged bundle model is missing",
          "[unit][doctor][regression]") {
    nlohmann::json report;
    report["system"]["capabilities"]["platform"] = "windows";
    report["bundle"]["healthy"] = true;
    report["bundle"]["packaged_layout_detected"] = true;
    report["bundle"]["packaged_models"] = nlohmann::json::array(
        {{{"filename", "corridorkey_fp16_512.onnx"}, {"found", true}},
         {{"filename", "corridorkey_fp16_768.onnx"}, {"found", false}}});
    report["video"]["healthy"] = true;
    report["cache"]["healthy"] = true;
    report["coreml"]["applicable"] = false;
    report["mlx"]["applicable"] = false;
    report["windows_universal"]["applicable"] = true;
    report["windows_universal"]["provider_available"] = true;
    report["windows_universal"]["backend_integrated"] = true;
    report["windows_universal"]["healthy"] = true;
    report["windows_universal"]["recommended_backend"] = "tensorrt";
    report["windows_universal"]["recommended_model"] = "corridorkey_fp16_512.onnx";
    report["models"] = nlohmann::json::array();

    auto summary = summarize_doctor_report(report);

    REQUIRE_FALSE(summary["validated_models_present"].get<bool>());
    REQUIRE_FALSE(summary["windows_universal_packaged_models_present"].get<bool>());
    REQUIRE_FALSE(summary["healthy"].get<bool>());
}
