#include <catch2/catch_all.hpp>
#include <corridorkey/detail/constants.hpp>
#include <string_view>

using namespace corridorkey::detail;

TEST_CASE("Regression: Hardware Provider Identifiers must be EXACT", "[regression][hardware]") {
    // These strings are case-sensitive and required by ONNX Runtime.

    SECTION("NVIDIA RTX (TensorRT specialized)") {
        REQUIRE(providers::TENSORRT == "NvTensorRTRTXExecutionProvider");
    }

    SECTION("DirectML (AMD/Intel/Legacy RTX)") {
        REQUIRE(providers::DIRECTML == "DmlExecutionProvider");
    }

    SECTION("CoreML (Apple Silicon)") {
        REQUIRE(providers::COREML == "CoreMLExecutionProvider");
        REQUIRE(providers::COREML_API == "CoreML");
    }

    SECTION("Standard Providers") {
        REQUIRE(providers::CPU == "CPUExecutionProvider");
        REQUIRE(providers::CUDA == "CUDAExecutionProvider");
    }
}

TEST_CASE("Regression: Session Option Keys must be EXACT", "[regression][hardware]") {
    SECTION("Device Identification") {
        REQUIRE(session_options::DEVICE_ID == "device_id");
    }
}
