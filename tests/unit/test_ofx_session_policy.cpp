#include <catch2/catch_all.hpp>

#include "app/ofx_session_policy.hpp"

using namespace corridorkey;

TEST_CASE("OFX session policy canonicalizes artifact names", "[unit][ofx][runtime][regression]") {
    CHECK(app::detail::canonical_ofx_artifact_name("models/corridorkey_fp16_1536_ctx.onnx") ==
          "corridorkey_fp16_1536_ctx.onnx");
    CHECK(app::detail::canonical_ofx_artifact_name("corridorkey_int8_512.onnx") ==
          "corridorkey_int8_512.onnx");
}

TEST_CASE("OFX session policy destroys zero-ref TensorRT sessions",
          "[unit][ofx][runtime][regression]") {
    CHECK(app::detail::should_destroy_zero_ref_session(Backend::TensorRT));
    CHECK_FALSE(app::detail::should_destroy_zero_ref_session(Backend::CPU));
    CHECK_FALSE(app::detail::should_destroy_zero_ref_session(Backend::CUDA));
}
