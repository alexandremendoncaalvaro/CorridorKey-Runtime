#include <catch2/catch_all.hpp>

#include "app/runtime_diagnostics.hpp"

using namespace corridorkey;
using namespace corridorkey::app;

TEST_CASE("windows TensorRT probes cover packaged FP16 resolutions",
          "[unit][doctor][regression]") {
    DeviceInfo device{"RTX 4080", 16384, Backend::TensorRT, 0};

    auto models = windows_probe_models_for_backend(Backend::TensorRT, device);
    const std::vector<std::string> expected_models{"corridorkey_fp16_2048.onnx",
                                                   "corridorkey_fp16_1536.onnx",
                                                   "corridorkey_fp16_1024.onnx",
                                                   "corridorkey_fp16_768.onnx",
                                                   "corridorkey_fp16_512.onnx"};

    REQUIRE(models == expected_models);
}

TEST_CASE("preferred Windows probe prioritizes strict TensorRT success",
          "[unit][doctor][regression]") {
    nlohmann::json probes = nlohmann::json::array(
        {{{"backend", "winml"},
          {"model", "corridorkey_fp16_1024.onnx"},
          {"requested_resolution", 1024},
          {"session_create_ok", true},
          {"frame_execute_ok", true},
          {"fallback_used", false}},
         {{"backend", "tensorrt"},
          {"model", "corridorkey_fp16_1536.onnx"},
          {"requested_resolution", 1536},
          {"session_create_ok", true},
          {"frame_execute_ok", true},
          {"fallback_used", false}}});

    auto preferred = preferred_windows_probe(probes);

    REQUIRE(preferred.has_value());
    REQUIRE(preferred->at("backend") == "tensorrt");
    REQUIRE(preferred->at("model") == "corridorkey_fp16_1536.onnx");
}

TEST_CASE("preferred Windows probe ignores probes that used fallback",
          "[unit][doctor][regression]") {
    nlohmann::json probes = nlohmann::json::array(
        {{{"backend", "tensorrt"},
          {"model", "corridorkey_fp16_2048.onnx"},
          {"requested_resolution", 2048},
          {"session_create_ok", true},
          {"frame_execute_ok", true},
          {"fallback_used", true}},
         {{"backend", "winml"},
          {"model", "corridorkey_fp16_1024.onnx"},
          {"requested_resolution", 1024},
          {"session_create_ok", true},
          {"frame_execute_ok", true},
          {"fallback_used", false}}});

    auto preferred = preferred_windows_probe(probes);

    REQUIRE(preferred.has_value());
    REQUIRE(preferred->at("backend") == "winml");
    REQUIRE(is_successful_windows_probe(*preferred));
}
