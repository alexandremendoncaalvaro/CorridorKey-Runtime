#include <catch2/catch_all.hpp>
#include <algorithm>
#include <filesystem>
#include <system_error>

#include "../test_model_artifact_utils.hpp"
#include "app/ofx_session_broker.hpp"
#include "common/shared_memory_transport.hpp"

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

TEST_CASE("OFX session broker records broker writeback timing on render",
          "[integration][ofx][runtime][regression]") {
    const std::filesystem::path model_path = "models/corridorkey_int8_512.onnx";
    if (auto reason = corridorkey::tests::unusable_model_artifact_reason(model_path);
        reason.has_value()) {
        SKIP(*reason);
    }

    OfxSessionBroker broker;

    OfxRuntimePrepareSessionRequest prepare_request;
    prepare_request.client_instance_id = "render_test";
    prepare_request.model_path = model_path;
    prepare_request.artifact_name = model_path.filename().string();
    prepare_request.requested_device = DeviceInfo{"Generic CPU", 0, Backend::CPU};
    prepare_request.requested_quality_mode = 1;
    prepare_request.requested_resolution = 512;
    prepare_request.effective_resolution = 512;
    prepare_request.engine_options.allow_cpu_fallback = false;
    prepare_request.engine_options.disable_cpu_ep_fallback = true;

    auto prepare_res = broker.prepare_session(prepare_request);
    REQUIRE(prepare_res.has_value());

    const auto transport_path =
        std::filesystem::temp_directory_path() / "corridorkey_ofx_broker_render_test.ckfx";
    std::error_code error;
    std::filesystem::remove(transport_path, error);

    auto transport_res = common::SharedFrameTransport::create(transport_path, 96, 64);
    REQUIRE(transport_res.has_value());
    auto transport = std::move(*transport_res);
    std::fill(transport.rgb_view().data.begin(), transport.rgb_view().data.end(), 0.0F);
    std::fill(transport.hint_view().data.begin(), transport.hint_view().data.end(), 0.0F);

    OfxRuntimeRenderFrameRequest render_request;
    render_request.session_id = prepare_res->session.session_id;
    render_request.shared_frame_path = transport_path;
    render_request.width = 96;
    render_request.height = 64;
    render_request.render_index = 0;
    render_request.params.target_resolution = 512;

    auto render_res = broker.render_frame(render_request);
    std::filesystem::remove(transport_path, error);

    REQUIRE(render_res.has_value());
    CHECK(has_stage(render_res->timings, "frame_prepare_inputs"));
    CHECK(has_stage(render_res->timings, "ofx_broker_writeback"));

    auto release_res =
        broker.release_session(OfxRuntimeReleaseSessionRequest{prepare_res->session.session_id});
    REQUIRE(release_res.has_value());
}
