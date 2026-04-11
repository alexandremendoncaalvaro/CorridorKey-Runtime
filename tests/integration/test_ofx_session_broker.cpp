#include <catch2/catch_all.hpp>
#include <algorithm>
#include <filesystem>

#include "../test_model_artifact_utils.hpp"
#include "app/ofx_session_broker.hpp"

using namespace corridorkey;
using namespace corridorkey::app;

namespace {

bool has_stage(const std::vector<StageTiming>& timings, const std::string& name) {
    return std::any_of(timings.begin(), timings.end(),
                       [&](const StageTiming& timing) { return timing.name == name; });
}

}  // namespace

TEST_CASE("OFX session broker reuses sessions for the same executable model",
          "[integration][ofx][runtime][regression]") {
    const std::filesystem::path model_path = "models/corridorkey_int8_512.onnx";
    if (auto reason = corridorkey::tests::unusable_model_artifact_reason(model_path);
        reason.has_value()) {
        SKIP(*reason);
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
    CHECK(has_stage(first_prepare->timings, "ort_env_acquire"));
    CHECK(has_stage(first_prepare->timings, "ort_session_create"));
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
    CHECK(second_prepare->timings.empty());
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
