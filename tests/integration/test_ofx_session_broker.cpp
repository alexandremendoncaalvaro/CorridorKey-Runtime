#include <catch2/catch_all.hpp>
#include <filesystem>

#include "app/ofx_session_broker.hpp"

using namespace corridorkey;
using namespace corridorkey::app;

TEST_CASE("OFX session broker reuses sessions for the same executable model",
          "[integration][ofx][runtime][regression]") {
    const std::filesystem::path model_path = "models/corridorkey_int8_512.onnx";
    if (!std::filesystem::exists(model_path)) {
        SKIP("Model not available");
    }

    OfxSessionBroker broker;

    OfxRuntimePrepareSessionRequest first_request;
    first_request.client_instance_id = "bootstrap";
    first_request.model_path = model_path;
    first_request.artifact_name = "requested_alias.onnx";
    first_request.requested_device = DeviceInfo{"Generic CPU", 0, Backend::CPU};
    first_request.requested_quality_mode = 1;
    first_request.requested_resolution = 512;
    first_request.effective_resolution = 512;
    first_request.engine_options.allow_cpu_fallback = false;
    first_request.engine_options.disable_cpu_ep_fallback = true;

    auto first_prepare = broker.prepare_session(first_request);
    REQUIRE(first_prepare.has_value());
    CHECK_FALSE(first_prepare->session.reused_existing_session);
    CHECK(first_prepare->session.artifact_name == model_path.filename().string());
    CHECK(broker.session_count() == 1);
    CHECK(broker.active_session_count() == 1);

    auto second_request = first_request;
    second_request.client_instance_id = "quality_switch";
    second_request.artifact_name = model_path.filename().string();
    second_request.requested_quality_mode = 2;
    second_request.requested_resolution = 1024;
    second_request.effective_resolution = 512;

    auto second_prepare = broker.prepare_session(second_request);
    REQUIRE(second_prepare.has_value());
    CHECK(second_prepare->session.reused_existing_session);
    CHECK(second_prepare->session.session_id == first_prepare->session.session_id);
    CHECK(second_prepare->session.artifact_name == model_path.filename().string());
    CHECK(second_prepare->session.requested_quality_mode == first_request.requested_quality_mode);
    CHECK(second_prepare->session.requested_resolution == first_request.requested_resolution);
    CHECK(second_prepare->session.effective_resolution == first_request.effective_resolution);
    CHECK(second_prepare->session.ref_count == 2);
    CHECK(broker.session_count() == 1);
    CHECK(broker.active_session_count() == 1);

    auto first_release =
        broker.release_session(OfxRuntimeReleaseSessionRequest{first_prepare->session.session_id});
    REQUIRE(first_release.has_value());
    CHECK(broker.session_count() == 1);
    CHECK(broker.active_session_count() == 1);

    auto second_release =
        broker.release_session(OfxRuntimeReleaseSessionRequest{second_prepare->session.session_id});
    REQUIRE(second_release.has_value());
    CHECK(broker.session_count() == 1);
    CHECK(broker.active_session_count() == 0);
}

TEST_CASE("OFX session broker isolates sessions across execution engine requests",
          "[integration][ofx][runtime][regression]") {
    const std::filesystem::path model_path = "models/corridorkey_int8_512.onnx";
    if (!std::filesystem::exists(model_path)) {
        SKIP("Model not available");
    }

    OfxSessionBroker broker;

    OfxRuntimePrepareSessionRequest official_request;
    official_request.client_instance_id = "official";
    official_request.model_path = model_path;
    official_request.artifact_name = model_path.filename().string();
    official_request.requested_device = DeviceInfo{"Generic CPU", 0, Backend::CPU};
    official_request.requested_quality_mode = 1;
    official_request.requested_resolution = 512;
    official_request.effective_resolution = 512;
    official_request.engine_options.execution_engine = ExecutionEngine::Official;

    auto official_prepare = broker.prepare_session(official_request);
    REQUIRE(official_prepare.has_value());
    CHECK(official_prepare->session.requested_engine == ExecutionEngine::Official);

    auto max_request = official_request;
    max_request.client_instance_id = "max";
    max_request.engine_options.execution_engine = ExecutionEngine::MaxPerformance;

    auto max_prepare = broker.prepare_session(max_request);
    REQUIRE(max_prepare.has_value());
    CHECK(max_prepare->session.requested_engine == ExecutionEngine::MaxPerformance);
    CHECK(max_prepare->session.session_id != official_prepare->session.session_id);
    CHECK(broker.session_count() == 2);
}
