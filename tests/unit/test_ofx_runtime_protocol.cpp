#include <catch2/catch_all.hpp>
#include <corridorkey/engine.hpp>

#include "app/ofx_runtime_protocol.hpp"

using namespace corridorkey;
using namespace corridorkey::app;

TEST_CASE("ofx runtime protocol roundtrips session payloads", "[unit][ofx][runtime]") {
    OfxRuntimePrepareSessionRequest prepare_request;
    prepare_request.client_instance_id = "instance-a";
    prepare_request.model_path = "models/corridorkey_fp16_1024.onnx";
    prepare_request.artifact_name = "corridorkey_fp16_1024.onnx";
    prepare_request.requested_device = DeviceInfo{"RTX 4090", 24576, Backend::TensorRT};
    prepare_request.engine_options.allow_cpu_fallback = false;
    prepare_request.engine_options.disable_cpu_ep_fallback = true;
    prepare_request.requested_quality_mode = 2;
    prepare_request.requested_resolution = 1024;
    prepare_request.effective_resolution = 1024;

    auto prepare_json = to_json(prepare_request);
    auto parsed_prepare = prepare_session_request_from_json(prepare_json);
    REQUIRE(parsed_prepare.has_value());
    CHECK(parsed_prepare->client_instance_id == prepare_request.client_instance_id);
    CHECK(parsed_prepare->model_path == prepare_request.model_path);
    CHECK(parsed_prepare->requested_device.backend == Backend::TensorRT);
    CHECK_FALSE(parsed_prepare->engine_options.allow_cpu_fallback);
    CHECK(parsed_prepare->engine_options.disable_cpu_ep_fallback);

    OfxRuntimeSessionSnapshot snapshot;
    snapshot.session_id = "session-1";
    snapshot.model_path = prepare_request.model_path;
    snapshot.artifact_name = prepare_request.artifact_name;
    snapshot.requested_device = prepare_request.requested_device;
    snapshot.effective_device = DeviceInfo{"RTX 4090", 24576, Backend::TensorRT};
    snapshot.backend_fallback =
        BackendFallbackInfo{Backend::TensorRT, Backend::CPU, "GPU residency was not maintained"};
    snapshot.requested_quality_mode = prepare_request.requested_quality_mode;
    snapshot.requested_resolution = prepare_request.requested_resolution;
    snapshot.effective_resolution = prepare_request.effective_resolution;
    snapshot.recommended_resolution = 1024;
    snapshot.ref_count = 2;
    snapshot.reused_existing_session = true;

    auto snapshot_json = to_json(snapshot);
    auto parsed_snapshot = session_snapshot_from_json(snapshot_json);
    REQUIRE(parsed_snapshot.has_value());
    CHECK(parsed_snapshot->session_id == snapshot.session_id);
    REQUIRE(parsed_snapshot->backend_fallback.has_value());
    CHECK(parsed_snapshot->backend_fallback->requested_backend == Backend::TensorRT);
    CHECK(parsed_snapshot->backend_fallback->selected_backend == Backend::CPU);
    CHECK(parsed_snapshot->backend_fallback->reason == "GPU residency was not maintained");
    CHECK(parsed_snapshot->ref_count == 2);
    CHECK(parsed_snapshot->reused_existing_session);

    StageTiming timing;
    timing.name = "ort_run";
    timing.total_ms = 42.5;
    timing.sample_count = 1;
    timing.work_units = 1;

    OfxRuntimePrepareSessionResponse response;
    response.session = snapshot;
    response.timings = {timing};

    auto response_json = to_json(response);
    auto parsed_response = prepare_session_response_from_json(response_json);
    REQUIRE(parsed_response.has_value());
    CHECK(parsed_response->session.artifact_name == snapshot.artifact_name);
    REQUIRE(parsed_response->timings.size() == 1);
    CHECK(parsed_response->timings.front().name == "ort_run");
    CHECK(parsed_response->timings.front().total_ms == Catch::Approx(42.5));
}

TEST_CASE("ofx runtime protocol roundtrips render envelopes", "[unit][ofx][runtime]") {
    InferenceParams params;
    params.target_resolution = 1024;
    params.requested_quality_resolution = 1536;
    params.quality_fallback_mode = QualityFallbackMode::CoarseToFine;
    params.refinement_mode = RefinementMode::Tiled;
    params.coarse_resolution_override = 1024;
    params.alpha_hint_policy = AlphaHintPolicy::RequireExternalHint;
    params.despill_strength = 0.35F;
    params.auto_despeckle = true;
    params.despeckle_size = 640;
    params.refiner_scale = 1.0F;
    params.input_is_linear = true;
    params.batch_size = 1;
    params.enable_tiling = true;
    params.tile_padding = 64;
    params.upscale_method = UpscaleMethod::Lanczos4;
    params.source_passthrough = true;
    params.sp_erode_px = 1;
    params.sp_blur_px = 2;
    params.output_alpha_only = true;

    OfxRuntimeRenderFrameRequest request;
    request.session_id = "session-1";
    request.shared_frame_path = "frames/frame_123.ckfx";
    request.width = 1920;
    request.height = 1080;
    request.params = params;
    request.render_index = 7;

    auto request_json = to_json(request);
    auto parsed_request = render_frame_request_from_json(request_json);
    REQUIRE(parsed_request.has_value());
    CHECK(parsed_request->session_id == request.session_id);
    CHECK(parsed_request->width == 1920);
    CHECK(parsed_request->height == 1080);
    CHECK(parsed_request->params.enable_tiling);
    CHECK(parsed_request->params.tile_padding == 64);
    CHECK(parsed_request->params.requested_quality_resolution == 1536);
    CHECK(parsed_request->params.quality_fallback_mode == QualityFallbackMode::CoarseToFine);
    CHECK(parsed_request->params.refinement_mode == RefinementMode::Tiled);
    CHECK(parsed_request->params.coarse_resolution_override == 1024);
    CHECK(parsed_request->params.alpha_hint_policy == AlphaHintPolicy::RequireExternalHint);
    CHECK(parsed_request->params.output_alpha_only);
    CHECK(parsed_request->render_index == 7);

    OfxRuntimeRequestEnvelope envelope;
    envelope.command = OfxRuntimeCommand::RenderFrame;
    envelope.payload = request_json;

    auto envelope_json = to_json(envelope);
    auto parsed_envelope = ofx_runtime_request_from_json(envelope_json);
    REQUIRE(parsed_envelope.has_value());
    CHECK(parsed_envelope->protocol_version == kOfxRuntimeProtocolVersion);
    CHECK(parsed_envelope->command == OfxRuntimeCommand::RenderFrame);

    OfxRuntimeResponseEnvelope ok_response;
    ok_response.success = true;
    ok_response.payload = nlohmann::json{{"accepted", true}};

    auto ok_json = to_json(ok_response);
    auto parsed_ok = ofx_runtime_response_from_json(ok_json);
    REQUIRE(parsed_ok.has_value());
    CHECK(parsed_ok->success);
    CHECK(parsed_ok->payload.at("accepted").get<bool>());
}

TEST_CASE("ofx runtime protocol rejects mismatched protocol versions",
          "[unit][ofx][runtime][regression]") {
    OfxRuntimeRequestEnvelope request;
    request.protocol_version = kOfxRuntimeProtocolVersion + 1;
    request.command = OfxRuntimeCommand::Health;
    request.payload = nlohmann::json::object();

    auto parsed_request = ofx_runtime_request_from_json(to_json(request));
    REQUIRE_FALSE(parsed_request.has_value());
    CHECK(parsed_request.error().message.find("Unsupported OFX runtime protocol version") !=
          std::string::npos);

    OfxRuntimeResponseEnvelope response;
    response.protocol_version = kOfxRuntimeProtocolVersion + 1;
    response.success = true;
    response.payload = nlohmann::json::object();

    auto parsed_response = ofx_runtime_response_from_json(to_json(response));
    REQUIRE_FALSE(parsed_response.has_value());
    CHECK(parsed_response.error().message.find("Unsupported OFX runtime protocol version") !=
          std::string::npos);
}
