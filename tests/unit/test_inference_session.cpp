#include <catch2/catch_all.hpp>
#include <src/core/inference_session.hpp>
#include <filesystem>

using namespace corridorkey;

TEST_CASE("InferenceSession::create validation", "[unit][inference]") {
    DeviceInfo cpu_device = { "CPU", 0, Backend::CPU };
    
    SECTION("Fails when model file does not exist") {
        auto result = InferenceSession::create("non_existent_model.onnx", cpu_device);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(result.error().code == ErrorCode::ModelLoadFailed);
    }
    
    // Note: We cannot easily test successful load without a real (even tiny) ONNX file.
    // That would be an integration test.
}
