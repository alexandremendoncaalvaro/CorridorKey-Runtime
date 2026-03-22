#include <catch2/catch_all.hpp>
#include <corridorkey/frame_io.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include "app/job_orchestrator.hpp"

using namespace corridorkey;
using namespace corridorkey::app;

namespace {
std::filesystem::path create_dummy_frame(const std::filesystem::path& dir, int index, int width, int height) {
    char filename[64];
    std::snprintf(filename, sizeof(filename), "dummy_frame_%04d.png", index);
    auto path = dir / filename;
    
    ImageBuffer original(width, height, 3);
    Image view = original.view();
    std::fill(view.data.begin(), view.data.end(), 0.5f);
    
    auto res = frame_io::write_frame(path, view);
    REQUIRE(res.has_value());
    return path;
}
}

TEST_CASE("JobOrchestrator runs full sequence and respects cancellation", "[integration][app]") {
    auto model_path = std::filesystem::path(PROJECT_ROOT) / "models" / "corridorkey_int8_512.onnx";
    if (!std::filesystem::exists(model_path)) {
        SUCCEED("Model file not found, skipping orchestrator integration test.");
        return;
    }

    auto tmp_dir = std::filesystem::temp_directory_path() / "corridorkey_test_orchest";
    std::filesystem::create_directories(tmp_dir);

    // Create 3 dummy frames
    for (int i = 1; i <= 3; ++i) {
        create_dummy_frame(tmp_dir, i, 64, 64);
    }

    JobRequest request;
    request.input_path = tmp_dir;
    request.input_path = tmp_dir;
    request.output_path = tmp_dir / "out_seq";
    request.model_path = model_path;
    request.device = DeviceInfo{"Generic CPU", 0, Backend::CPU};
    request.params.target_resolution = 512;
    request.params.enable_tiling = false;

    SECTION("Happy path completion") {
        int frames_processed = 0;
        auto on_progress = [&](float /*progress*/, const std::string& status) -> bool {
            if (status.find("Processing frame") != std::string::npos) {
                frames_processed++;
            }
            return true; // Continue
        };

        auto run_res = JobOrchestrator::run(request, on_progress, nullptr);
        if (!run_res.has_value()) {
            FAIL("Orchestrator returned error: " << run_res.error().message);
        }
        
        // Ensure all frames were rendered
        REQUIRE(std::filesystem::exists(tmp_dir / "out_seq" / "Comp" / "dummy_frame_0001.png"));
        REQUIRE(std::filesystem::exists(tmp_dir / "out_seq" / "Comp" / "dummy_frame_0002.png"));
        REQUIRE(std::filesystem::exists(tmp_dir / "out_seq" / "Comp" / "dummy_frame_0003.png"));
    }

    SECTION("Immediate cancellation") {
        int frames_processed = 0;
        auto on_progress = [&](float /*progress*/, const std::string& status) -> bool {
            if (status.find("Processing frame") != std::string::npos) {
                frames_processed++;
            }
            // Cancel immediately on first progress report
            return false; 
        };

        auto run_res = JobOrchestrator::run(request, on_progress, nullptr);
        
        // The orchestrator returns an error `Cancellation requested`
        REQUIRE(!run_res.has_value());
        REQUIRE(std::string(run_res.error().message).find("cancel") != std::string::npos);
        
        // It should have halted before finishing the entire backlog
        REQUIRE(frames_processed < 3);
    }

    // Cleanup
    std::filesystem::remove_all(tmp_dir);
}
