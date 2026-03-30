#include <catch2/catch_all.hpp>

#include "app/job_orchestrator.hpp"

using namespace corridorkey;
using namespace corridorkey::app;

TEST_CASE("doctor summary honors packaged bundle inventory for RTX lite",
          "[unit][doctor][regression]") {
    nlohmann::json report;
    report["system"]["capabilities"]["platform"] = "windows";
    report["bundle"]["healthy"] = true;
    report["bundle"]["packaged_layout_detected"] = true;
    report["bundle"]["packaged_models"] = nlohmann::json::array(
        {{{"filename", "corridorkey_fp16_512.onnx"}, {"found", true}},
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
    report["bundle"]["model_inventory_contract_complete"] = true;
    report["bundle"]["model_profile"] = "rtx-lite";
    report["optimization_profile"]["id"] = "windows-rtx-lite";
    report["models"] = nlohmann::json::array(
        {{{"filename", "corridorkey_fp16_512.onnx"},
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
    REQUIRE(summary["bundle_inventory_contract_healthy"].get<bool>());
    REQUIRE(summary["packaged_profile_matches_active_profile"].get<bool>());
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
         {{"filename", "corridorkey_fp16_1024.onnx"}, {"found", false}}});
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
    report["bundle"]["model_inventory_contract_complete"] = true;
    report["bundle"]["model_profile"] = "rtx-lite";
    report["optimization_profile"]["id"] = "windows-rtx-lite";
    report["models"] = nlohmann::json::array();

    auto summary = summarize_doctor_report(report);

    REQUIRE_FALSE(summary["validated_models_present"].get<bool>());
    REQUIRE_FALSE(summary["windows_universal_packaged_models_present"].get<bool>());
    REQUIRE_FALSE(summary["healthy"].get<bool>());
}

TEST_CASE("doctor summary reports recommended and certified artifact state",
          "[unit][doctor][regression]") {
    nlohmann::json report;
    report["system"]["capabilities"]["platform"] = "windows";
    report["bundle"]["healthy"] = true;
    report["bundle"]["model_inventory_contract_complete"] = true;
    report["bundle"]["model_profile"] = "rtx-full";
    report["video"]["healthy"] = true;
    report["cache"]["healthy"] = true;
    report["coreml"]["applicable"] = false;
    report["mlx"]["applicable"] = false;
    report["windows_universal"]["applicable"] = true;
    report["windows_universal"]["healthy"] = true;
    report["optimization_profile"]["id"] = "windows-rtx-full";
    report["models"] = nlohmann::json::array(
        {{{"filename", "corridorkey_fp16_1024.onnx"},
          {"found", true},
          {"artifact_state",
           {{"certified_for_active_device", true}, {"recommended_for_active_device", true}}}},
         {{"filename", "corridorkey_fp16_1536.onnx"},
          {"found", true},
          {"artifact_state",
           {{"certified_for_active_device", true}, {"recommended_for_active_device", false}}}}});

    auto summary = summarize_doctor_report(report);

    REQUIRE(summary["certified_model_count"].get<std::size_t>() == 2);
    REQUIRE(summary["recommended_model_present"].get<bool>());
    REQUIRE(summary["bundle_inventory_contract_healthy"].get<bool>());
    REQUIRE(summary["packaged_profile_matches_active_profile"].get<bool>());
}

TEST_CASE("doctor summary fails when packaged RTX profile and active profile diverge",
          "[unit][doctor][regression]") {
    nlohmann::json report;
    report["system"]["capabilities"]["platform"] = "windows";
    report["bundle"]["healthy"] = true;
    report["bundle"]["model_inventory_contract_complete"] = true;
    report["bundle"]["model_profile"] = "rtx-full";
    report["video"]["healthy"] = true;
    report["cache"]["healthy"] = true;
    report["coreml"]["applicable"] = false;
    report["mlx"]["applicable"] = false;
    report["windows_universal"]["applicable"] = true;
    report["windows_universal"]["healthy"] = true;
    report["optimization_profile"]["id"] = "windows-rtx-lite";
    report["models"] = nlohmann::json::array();

    auto summary = summarize_doctor_report(report);

    REQUIRE(summary["bundle_inventory_contract_healthy"].get<bool>());
    REQUIRE_FALSE(summary["packaged_profile_matches_active_profile"].get<bool>());
    REQUIRE_FALSE(summary["healthy"].get<bool>());
}

TEST_CASE("benchmark phase timings aggregate raw stage timings", "[unit][runtime][regression]") {
    const std::vector<StageTiming> timings = {
        StageTiming{"engine_create", 10.0, 1, 0},
        StageTiming{"benchmark_warmup_frame", 40.0, 2, 2},
        StageTiming{"sequence_infer_batch", 90.0, 3, 3},
        StageTiming{"sequence_write_output", 25.0, 1, 1},
        StageTiming{"job_total", 165.0, 1, 1},
    };

    auto phase_timings = summarize_stage_groups(timings);
    REQUIRE(phase_timings.is_array());
    REQUIRE(phase_timings.size() == 5);
    REQUIRE(phase_timings[0]["name"] == "prepare");
    REQUIRE(phase_timings[0]["total_ms"] == Catch::Approx(10.0));
    REQUIRE(phase_timings[1]["name"] == "warmup_compile");
    REQUIRE(phase_timings[1]["total_ms"] == Catch::Approx(40.0));
    REQUIRE(phase_timings[2]["name"] == "execute");
    REQUIRE(phase_timings[2]["total_ms"] == Catch::Approx(90.0));
    REQUIRE(phase_timings[3]["name"] == "write_output");
    REQUIRE(phase_timings[3]["total_ms"] == Catch::Approx(25.0));
    REQUIRE(phase_timings[4]["name"] == "total");
    REQUIRE(phase_timings[4]["total_ms"] == Catch::Approx(165.0));
}
