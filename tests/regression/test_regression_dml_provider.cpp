#include <algorithm>
#include <catch2/catch_all.hpp>
#include <corridorkey/detail/constants.hpp>
#include <corridorkey/engine.hpp>
#include <string>

#if __has_include(<onnxruntime/onnxruntime_cxx_api.h>)
#include <onnxruntime/onnxruntime_cxx_api.h>
#elif __has_include(<onnxruntime/core/session/onnxruntime_cxx_api.h>)
#include <onnxruntime/core/session/onnxruntime_cxx_api.h>
#endif

using namespace corridorkey::detail;

TEST_CASE("Regression: DirectML provider string matches ONNX Runtime expectations",
          "[regression][device]") {
    REQUIRE(providers::DIRECTML == "DmlExecutionProvider");

#if defined(_WIN32)
    try {
        Ort::Env env;
        auto available = Ort::GetAvailableProviders();

        bool found_dml_with_wrong_case = false;
        for (const auto& p : available) {
            std::string p_upper = p;
            std::transform(p_upper.begin(), p_upper.end(), p_upper.begin(), ::toupper);

            if (p_upper == "DMLEXECUTIONPROVIDER" && p != providers::DIRECTML) {
                found_dml_with_wrong_case = true;
            }
        }

        REQUIRE_FALSE(found_dml_with_wrong_case);

    } catch (...) {
        SUCCEED("ONNX Runtime not available for dynamic provider check.");
    }
#endif
}
