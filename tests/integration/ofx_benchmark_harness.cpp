#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <numeric>
#include <string>
#include <system_error>
#include <vector>

#include "app/job_orchestrator.hpp"
#include "app/ofx_session_broker.hpp"
#include "app/runtime_contracts.hpp"
#include "common/runtime_paths.hpp"
#include "common/shared_memory_transport.hpp"
#include "common/stage_profiler.hpp"
#include "core/inference_session_metadata.hpp"

using namespace corridorkey;
using namespace corridorkey::app;

namespace {

struct HarnessOptions {
    std::filesystem::path model_path = std::filesystem::path(PROJECT_ROOT) / "models" /
                                       "corridorkey_int8_512.onnx";
    DeviceInfo device = DeviceInfo{"Generic CPU", 0, Backend::CPU};
    int resolution = 512;
    int iterations = 5;
    core::IoBindingMode io_binding_mode = core::IoBindingMode::Auto;
};

Result<HarnessOptions> parse_arguments(int argc, char* argv[]) {
    HarnessOptions options;

    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--model" && index + 1 < argc) {
            options.model_path = argv[++index];
            continue;
        }
        if (argument == "--resolution" && index + 1 < argc) {
            options.resolution = std::atoi(argv[++index]);
            continue;
        }
        if (argument == "--iterations" && index + 1 < argc) {
            options.iterations = std::atoi(argv[++index]);
            continue;
        }
        if (argument == "--device" && index + 1 < argc) {
            const std::string device_name = argv[++index];
            if (device_name == "auto") {
                options.device = auto_detect();
            } else if (device_name == "rtx" || device_name == "tensorrt") {
                options.device = DeviceInfo{"TensorRT RTX", 0, Backend::TensorRT};
            } else if (device_name == "cpu") {
                options.device = DeviceInfo{"Generic CPU", 0, Backend::CPU};
            } else if (device_name == "cuda") {
                options.device = DeviceInfo{"CUDA", 0, Backend::CUDA};
            } else if (device_name == "dml") {
                options.device = DeviceInfo{"DirectML", 0, Backend::DirectML};
            } else if (device_name == "coreml") {
                options.device = DeviceInfo{"CoreML", 0, Backend::CoreML};
            } else if (device_name == "mlx") {
                options.device = DeviceInfo{"MLX", 0, Backend::MLX};
            } else {
                return Unexpected(Error{ErrorCode::InvalidParameters,
                                        "Unsupported --device for OFX harness: " + device_name});
            }
            continue;
        }
        if (argument == "--io-binding" && index + 1 < argc) {
            const auto parsed = core::parse_io_binding_mode(argv[++index]);
            if (!parsed.has_value()) {
                return Unexpected(
                    Error{ErrorCode::InvalidParameters, "Unsupported --io-binding value."});
            }
            options.io_binding_mode = *parsed;
            continue;
        }

        return Unexpected(
            Error{ErrorCode::InvalidParameters, "Unknown OFX harness argument: " + argument});
    }

    if (options.resolution <= 0) {
        return Unexpected(Error{ErrorCode::InvalidParameters,
                                "Resolution must be greater than zero."});
    }
    if (options.iterations <= 0) {
        return Unexpected(Error{ErrorCode::InvalidParameters,
                                "Iterations must be greater than zero."});
    }

    return options;
}

nlohmann::json stage_timings_to_json(const std::vector<StageTiming>& timings) {
    nlohmann::json stage_timings = nlohmann::json::array();
    for (const auto& timing : timings) {
        stage_timings.push_back(to_json(timing));
    }
    return stage_timings;
}

nlohmann::json failure_json(const std::string& message) {
    return nlohmann::json{{"success", false}, {"error", message}};
}

void apply_io_binding_environment(core::IoBindingMode mode) {
#ifdef _WIN32
    _putenv_s("CORRIDORKEY_IO_BINDING", std::string(core::io_binding_mode_to_string(mode)).c_str());
#else
    setenv("CORRIDORKEY_IO_BINDING", std::string(core::io_binding_mode_to_string(mode)).c_str(), 1);
#endif
}

}  // namespace

int main(int argc, char* argv[]) {
    const auto options_res = parse_arguments(argc, argv);
    if (!options_res) {
        std::cout << failure_json(options_res.error().message).dump(4) << std::endl;
        return 1;
    }

    const HarnessOptions options = *options_res;
    apply_io_binding_environment(options.io_binding_mode);
    const auto artifact = common::inspect_model_artifact(options.model_path);
    if (!artifact.found) {
        std::cout << failure_json("Model file not found: " + options.model_path.string()).dump(4)
                  << std::endl;
        return 1;
    }
    if (!artifact.usable) {
        std::cout << failure_json(artifact.detail).dump(4) << std::endl;
        return 1;
    }

    const auto transport_path = common::next_ofx_shared_frame_path();
    std::error_code cleanup_error;
    std::filesystem::remove(transport_path, cleanup_error);

    auto transport_res =
        common::SharedFrameTransport::create(transport_path, options.resolution, options.resolution);
    if (!transport_res) {
        std::cout << failure_json(transport_res.error().message).dump(4) << std::endl;
        return 1;
    }

    auto transport = std::move(*transport_res);
    std::fill(transport.rgb_view().data.begin(), transport.rgb_view().data.end(), 0.0f);
    std::fill(transport.hint_view().data.begin(), transport.hint_view().data.end(), 0.0f);

    common::StageProfiler profiler;
    OfxSessionBroker broker;

    OfxRuntimePrepareSessionRequest prepare_request;
    prepare_request.client_instance_id = "benchmark";
    prepare_request.model_path = options.model_path;
    prepare_request.artifact_name = options.model_path.filename().string();
    prepare_request.requested_device = options.device;
    prepare_request.requested_quality_mode = 1;
    prepare_request.requested_resolution = options.resolution;
    prepare_request.effective_resolution = options.resolution;
    prepare_request.engine_options.allow_cpu_fallback = true;

    auto prepare_start = std::chrono::steady_clock::now();
    auto prepare_res = broker.prepare_session(prepare_request);
    auto prepare_end = std::chrono::steady_clock::now();
    if (!prepare_res) {
        std::filesystem::remove(transport_path, cleanup_error);
        std::cout << failure_json(prepare_res.error().message).dump(4) << std::endl;
        return 1;
    }

    profiler.record("ofx_prepare_session",
                    std::chrono::duration<double, std::milli>(prepare_end - prepare_start).count(),
                    1);
    for (const auto& timing : prepare_res->timings) {
        profiler.record(timing);
    }

    const auto session_id = prepare_res->session.session_id;
    DeviceInfo effective_device = prepare_res->session.effective_device;
    std::optional<BackendFallbackInfo> fallback = prepare_res->session.backend_fallback;
    std::vector<double> render_latencies_ms;
    render_latencies_ms.reserve(static_cast<std::size_t>(options.iterations));

    for (int iteration = 0; iteration < options.iterations; ++iteration) {
        OfxRuntimeRenderFrameRequest render_request;
        render_request.session_id = session_id;
        render_request.shared_frame_path = transport_path;
        render_request.width = options.resolution;
        render_request.height = options.resolution;
        render_request.render_index = static_cast<std::uint64_t>(iteration);
        render_request.params.target_resolution = options.resolution;

        auto render_start = std::chrono::steady_clock::now();
        auto render_res = broker.render_frame(render_request);
        auto render_end = std::chrono::steady_clock::now();
        if (!render_res) {
            std::filesystem::remove(transport_path, cleanup_error);
            std::cout << failure_json(render_res.error().message).dump(4) << std::endl;
            return 1;
        }

        const double render_latency_ms =
            std::chrono::duration<double, std::milli>(render_end - render_start).count();
        render_latencies_ms.push_back(render_latency_ms);
        profiler.record("ofx_render_roundtrip", render_latency_ms, 1);
        for (const auto& timing : render_res->timings) {
            profiler.record(timing);
        }
        effective_device = render_res->session.effective_device;
        fallback = render_res->session.backend_fallback;
    }

    auto release_start = std::chrono::steady_clock::now();
    auto release_res = broker.release_session(OfxRuntimeReleaseSessionRequest{session_id});
    auto release_end = std::chrono::steady_clock::now();
    std::filesystem::remove(transport_path, cleanup_error);
    if (!release_res) {
        std::cout << failure_json(release_res.error().message).dump(4) << std::endl;
        return 1;
    }

    profiler.record("ofx_release_session",
                    std::chrono::duration<double, std::milli>(release_end - release_start).count(),
                    1);

    const double total_render_ms =
        std::accumulate(render_latencies_ms.begin(), render_latencies_ms.end(), 0.0);
    const double average_latency_ms =
        total_render_ms / static_cast<double>(render_latencies_ms.size());

    nlohmann::json results;
    results["success"] = true;
    results["mode"] = "ofx_broker_synthetic";
    results["model"] = options.model_path.filename().string();
    results["artifact"] = options.model_path.filename().string();
    results["artifact_path"] = options.model_path.string();
    results["resolution"] = options.resolution;
    results["requested_resolution"] = options.resolution;
    results["effective_resolution"] = options.resolution;
    results["requested_device"] = options.device.name;
    results["device"] = effective_device.name;
    results["backend"] = backend_to_string(effective_device.backend);
    results["batch_size"] = 1;
    results["tiling_enabled"] = false;
    results["io_binding"]["requested_mode"] =
        std::string(core::io_binding_mode_to_string(options.io_binding_mode));
    results["io_binding"]["eligible"] =
        core::supports_windows_rtx_io_binding(options.model_path, effective_device.backend);
    results["io_binding"]["active"] =
        core::should_enable_io_binding(options.model_path, effective_device.backend,
                                       options.io_binding_mode);
    results["warmup_runs"] = 0;
    results["steady_state_runs"] = options.iterations;
    results["benchmark_runs"] = options.iterations;
    results["avg_latency_ms"] = average_latency_ms;
    results["fps"] =
        total_render_ms > 0.0 ? (1000.0 * static_cast<double>(options.iterations)) / total_render_ms
                              : 0.0;
    results["stage_timings"] = stage_timings_to_json(profiler.snapshot());
    results["phase_timings"] = summarize_stage_groups(profiler.snapshot());
    if (fallback.has_value()) {
        results["fallback"] = to_json(*fallback);
    }

    std::cout << results.dump(4) << std::endl;
    return 0;
}
