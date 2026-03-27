#include "ofx_runtime_protocol.hpp"

#include <array>

namespace corridorkey::app {

namespace {

using Json = nlohmann::json;

Error invalid_protocol_error(const std::string& message) {
    return Error{ErrorCode::InvalidParameters, message};
}

Result<void> validate_protocol_version(int protocol_version) {
    if (protocol_version != kOfxRuntimeProtocolVersion) {
        return Unexpected<Error>(invalid_protocol_error(
            "Unsupported OFX runtime protocol version: " + std::to_string(protocol_version) +
            ". Expected " + std::to_string(kOfxRuntimeProtocolVersion) + "."));
    }
    return {};
}

std::optional<Json> get_optional_object_field(const Json& json, const char* name) {
    if (!json.contains(name) || json.at(name).is_null()) {
        return std::nullopt;
    }
    return Json(json.at(name));
}

Result<std::string> required_string(const Json& json, const char* name) {
    if (!json.contains(name) || !json.at(name).is_string()) {
        return Unexpected<Error>(
            invalid_protocol_error(std::string("Missing string field: ") + name));
    }
    return json.at(name).get<std::string>();
}

Result<int> required_int(const Json& json, const char* name) {
    if (!json.contains(name) || !json.at(name).is_number_integer()) {
        return Unexpected<Error>(
            invalid_protocol_error(std::string("Missing integer field: ") + name));
    }
    return json.at(name).get<int>();
}

Result<std::uint64_t> required_uint64(const Json& json, const char* name) {
    if (!json.contains(name) || !json.at(name).is_number_unsigned()) {
        return Unexpected<Error>(
            invalid_protocol_error(std::string("Missing unsigned field: ") + name));
    }
    return json.at(name).get<std::uint64_t>();
}

Result<bool> required_bool(const Json& json, const char* name) {
    if (!json.contains(name) || !json.at(name).is_boolean()) {
        return Unexpected<Error>(
            invalid_protocol_error(std::string("Missing boolean field: ") + name));
    }
    return json.at(name).get<bool>();
}

Result<Json> required_object(const Json& json, const char* name) {
    if (!json.contains(name) || !json.at(name).is_object()) {
        return Unexpected<Error>(
            invalid_protocol_error(std::string("Missing object field: ") + name));
    }
    return json.at(name);
}

Result<std::vector<StageTiming>> stage_timings_from_json(const Json& json) {
    if (!json.is_array()) {
        return Unexpected<Error>(invalid_protocol_error("timings must be an array"));
    }

    std::vector<StageTiming> timings;
    timings.reserve(json.size());
    for (const auto& entry : json) {
        auto timing = stage_timing_from_json(entry);
        if (!timing) {
            return Unexpected<Error>(timing.error());
        }
        timings.push_back(*timing);
    }
    return timings;
}

Result<Backend> backend_from_string(const std::string& value) {
    if (value == "auto") return Backend::Auto;
    if (value == "cpu") return Backend::CPU;
    if (value == "cuda") return Backend::CUDA;
    if (value == "tensorrt") return Backend::TensorRT;
    if (value == "coreml") return Backend::CoreML;
    if (value == "dml") return Backend::DirectML;
    if (value == "mlx") return Backend::MLX;
    if (value == "winml") return Backend::WindowsML;
    if (value == "openvino") return Backend::OpenVINO;
    return Unexpected<Error>(invalid_protocol_error("Unknown backend: " + value));
}

std::string backend_to_string(Backend backend) {
    switch (backend) {
        case Backend::CPU:
            return "cpu";
        case Backend::CUDA:
            return "cuda";
        case Backend::TensorRT:
            return "tensorrt";
        case Backend::CoreML:
            return "coreml";
        case Backend::DirectML:
            return "dml";
        case Backend::MLX:
            return "mlx";
        case Backend::WindowsML:
            return "winml";
        case Backend::OpenVINO:
            return "openvino";
        default:
            return "auto";
    }
}

std::string quality_fallback_mode_to_string(QualityFallbackMode mode) {
    switch (mode) {
        case QualityFallbackMode::Direct:
            return "direct";
        case QualityFallbackMode::CoarseToFine:
            return "coarse_to_fine";
        default:
            return "auto";
    }
}

Result<QualityFallbackMode> quality_fallback_mode_from_string(const std::string& value) {
    if (value == "auto") return QualityFallbackMode::Auto;
    if (value == "direct") return QualityFallbackMode::Direct;
    if (value == "coarse_to_fine") return QualityFallbackMode::CoarseToFine;
    return Unexpected<Error>(invalid_protocol_error("Unknown quality fallback mode: " + value));
}

std::string refinement_mode_to_string(RefinementMode mode) {
    switch (mode) {
        case RefinementMode::FullFrame:
            return "full_frame";
        case RefinementMode::Tiled:
            return "tiled";
        default:
            return "auto";
    }
}

Result<RefinementMode> refinement_mode_from_string(const std::string& value) {
    if (value == "auto") return RefinementMode::Auto;
    if (value == "full_frame") return RefinementMode::FullFrame;
    if (value == "tiled") return RefinementMode::Tiled;
    return Unexpected<Error>(invalid_protocol_error("Unknown refinement mode: " + value));
}

Json backend_fallback_json(const BackendFallbackInfo& fallback) {
    return Json{{"requested_backend", backend_to_string(fallback.requested_backend)},
                {"selected_backend", backend_to_string(fallback.selected_backend)},
                {"reason", fallback.reason}};
}

Json stage_timing_json(const StageTiming& timing) {
    return Json{{"name", timing.name},
                {"total_ms", timing.total_ms},
                {"sample_count", timing.sample_count},
                {"work_units", timing.work_units}};
}

}  // namespace

std::string ofx_runtime_command_to_string(OfxRuntimeCommand command) {
    switch (command) {
        case OfxRuntimeCommand::Health:
            return "health";
        case OfxRuntimeCommand::PrepareSession:
            return "prepare_session";
        case OfxRuntimeCommand::RenderFrame:
            return "render_frame";
        case OfxRuntimeCommand::ReleaseSession:
            return "release_session";
        case OfxRuntimeCommand::Shutdown:
            return "shutdown";
    }
    return "health";
}

Result<OfxRuntimeCommand> ofx_runtime_command_from_string(const std::string& value) {
    if (value == "health") return OfxRuntimeCommand::Health;
    if (value == "prepare_session") return OfxRuntimeCommand::PrepareSession;
    if (value == "render_frame") return OfxRuntimeCommand::RenderFrame;
    if (value == "release_session") return OfxRuntimeCommand::ReleaseSession;
    if (value == "shutdown") return OfxRuntimeCommand::Shutdown;
    return Unexpected<Error>(invalid_protocol_error("Unknown command: " + value));
}

nlohmann::json to_json(const DeviceInfo& device) {
    return Json{{"name", device.name},
                {"available_memory_mb", device.available_memory_mb},
                {"backend", backend_to_string(device.backend)}};
}

Result<DeviceInfo> device_from_json(const nlohmann::json& json) {
    auto name = required_string(json, "name");
    if (!name) return Unexpected<Error>(name.error());
    if (!json.contains("available_memory_mb") ||
        !json.at("available_memory_mb").is_number_integer()) {
        return Unexpected<Error>(
            invalid_protocol_error("Missing integer field: available_memory_mb"));
    }
    auto backend_string = required_string(json, "backend");
    if (!backend_string) return Unexpected<Error>(backend_string.error());
    auto backend = backend_from_string(*backend_string);
    if (!backend) return Unexpected<Error>(backend.error());

    DeviceInfo device;
    device.name = *name;
    device.available_memory_mb = json.at("available_memory_mb").get<std::int64_t>();
    device.backend = *backend;
    return device;
}

Result<BackendFallbackInfo> backend_fallback_from_json(const nlohmann::json& json) {
    auto requested = required_string(json, "requested_backend");
    if (!requested) return Unexpected<Error>(requested.error());
    auto selected = required_string(json, "selected_backend");
    if (!selected) return Unexpected<Error>(selected.error());
    auto reason = required_string(json, "reason");
    if (!reason) return Unexpected<Error>(reason.error());

    auto requested_backend = backend_from_string(*requested);
    if (!requested_backend) return Unexpected<Error>(requested_backend.error());
    auto selected_backend = backend_from_string(*selected);
    if (!selected_backend) return Unexpected<Error>(selected_backend.error());

    return BackendFallbackInfo{*requested_backend, *selected_backend, *reason};
}

nlohmann::json to_json(const EngineCreateOptions& options) {
    return Json{{"allow_cpu_fallback", options.allow_cpu_fallback},
                {"disable_cpu_ep_fallback", options.disable_cpu_ep_fallback}};
}

Result<EngineCreateOptions> engine_create_options_from_json(const nlohmann::json& json) {
    auto allow = required_bool(json, "allow_cpu_fallback");
    if (!allow) return Unexpected<Error>(allow.error());
    auto disable = required_bool(json, "disable_cpu_ep_fallback");
    if (!disable) return Unexpected<Error>(disable.error());

    EngineCreateOptions options;
    options.allow_cpu_fallback = *allow;
    options.disable_cpu_ep_fallback = *disable;
    return options;
}

nlohmann::json to_json(const InferenceParams& params) {
    return Json{{"target_resolution", params.target_resolution},
                {"requested_quality_resolution", params.requested_quality_resolution},
                {"quality_fallback_mode",
                 quality_fallback_mode_to_string(params.quality_fallback_mode)},
                {"refinement_mode", refinement_mode_to_string(params.refinement_mode)},
                {"coarse_resolution_override", params.coarse_resolution_override},
                {"despill_strength", params.despill_strength},
                {"spill_method", params.spill_method},
                {"auto_despeckle", params.auto_despeckle},
                {"despeckle_size", params.despeckle_size},
                {"refiner_scale", params.refiner_scale},
                {"input_is_linear", params.input_is_linear},
                {"batch_size", params.batch_size},
                {"enable_tiling", params.enable_tiling},
                {"tile_padding", params.tile_padding},
                {"upscale_method",
                 params.upscale_method == UpscaleMethod::Lanczos4 ? "lanczos4" : "bilinear"},
                {"source_passthrough", params.source_passthrough},
                {"sp_erode_px", params.sp_erode_px},
                {"sp_blur_px", params.sp_blur_px}};
}

Result<InferenceParams> inference_params_from_json(const nlohmann::json& json) {
    InferenceParams params;
    auto target_resolution = required_int(json, "target_resolution");
    if (!target_resolution) return Unexpected<Error>(target_resolution.error());
    params.target_resolution = *target_resolution;
    if (json.contains("requested_quality_resolution") &&
        json.at("requested_quality_resolution").is_number_integer()) {
        params.requested_quality_resolution =
            json.at("requested_quality_resolution").get<int>();
    }
    if (json.contains("quality_fallback_mode") && json.at("quality_fallback_mode").is_string()) {
        auto fallback_mode =
            quality_fallback_mode_from_string(json.at("quality_fallback_mode").get<std::string>());
        if (!fallback_mode) return Unexpected<Error>(fallback_mode.error());
        params.quality_fallback_mode = *fallback_mode;
    }
    if (json.contains("refinement_mode") && json.at("refinement_mode").is_string()) {
        auto refinement_mode =
            refinement_mode_from_string(json.at("refinement_mode").get<std::string>());
        if (!refinement_mode) return Unexpected<Error>(refinement_mode.error());
        params.refinement_mode = *refinement_mode;
    }
    if (json.contains("coarse_resolution_override") &&
        json.at("coarse_resolution_override").is_number_integer()) {
        params.coarse_resolution_override = json.at("coarse_resolution_override").get<int>();
    }
    if (!json.contains("despill_strength") || !json.at("despill_strength").is_number()) {
        return Unexpected<Error>(invalid_protocol_error("Missing numeric field: despill_strength"));
    }
    params.despill_strength = json.at("despill_strength").get<float>();
    if (json.contains("spill_method") && json.at("spill_method").is_number_integer()) {
        int method = json.at("spill_method").get<int>();
        if (method >= 0 && method <= 2) {
            params.spill_method = method;
        }
    }
    auto auto_despeckle = required_bool(json, "auto_despeckle");
    if (!auto_despeckle) return Unexpected<Error>(auto_despeckle.error());
    params.auto_despeckle = *auto_despeckle;
    auto despeckle_size = required_int(json, "despeckle_size");
    if (!despeckle_size) return Unexpected<Error>(despeckle_size.error());
    params.despeckle_size = *despeckle_size;
    if (!json.contains("refiner_scale") || !json.at("refiner_scale").is_number()) {
        return Unexpected<Error>(invalid_protocol_error("Missing numeric field: refiner_scale"));
    }
    params.refiner_scale = json.at("refiner_scale").get<float>();
    auto input_is_linear = required_bool(json, "input_is_linear");
    if (!input_is_linear) return Unexpected<Error>(input_is_linear.error());
    params.input_is_linear = *input_is_linear;
    auto batch_size = required_int(json, "batch_size");
    if (!batch_size) return Unexpected<Error>(batch_size.error());
    params.batch_size = *batch_size;
    auto enable_tiling = required_bool(json, "enable_tiling");
    if (!enable_tiling) return Unexpected<Error>(enable_tiling.error());
    params.enable_tiling = *enable_tiling;
    auto tile_padding = required_int(json, "tile_padding");
    if (!tile_padding) return Unexpected<Error>(tile_padding.error());
    params.tile_padding = *tile_padding;
    auto upscale_method = required_string(json, "upscale_method");
    if (!upscale_method) return Unexpected<Error>(upscale_method.error());
    params.upscale_method =
        *upscale_method == "bilinear" ? UpscaleMethod::Bilinear : UpscaleMethod::Lanczos4;
    auto source_passthrough = required_bool(json, "source_passthrough");
    if (!source_passthrough) return Unexpected<Error>(source_passthrough.error());
    params.source_passthrough = *source_passthrough;
    auto sp_erode_px = required_int(json, "sp_erode_px");
    if (!sp_erode_px) return Unexpected<Error>(sp_erode_px.error());
    params.sp_erode_px = *sp_erode_px;
    auto sp_blur_px = required_int(json, "sp_blur_px");
    if (!sp_blur_px) return Unexpected<Error>(sp_blur_px.error());
    params.sp_blur_px = *sp_blur_px;
    return params;
}

Result<StageTiming> stage_timing_from_json(const nlohmann::json& json) {
    auto name = required_string(json, "name");
    if (!name) return Unexpected<Error>(name.error());
    if (!json.contains("total_ms") || !json.at("total_ms").is_number()) {
        return Unexpected<Error>(invalid_protocol_error("Missing numeric field: total_ms"));
    }
    auto sample_count = required_uint64(json, "sample_count");
    if (!sample_count) return Unexpected<Error>(sample_count.error());
    auto work_units = required_uint64(json, "work_units");
    if (!work_units) return Unexpected<Error>(work_units.error());

    StageTiming timing;
    timing.name = *name;
    timing.total_ms = json.at("total_ms").get<double>();
    timing.sample_count = *sample_count;
    timing.work_units = *work_units;
    return timing;
}

nlohmann::json to_json(const OfxRuntimeRequestEnvelope& envelope) {
    return Json{{"protocol_version", envelope.protocol_version},
                {"command", ofx_runtime_command_to_string(envelope.command)},
                {"payload", envelope.payload}};
}

Result<OfxRuntimeRequestEnvelope> ofx_runtime_request_from_json(const nlohmann::json& json) {
    auto protocol_version = required_int(json, "protocol_version");
    if (!protocol_version) return Unexpected<Error>(protocol_version.error());
    auto protocol_valid = validate_protocol_version(*protocol_version);
    if (!protocol_valid) return Unexpected<Error>(protocol_valid.error());
    auto command_string = required_string(json, "command");
    if (!command_string) return Unexpected<Error>(command_string.error());
    auto command = ofx_runtime_command_from_string(*command_string);
    if (!command) return Unexpected<Error>(command.error());
    auto payload = required_object(json, "payload");
    if (!payload) return Unexpected<Error>(payload.error());
    return OfxRuntimeRequestEnvelope{*protocol_version, *command, *payload};
}

nlohmann::json to_json(const OfxRuntimeResponseEnvelope& envelope) {
    return Json{{"protocol_version", envelope.protocol_version},
                {"success", envelope.success},
                {"error", envelope.error},
                {"payload", envelope.payload}};
}

Result<OfxRuntimeResponseEnvelope> ofx_runtime_response_from_json(const nlohmann::json& json) {
    auto protocol_version = required_int(json, "protocol_version");
    if (!protocol_version) return Unexpected<Error>(protocol_version.error());
    auto protocol_valid = validate_protocol_version(*protocol_version);
    if (!protocol_valid) return Unexpected<Error>(protocol_valid.error());
    auto success = required_bool(json, "success");
    if (!success) return Unexpected<Error>(success.error());
    auto error = required_string(json, "error");
    if (!error) return Unexpected<Error>(error.error());
    auto payload = required_object(json, "payload");
    if (!payload) return Unexpected<Error>(payload.error());
    return OfxRuntimeResponseEnvelope{*protocol_version, *success, *error, *payload};
}

nlohmann::json to_json(const OfxRuntimePrepareSessionRequest& request) {
    return Json{{"client_instance_id", request.client_instance_id},
                {"model_path", request.model_path.string()},
                {"artifact_name", request.artifact_name},
                {"requested_device", to_json(request.requested_device)},
                {"engine_options", to_json(request.engine_options)},
                {"requested_quality_mode", request.requested_quality_mode},
                {"requested_resolution", request.requested_resolution},
                {"effective_resolution", request.effective_resolution}};
}

Result<OfxRuntimePrepareSessionRequest> prepare_session_request_from_json(
    const nlohmann::json& json) {
    auto client_instance_id = required_string(json, "client_instance_id");
    if (!client_instance_id) return Unexpected<Error>(client_instance_id.error());
    auto model_path = required_string(json, "model_path");
    if (!model_path) return Unexpected<Error>(model_path.error());
    auto artifact_name = required_string(json, "artifact_name");
    if (!artifact_name) return Unexpected<Error>(artifact_name.error());
    auto requested_device_json = required_object(json, "requested_device");
    if (!requested_device_json) return Unexpected<Error>(requested_device_json.error());
    auto requested_device = device_from_json(*requested_device_json);
    if (!requested_device) return Unexpected<Error>(requested_device.error());
    auto engine_options_json = required_object(json, "engine_options");
    if (!engine_options_json) return Unexpected<Error>(engine_options_json.error());
    auto engine_options = engine_create_options_from_json(*engine_options_json);
    if (!engine_options) return Unexpected<Error>(engine_options.error());
    auto requested_quality_mode = required_int(json, "requested_quality_mode");
    if (!requested_quality_mode) return Unexpected<Error>(requested_quality_mode.error());
    auto requested_resolution = required_int(json, "requested_resolution");
    if (!requested_resolution) return Unexpected<Error>(requested_resolution.error());
    auto effective_resolution = required_int(json, "effective_resolution");
    if (!effective_resolution) return Unexpected<Error>(effective_resolution.error());

    OfxRuntimePrepareSessionRequest request;
    request.client_instance_id = *client_instance_id;
    request.model_path = *model_path;
    request.artifact_name = *artifact_name;
    request.requested_device = *requested_device;
    request.engine_options = *engine_options;
    request.requested_quality_mode = *requested_quality_mode;
    request.requested_resolution = *requested_resolution;
    request.effective_resolution = *effective_resolution;
    return request;
}

nlohmann::json to_json(const OfxRuntimeSessionSnapshot& snapshot) {
    Json json{{"session_id", snapshot.session_id},
              {"model_path", snapshot.model_path.string()},
              {"artifact_name", snapshot.artifact_name},
              {"requested_device", to_json(snapshot.requested_device)},
              {"effective_device", to_json(snapshot.effective_device)},
              {"requested_quality_mode", snapshot.requested_quality_mode},
              {"requested_resolution", snapshot.requested_resolution},
              {"effective_resolution", snapshot.effective_resolution},
              {"recommended_resolution", snapshot.recommended_resolution},
              {"ref_count", snapshot.ref_count},
              {"reused_existing_session", snapshot.reused_existing_session}};
    if (snapshot.backend_fallback.has_value()) {
        json["backend_fallback"] = backend_fallback_json(*snapshot.backend_fallback);
    }
    return json;
}

Result<OfxRuntimeSessionSnapshot> session_snapshot_from_json(const nlohmann::json& json) {
    auto session_id = required_string(json, "session_id");
    if (!session_id) return Unexpected<Error>(session_id.error());
    auto model_path = required_string(json, "model_path");
    if (!model_path) return Unexpected<Error>(model_path.error());
    auto artifact_name = required_string(json, "artifact_name");
    if (!artifact_name) return Unexpected<Error>(artifact_name.error());
    auto requested_device_json = required_object(json, "requested_device");
    if (!requested_device_json) return Unexpected<Error>(requested_device_json.error());
    auto requested_device = device_from_json(*requested_device_json);
    if (!requested_device) return Unexpected<Error>(requested_device.error());
    auto effective_device_json = required_object(json, "effective_device");
    if (!effective_device_json) return Unexpected<Error>(effective_device_json.error());
    auto effective_device = device_from_json(*effective_device_json);
    if (!effective_device) return Unexpected<Error>(effective_device.error());
    auto requested_quality_mode = required_int(json, "requested_quality_mode");
    if (!requested_quality_mode) return Unexpected<Error>(requested_quality_mode.error());
    auto requested_resolution = required_int(json, "requested_resolution");
    if (!requested_resolution) return Unexpected<Error>(requested_resolution.error());
    auto effective_resolution = required_int(json, "effective_resolution");
    if (!effective_resolution) return Unexpected<Error>(effective_resolution.error());
    auto recommended_resolution = required_int(json, "recommended_resolution");
    if (!recommended_resolution) return Unexpected<Error>(recommended_resolution.error());
    auto ref_count = required_uint64(json, "ref_count");
    if (!ref_count) return Unexpected<Error>(ref_count.error());
    auto reused_existing_session = required_bool(json, "reused_existing_session");
    if (!reused_existing_session) {
        return Unexpected<Error>(reused_existing_session.error());
    }

    std::optional<BackendFallbackInfo> backend_fallback = std::nullopt;
    if (auto fallback_json = get_optional_object_field(json, "backend_fallback");
        fallback_json.has_value()) {
        auto fallback = backend_fallback_from_json(*fallback_json);
        if (!fallback) return Unexpected<Error>(fallback.error());
        backend_fallback = *fallback;
    }

    OfxRuntimeSessionSnapshot snapshot;
    snapshot.session_id = *session_id;
    snapshot.model_path = *model_path;
    snapshot.artifact_name = *artifact_name;
    snapshot.requested_device = *requested_device;
    snapshot.effective_device = *effective_device;
    snapshot.backend_fallback = backend_fallback;
    snapshot.requested_quality_mode = *requested_quality_mode;
    snapshot.requested_resolution = *requested_resolution;
    snapshot.effective_resolution = *effective_resolution;
    snapshot.recommended_resolution = *recommended_resolution;
    snapshot.ref_count = *ref_count;
    snapshot.reused_existing_session = *reused_existing_session;
    return snapshot;
}

nlohmann::json to_json(const OfxRuntimePrepareSessionResponse& response) {
    Json timings = Json::array();
    for (const auto& timing : response.timings) {
        timings.push_back(stage_timing_json(timing));
    }
    return Json{{"session", to_json(response.session)}, {"timings", timings}};
}

Result<OfxRuntimePrepareSessionResponse> prepare_session_response_from_json(
    const nlohmann::json& json) {
    auto session_json = required_object(json, "session");
    if (!session_json) return Unexpected<Error>(session_json.error());
    auto session = session_snapshot_from_json(*session_json);
    if (!session) return Unexpected<Error>(session.error());
    auto timings_json = json.contains("timings") ? json.at("timings") : Json::array();
    auto timings = stage_timings_from_json(timings_json);
    if (!timings) return Unexpected<Error>(timings.error());
    return OfxRuntimePrepareSessionResponse{*session, *timings};
}

nlohmann::json to_json(const OfxRuntimeRenderFrameRequest& request) {
    return Json{{"session_id", request.session_id},
                {"shared_frame_path", request.shared_frame_path.string()},
                {"width", request.width},
                {"height", request.height},
                {"params", to_json(request.params)},
                {"render_index", request.render_index}};
}

Result<OfxRuntimeRenderFrameRequest> render_frame_request_from_json(const nlohmann::json& json) {
    auto session_id = required_string(json, "session_id");
    if (!session_id) return Unexpected<Error>(session_id.error());
    auto shared_frame_path = required_string(json, "shared_frame_path");
    if (!shared_frame_path) return Unexpected<Error>(shared_frame_path.error());
    auto width = required_int(json, "width");
    if (!width) return Unexpected<Error>(width.error());
    auto height = required_int(json, "height");
    if (!height) return Unexpected<Error>(height.error());
    auto params_json = required_object(json, "params");
    if (!params_json) return Unexpected<Error>(params_json.error());
    auto params = inference_params_from_json(*params_json);
    if (!params) return Unexpected<Error>(params.error());
    auto render_index = required_uint64(json, "render_index");
    if (!render_index) return Unexpected<Error>(render_index.error());

    OfxRuntimeRenderFrameRequest request;
    request.session_id = *session_id;
    request.shared_frame_path = *shared_frame_path;
    request.width = *width;
    request.height = *height;
    request.params = *params;
    request.render_index = *render_index;
    return request;
}

nlohmann::json to_json(const OfxRuntimeRenderFrameResponse& response) {
    Json timings = Json::array();
    for (const auto& timing : response.timings) {
        timings.push_back(stage_timing_json(timing));
    }
    return Json{{"session", to_json(response.session)}, {"timings", timings}};
}

Result<OfxRuntimeRenderFrameResponse> render_frame_response_from_json(const nlohmann::json& json) {
    auto session_json = required_object(json, "session");
    if (!session_json) return Unexpected<Error>(session_json.error());
    auto session = session_snapshot_from_json(*session_json);
    if (!session) return Unexpected<Error>(session.error());
    auto timings_json = json.contains("timings") ? json.at("timings") : Json::array();
    auto timings = stage_timings_from_json(timings_json);
    if (!timings) return Unexpected<Error>(timings.error());
    return OfxRuntimeRenderFrameResponse{*session, *timings};
}

nlohmann::json to_json(const OfxRuntimeReleaseSessionRequest& request) {
    return Json{{"session_id", request.session_id}};
}

Result<OfxRuntimeReleaseSessionRequest> release_session_request_from_json(
    const nlohmann::json& json) {
    auto session_id = required_string(json, "session_id");
    if (!session_id) return Unexpected<Error>(session_id.error());
    return OfxRuntimeReleaseSessionRequest{*session_id};
}

nlohmann::json to_json(const OfxRuntimeHealthResponse& response) {
    return Json{{"server_pid", response.server_pid},
                {"session_count", response.session_count},
                {"active_session_count", response.active_session_count}};
}

Result<OfxRuntimeHealthResponse> health_response_from_json(const nlohmann::json& json) {
    auto server_pid = required_int(json, "server_pid");
    if (!server_pid) return Unexpected<Error>(server_pid.error());
    auto session_count = required_uint64(json, "session_count");
    if (!session_count) return Unexpected<Error>(session_count.error());
    auto active_session_count = required_uint64(json, "active_session_count");
    if (!active_session_count) return Unexpected<Error>(active_session_count.error());
    return OfxRuntimeHealthResponse{*server_pid, *session_count, *active_session_count};
}

nlohmann::json to_json(const OfxRuntimeShutdownRequest& request) {
    return Json{{"reason", request.reason}};
}

Result<OfxRuntimeShutdownRequest> shutdown_request_from_json(const nlohmann::json& json) {
    auto reason = required_string(json, "reason");
    if (!reason) return Unexpected<Error>(reason.error());
    return OfxRuntimeShutdownRequest{*reason};
}

}  // namespace corridorkey::app
