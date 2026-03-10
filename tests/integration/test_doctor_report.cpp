#include <catch2/catch_all.hpp>
#include <filesystem>

#include "app/job_orchestrator.hpp"

using namespace corridorkey::app;

TEST_CASE("doctor report exposes operational health sections", "[integration][doctor]") {
    auto models_dir = std::filesystem::path(PROJECT_ROOT) / "models";
    auto report = JobOrchestrator::run_doctor(models_dir);

    REQUIRE(report.contains("executable"));
    REQUIRE(report.contains("bundle"));
    REQUIRE(report.contains("video"));
    REQUIRE(report.contains("cache"));
    REQUIRE(report.contains("summary"));
    REQUIRE(report["models"].is_array());
    REQUIRE(report["presets"].is_array());

    REQUIRE(report["bundle"].contains("healthy"));
    REQUIRE(report["bundle"].contains("signature"));
    REQUIRE(report["video"]["supported_encoders"].is_array());
    REQUIRE(report["video"].contains("portable_h264_available"));
    REQUIRE(report["cache"].contains("writable"));
    REQUIRE(report["summary"].contains("validated_models_present"));
}
