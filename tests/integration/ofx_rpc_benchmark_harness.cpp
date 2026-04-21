// RPC-path benchmark: spawns the real runtime server binary the same way
// the OFX plugin does (OfxRuntimeClient) and drives prepare_session +
// render_frame over the JSON + shared-memory transport. Sister to
// ofx_benchmark_harness.cpp, which exercises the OfxSessionBroker
// in-process. The gap between the two isolates regressions that live in
// the IPC layer (spawn, JSON protocol, mmap transport) from regressions
// that live in the broker/engine/session itself.
//
// Usage:
//   ofx_rpc_benchmark_harness \
//     --server-binary <path-to-corridorkey> \
//     --model <path-to-model> \
//     --device mlx \
//     --resolution 1024 \
//     --frame-width 1920 --frame-height 1080 \
//     --iterations 20 \
//     --endpoint-port 46001
//
// Output: JSON on stdout with avg_latency_ms, fps, and per_frame_timings
// (iteration, roundtrip_ms, stages{...}) so automated runs can graph the
// in-session drift that was the smoking gun for the 0.7.5 -> 0.7.6 MLX
// regression (per-frame MLX compute climbed from seconds to tens of
// seconds under the Resolve-spawned server).

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <numeric>
#include <string>
#include <vector>

#if defined(__APPLE__)
#include <pthread.h>
#include <sys/qos.h>
#endif

#include <corridorkey/engine.hpp>
#include <corridorkey/types.hpp>
#include <nlohmann/json.hpp>

#include "app/ofx_runtime_protocol.hpp"
#include "app/runtime_contracts.hpp"
#include "common/local_ipc.hpp"
#include "common/stage_profiler.hpp"
#include "plugins/ofx/ofx_runtime_client.hpp"

using namespace corridorkey;

namespace {

struct HarnessOptions {
    std::filesystem::path server_binary;
    std::filesystem::path model_path;
    std::string device_name = "mlx";
    int resolution = 1024;
    int frame_width = 1920;
    int frame_height = 1080;
    int iterations = 20;
    std::uint16_t endpoint_port = 46001;
    int prepare_timeout_ms = 120000;
    int request_timeout_ms = 180000;
    int idle_timeout_ms = 300000;
    int launch_timeout_ms = 15000;
    bool keep_server = false;
    // macOS-only: QoS class to apply to the harness (and therefore inherit into
    // the spawned runtime server, absent a fix) before launching. Values:
    // "user-initiated" (default), "utility", "background". This reproduces the
    // Resolve render-action thread's QoS context used to diagnose the
    // v0.7.6-mac.1 per-frame slowdown.
    std::string parent_qos_class = "user-initiated";
    // Input-content mode. "constant" fills with a fixed green-screen tone
    // (the legacy synthetic harness behavior, and equivalent to the zero-fill
    // the CLI `benchmark` subcommand uses). "random" regenerates a PRNG
    // image per iteration so MLX cannot exploit identical-input paths or
    // constant-tensor optimizations. Use "random" to produce a
    // content-varying steady-state baseline comparable to real Resolve
    // traffic; the gap between constant and random exposes how much of
    // the documented ~287 ms MLX floor is a degenerate-input artifact.
    std::string input_mode = "constant";
    // Optional PRNG seed for --input-mode=random so runs are reproducible.
    std::uint32_t input_random_seed = 0xC0B7A1C0u;
};

Result<HarnessOptions> parse_arguments(int argc, char* argv[]) {
    HarnessOptions options;
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        auto need = [&](const char* name) -> Result<std::string> {
            if (index + 1 >= argc) {
                return Unexpected<Error>(
                    Error{ErrorCode::InvalidParameters, std::string("missing value for ") + name});
            }
            return std::string(argv[++index]);
        };
        if (argument == "--server-binary") {
            auto value = need("--server-binary");
            if (!value) return Unexpected(value.error());
            options.server_binary = *value;
        } else if (argument == "--model") {
            auto value = need("--model");
            if (!value) return Unexpected(value.error());
            options.model_path = *value;
        } else if (argument == "--device") {
            auto value = need("--device");
            if (!value) return Unexpected(value.error());
            options.device_name = *value;
        } else if (argument == "--resolution") {
            auto value = need("--resolution");
            if (!value) return Unexpected(value.error());
            options.resolution = std::atoi(value->c_str());
        } else if (argument == "--frame-width") {
            auto value = need("--frame-width");
            if (!value) return Unexpected(value.error());
            options.frame_width = std::atoi(value->c_str());
        } else if (argument == "--frame-height") {
            auto value = need("--frame-height");
            if (!value) return Unexpected(value.error());
            options.frame_height = std::atoi(value->c_str());
        } else if (argument == "--iterations") {
            auto value = need("--iterations");
            if (!value) return Unexpected(value.error());
            options.iterations = std::atoi(value->c_str());
        } else if (argument == "--endpoint-port") {
            auto value = need("--endpoint-port");
            if (!value) return Unexpected(value.error());
            options.endpoint_port = static_cast<std::uint16_t>(std::atoi(value->c_str()));
        } else if (argument == "--prepare-timeout-ms") {
            auto value = need("--prepare-timeout-ms");
            if (!value) return Unexpected(value.error());
            options.prepare_timeout_ms = std::atoi(value->c_str());
        } else if (argument == "--request-timeout-ms") {
            auto value = need("--request-timeout-ms");
            if (!value) return Unexpected(value.error());
            options.request_timeout_ms = std::atoi(value->c_str());
        } else if (argument == "--keep-server") {
            options.keep_server = true;
        } else if (argument == "--parent-qos-class") {
            auto value = need("--parent-qos-class");
            if (!value) return Unexpected(value.error());
            options.parent_qos_class = *value;
        } else if (argument == "--input-mode") {
            auto value = need("--input-mode");
            if (!value) return Unexpected(value.error());
            options.input_mode = *value;
        } else if (argument == "--input-seed") {
            auto value = need("--input-seed");
            if (!value) return Unexpected(value.error());
            options.input_random_seed =
                static_cast<std::uint32_t>(std::strtoul(value->c_str(), nullptr, 10));
        } else {
            return Unexpected<Error>(
                Error{ErrorCode::InvalidParameters, "Unknown argument: " + argument});
        }
    }
    if (options.server_binary.empty()) {
        return Unexpected<Error>(
            Error{ErrorCode::InvalidParameters, "--server-binary is required"});
    }
    if (options.model_path.empty()) {
        return Unexpected<Error>(Error{ErrorCode::InvalidParameters, "--model is required"});
    }
    return options;
}

DeviceInfo device_from_name(const std::string& name) {
    if (name == "mlx") {
        return DeviceInfo{"Apple Silicon MLX", 0, Backend::MLX};
    }
    if (name == "cpu") {
        return DeviceInfo{"Generic CPU", 0, Backend::CPU};
    }
    if (name == "coreml") {
        return DeviceInfo{"Apple CoreML", 0, Backend::CoreML};
    }
    if (name == "auto") {
        return DeviceInfo{"Auto", 0, Backend::Auto};
    }
    return DeviceInfo{name, 0, Backend::Auto};
}

ImageBuffer make_filled_rgb(int width, int height) {
    ImageBuffer buffer(width, height, 3);
    auto data = buffer.view().data;
    for (std::size_t i = 0; i < data.size(); i += 3) {
        data[i + 0] = 0.15f;  // r
        data[i + 1] = 0.65f;  // g (green-screen mid tone)
        data[i + 2] = 0.10f;  // b
    }
    return buffer;
}

ImageBuffer make_filled_hint(int width, int height) {
    ImageBuffer buffer(width, height, 1);
    std::fill(buffer.view().data.begin(), buffer.view().data.end(), 0.5f);
    return buffer;
}

// xorshift32 PRNG, inline. We don't use std::mt19937 so that the per-pixel
// fill stays cheap enough that input generation isn't the dominant cost when
// benchmarking a 1920x1080 frame at 30+ iterations.
inline std::uint32_t xorshift32(std::uint32_t& state) {
    std::uint32_t x = state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    state = x;
    return x;
}

void fill_random_rgb(ImageBuffer& buffer, std::uint32_t seed) {
    auto data = buffer.view().data;
    std::uint32_t state = seed ? seed : 0x9E3779B9u;
    const float inv = 1.0f / static_cast<float>(0xFFFFFFFFu);
    for (std::size_t i = 0; i < data.size(); ++i) {
        data[i] = static_cast<float>(xorshift32(state)) * inv;
    }
}

void fill_random_hint(ImageBuffer& buffer, std::uint32_t seed) {
    auto data = buffer.view().data;
    std::uint32_t state = seed ? seed ^ 0xDEADBEEFu : 0x2A2A2A2Au;
    const float inv = 1.0f / static_cast<float>(0xFFFFFFFFu);
    for (std::size_t i = 0; i < data.size(); ++i) {
        data[i] = static_cast<float>(xorshift32(state)) * inv;
    }
}

nlohmann::json failure_json(const std::string& message) {
    return nlohmann::json{{"success", false}, {"error", message}};
}

}  // namespace

int main(int argc, char* argv[]) {
    auto options_res = parse_arguments(argc, argv);
    if (!options_res) {
        std::cout << failure_json(options_res.error().message).dump(4) << std::endl;
        return 1;
    }
    HarnessOptions options = *options_res;

#if defined(__APPLE__)
    // Apply the requested parent QoS class BEFORE calling OfxRuntimeClient::create
    // (which ultimately triggers posix_spawn of the server binary). With the
    // v0.7.6-mac.1 regression in place, a parent QoS of "utility" or
    // "background" forces the server to inherit that QoS and slows MLX
    // per-frame latency 5-50x. With the fix in place, the server overrides
    // the inherited QoS at spawn time AND at startup, and latency must stay
    // within the normal budget regardless of this flag.
    if (options.parent_qos_class == "background") {
        pthread_set_qos_class_self_np(QOS_CLASS_BACKGROUND, 0);
    } else if (options.parent_qos_class == "utility") {
        pthread_set_qos_class_self_np(QOS_CLASS_UTILITY, 0);
    } else if (options.parent_qos_class == "user-initiated") {
        pthread_set_qos_class_self_np(QOS_CLASS_USER_INITIATED, 0);
    } else if (options.parent_qos_class == "default") {
        pthread_set_qos_class_self_np(QOS_CLASS_DEFAULT, 0);
    } else {
        std::cout
            << failure_json("unknown --parent-qos-class value: " + options.parent_qos_class).dump(4)
            << std::endl;
        return 1;
    }
#endif

    if (!std::filesystem::exists(options.server_binary)) {
        std::cout
            << failure_json("server binary not found: " + options.server_binary.string()).dump(4)
            << std::endl;
        return 1;
    }

    ofx::OfxRuntimeClientOptions client_options;
    client_options.endpoint.host = "127.0.0.1";
    client_options.endpoint.port = options.endpoint_port;
    client_options.server_binary = options.server_binary;
    client_options.request_timeout_ms = options.request_timeout_ms;
    client_options.prepare_timeout_ms = options.prepare_timeout_ms;
    client_options.launch_timeout_ms = options.launch_timeout_ms;
    client_options.idle_timeout_ms = options.idle_timeout_ms;

    auto client_res = ofx::OfxRuntimeClient::create(client_options);
    if (!client_res) {
        std::cout << failure_json(client_res.error().message).dump(4) << std::endl;
        return 1;
    }
    auto& client = *client_res;

    app::OfxRuntimePrepareSessionRequest prepare;
    prepare.client_instance_id = "rpc_benchmark";
    prepare.model_path = options.model_path;
    prepare.artifact_name = options.model_path.filename().string();
    prepare.requested_device = device_from_name(options.device_name);
    prepare.requested_quality_mode = 1;
    prepare.requested_resolution = options.resolution;
    prepare.effective_resolution = options.resolution;
    prepare.engine_options.allow_cpu_fallback = true;

    common::StageProfiler profiler;
    auto prepare_start = std::chrono::steady_clock::now();
    auto prepare_response =
        client->prepare_session(prepare, [&](const StageTiming& t) { profiler.record(t); });
    auto prepare_end = std::chrono::steady_clock::now();
    if (!prepare_response) {
        std::cout << failure_json(prepare_response.error().message).dump(4) << std::endl;
        return 1;
    }
    profiler.record("rpc_prepare_session",
                    std::chrono::duration<double, std::milli>(prepare_end - prepare_start).count(),
                    1);

    if (options.input_mode != "constant" && options.input_mode != "random") {
        std::cout << failure_json("unknown --input-mode: " + options.input_mode).dump(4)
                  << std::endl;
        return 1;
    }

    auto rgb_buffer = make_filled_rgb(options.frame_width, options.frame_height);
    auto hint_buffer = make_filled_hint(options.frame_width, options.frame_height);

    InferenceParams params;
    params.target_resolution = options.resolution;
    params.batch_size = 1;
    params.output_alpha_only = false;

    std::vector<nlohmann::json> per_frame;
    per_frame.reserve(static_cast<std::size_t>(options.iterations));
    std::vector<double> latencies;
    latencies.reserve(static_cast<std::size_t>(options.iterations));

    for (int iteration = 0; iteration < options.iterations; ++iteration) {
        if (options.input_mode == "random") {
            // Derive a per-iteration seed so every frame is a distinct
            // noise pattern. This blocks any MLX constant-tensor fast path
            // and forces real per-frame compute.
            const std::uint32_t iter_seed =
                options.input_random_seed + static_cast<std::uint32_t>(iteration) * 0x9E3779B9u;
            fill_random_rgb(rgb_buffer, iter_seed);
            fill_random_hint(hint_buffer, iter_seed);
        }
        std::vector<StageTiming> iter_stages;
        auto start = std::chrono::steady_clock::now();
        auto frame_res = client->process_frame(rgb_buffer.view(), hint_buffer.view(), params,
                                               static_cast<std::uint64_t>(iteration),
                                               [&](const StageTiming& timing) {
                                                   iter_stages.push_back(timing);
                                                   profiler.record(timing);
                                               });
        auto end = std::chrono::steady_clock::now();
        if (!frame_res) {
            std::cout << failure_json(frame_res.error().message).dump(4) << std::endl;
            return 1;
        }
        const double roundtrip = std::chrono::duration<double, std::milli>(end - start).count();
        latencies.push_back(roundtrip);
        profiler.record("rpc_render_roundtrip", roundtrip, 1);

        nlohmann::json entry;
        entry["iteration"] = iteration;
        entry["roundtrip_ms"] = roundtrip;
        nlohmann::json stages = nlohmann::json::object();
        for (const auto& t : iter_stages) {
            stages[t.name] = t.total_ms;
        }
        entry["stages"] = std::move(stages);
        per_frame.push_back(std::move(entry));
    }

    if (!options.keep_server) {
        auto release = client->release_session();
        if (!release) {
            // Not fatal for the benchmark -- server may already be gone.
            std::cerr << "[warn] release_session: " << release.error().message << std::endl;
        }
    }

    nlohmann::json report;
    report["success"] = true;
    report["server_binary"] = options.server_binary.string();
    report["model"] = options.model_path.string();
    report["device"] = options.device_name;
    report["resolution"] = options.resolution;
    report["frame_width"] = options.frame_width;
    report["frame_height"] = options.frame_height;
    report["iterations"] = options.iterations;
    report["endpoint_port"] = options.endpoint_port;
    report["parent_qos_class"] = options.parent_qos_class;
    report["input_mode"] = options.input_mode;
    if (options.input_mode == "random") {
        report["input_random_seed"] = options.input_random_seed;
    }
    if (!latencies.empty()) {
        const double sum = std::accumulate(latencies.begin(), latencies.end(), 0.0);
        const double avg = sum / static_cast<double>(latencies.size());
        report["avg_latency_ms"] = avg;
        report["fps"] = avg > 0.0 ? 1000.0 / avg : 0.0;
    }
    report["per_frame_timings"] = per_frame;

    nlohmann::json stage_summary = nlohmann::json::array();
    for (const auto& entry : profiler.snapshot()) {
        nlohmann::json obj;
        obj["name"] = entry.name;
        obj["total_ms"] = entry.total_ms;
        obj["sample_count"] = entry.sample_count;
        obj["work_units"] = entry.work_units;
        obj["avg_ms"] =
            entry.sample_count > 0 ? entry.total_ms / static_cast<double>(entry.sample_count) : 0.0;
        stage_summary.push_back(std::move(obj));
    }
    report["stage_timings"] = std::move(stage_summary);

    std::cout << report.dump(4) << std::endl;
    return 0;
}
