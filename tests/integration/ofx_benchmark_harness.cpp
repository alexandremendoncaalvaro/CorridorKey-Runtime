#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
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
#include "frame_io/video_io.hpp"

using namespace corridorkey;
using namespace corridorkey::app;

namespace {

struct HarnessOptions {
    std::filesystem::path model_path =
        std::filesystem::path(PROJECT_ROOT) / "models" / "corridorkey_int8_512.onnx";
    DeviceInfo device = DeviceInfo{"Generic CPU", 0, Backend::CPU};
    int resolution = 512;
    int frame_width = 0;
    int frame_height = 0;
    int iterations = 5;
    core::IoBindingMode io_binding_mode = core::IoBindingMode::Auto;
    // When both video paths are set the harness runs in "video" mode: it
    // decodes pairs of frames from the two clips and drives them into the
    // shared transport before each render. Frame width/height auto-populate
    // from the RGB clip; both clips loop independently if the iteration
    // count exceeds their length. Session remains alive for the full loop
    // so the render path reuses the same underlying ORT session across
    // frames, which is the pattern a real Resolve session exhibits and
    // the pattern the synthetic-black-frame harness cannot easily
    // reproduce (a 20-iteration synthetic pass stops before in-session
    // resource accumulation has time to surface).
    std::filesystem::path input_video_path;
    std::filesystem::path hint_video_path;
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
        if (argument == "--frame-width" && index + 1 < argc) {
            options.frame_width = std::atoi(argv[++index]);
            continue;
        }
        if (argument == "--frame-height" && index + 1 < argc) {
            options.frame_height = std::atoi(argv[++index]);
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
        if (argument == "--input-video" && index + 1 < argc) {
            options.input_video_path = argv[++index];
            continue;
        }
        if (argument == "--hint-video" && index + 1 < argc) {
            options.hint_video_path = argv[++index];
            continue;
        }

        return Unexpected(
            Error{ErrorCode::InvalidParameters, "Unknown OFX harness argument: " + argument});
    }

    const bool has_input = !options.input_video_path.empty();
    const bool has_hint = !options.hint_video_path.empty();
    if (has_input != has_hint) {
        return Unexpected(Error{ErrorCode::InvalidParameters,
                                "--input-video and --hint-video must be provided together."});
    }

    if (options.resolution <= 0) {
        return Unexpected(
            Error{ErrorCode::InvalidParameters, "Resolution must be greater than zero."});
    }
    if (options.frame_width < 0 || options.frame_height < 0) {
        return Unexpected(
            Error{ErrorCode::InvalidParameters, "Frame dimensions must be zero or positive."});
    }
    if (options.iterations <= 0) {
        return Unexpected(
            Error{ErrorCode::InvalidParameters, "Iterations must be greater than zero."});
    }
    if (options.frame_width == 0) {
        options.frame_width = options.resolution;
    }
    if (options.frame_height == 0) {
        options.frame_height = options.resolution;
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

bool has_stage(const std::vector<StageTiming>& timings, std::string_view name) {
    return std::any_of(timings.begin(), timings.end(),
                       [&](const StageTiming& timing) { return timing.name == name; });
}

// Decode the next pair of frames from both video clips into the provided
// transport views. Loops each clip independently on EOF so long harness
// runs (60+ frames against short source clips) still produce fresh inputs
// without exhausting the input file. Returns an error only when a fresh
// reader cannot be opened or a decode fails outright; EOF with a
// successful re-open is treated as normal.
Result<void> feed_next_video_frame_pair(std::unique_ptr<VideoReader>& rgb_reader,
                                        std::unique_ptr<VideoReader>& hint_reader,
                                        const std::filesystem::path& rgb_path,
                                        const std::filesystem::path& hint_path,
                                        Image& transport_rgb, Image& transport_hint) {
    auto reopen = [](std::unique_ptr<VideoReader>& reader,
                     const std::filesystem::path& path) -> Result<void> {
        auto opened = VideoReader::open(path);
        if (!opened) {
            return Unexpected(opened.error());
        }
        reader = std::move(*opened);
        return {};
    };

    auto read_with_loop = [&](std::unique_ptr<VideoReader>& reader,
                              const std::filesystem::path& path) -> Result<VideoFrame> {
        auto frame = reader->read_next_frame();
        if (!frame) {
            return Unexpected(frame.error());
        }
        if (!frame->buffer.view().empty()) {
            return std::move(*frame);
        }
        // EOF -> reopen and read again.
        auto reopen_res = reopen(reader, path);
        if (!reopen_res) {
            return Unexpected(reopen_res.error());
        }
        auto retry = reader->read_next_frame();
        if (!retry) {
            return Unexpected(retry.error());
        }
        if (retry->buffer.view().empty()) {
            return Unexpected(Error{ErrorCode::IoError,
                                    "Reopened video returned EOF on first frame: " + path.string()});
        }
        return std::move(*retry);
    };

    auto rgb_frame = read_with_loop(rgb_reader, rgb_path);
    if (!rgb_frame) {
        return Unexpected(rgb_frame.error());
    }
    auto hint_frame = read_with_loop(hint_reader, hint_path);
    if (!hint_frame) {
        return Unexpected(hint_frame.error());
    }

    // RGB: both the decoded frame and the transport expose 3-channel float32
    // at the same width/height, so copy directly into the mapped view.
    const Image rgb_view = rgb_frame->buffer.view();
    if (rgb_view.width != transport_rgb.width || rgb_view.height != transport_rgb.height ||
        rgb_view.channels != transport_rgb.channels) {
        return Unexpected(Error{
            ErrorCode::InvalidParameters,
            "Input video dimensions do not match transport: rgb_view=" +
                std::to_string(rgb_view.width) + "x" + std::to_string(rgb_view.height) + "x" +
                std::to_string(rgb_view.channels)});
    }
    std::memcpy(transport_rgb.data.data(), rgb_view.data.data(),
                rgb_view.data.size() * sizeof(float));

    // Hint: transport is single-channel; the alpha-hint MP4 is stored as a
    // 3-channel grayscale matte. Project the first channel into the
    // single-channel hint plane.
    const Image hint_view = hint_frame->buffer.view();
    if (hint_view.width != transport_hint.width || hint_view.height != transport_hint.height) {
        return Unexpected(Error{
            ErrorCode::InvalidParameters,
            "Hint video dimensions do not match transport: hint_view=" +
                std::to_string(hint_view.width) + "x" + std::to_string(hint_view.height)});
    }
    const int pixel_count = hint_view.width * hint_view.height;
    const int hint_channels = hint_view.channels;
    for (int i = 0; i < pixel_count; ++i) {
        transport_hint.data[i] = hint_view.data[i * hint_channels];
    }
    return {};
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

    HarnessOptions options = *options_res;
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

    const bool video_mode = !options.input_video_path.empty() && !options.hint_video_path.empty();
    std::unique_ptr<VideoReader> rgb_reader;
    std::unique_ptr<VideoReader> hint_reader;
    if (video_mode) {
        auto rgb_opened = VideoReader::open(options.input_video_path);
        if (!rgb_opened) {
            std::cout << failure_json("Failed to open --input-video " +
                                      options.input_video_path.string() + ": " +
                                      rgb_opened.error().message)
                             .dump(4)
                      << std::endl;
            return 1;
        }
        rgb_reader = std::move(*rgb_opened);
        auto hint_opened = VideoReader::open(options.hint_video_path);
        if (!hint_opened) {
            std::cout << failure_json("Failed to open --hint-video " +
                                      options.hint_video_path.string() + ": " +
                                      hint_opened.error().message)
                             .dump(4)
                      << std::endl;
            return 1;
        }
        hint_reader = std::move(*hint_opened);

        // Force the transport dimensions to match the source clip. The CLI
        // --frame-width/--frame-height flags are intentionally ignored here
        // because any mismatch would produce garbage or an outright
        // dimension error downstream; video mode is deliberately honest
        // about the real input resolution.
        options.frame_width = rgb_reader->width();
        options.frame_height = rgb_reader->height();
        if (hint_reader->width() != options.frame_width ||
            hint_reader->height() != options.frame_height) {
            std::cout << failure_json("--input-video and --hint-video must share the same "
                                      "dimensions; got " +
                                      std::to_string(rgb_reader->width()) + "x" +
                                      std::to_string(rgb_reader->height()) + " and " +
                                      std::to_string(hint_reader->width()) + "x" +
                                      std::to_string(hint_reader->height()))
                             .dump(4)
                      << std::endl;
            return 1;
        }
    }

    const auto transport_path = common::next_ofx_shared_frame_path();
    std::error_code cleanup_error;
    std::filesystem::remove(transport_path, cleanup_error);

    auto transport_res = common::SharedFrameTransport::create(transport_path, options.frame_width,
                                                              options.frame_height);
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
    // Per-iteration timing view so callers can see in-session drift
    // (memory leaks, workspace accumulation, etc.) without having to
    // average across the whole run. Each entry captures the wall-clock
    // roundtrip plus the stage-timing breakdown the broker reports for
    // that single frame.
    std::vector<nlohmann::json> per_frame_timings;
    per_frame_timings.reserve(static_cast<std::size_t>(options.iterations));

    for (int iteration = 0; iteration < options.iterations; ++iteration) {
        if (video_mode) {
            Image transport_rgb = transport.rgb_view();
            Image transport_hint = transport.hint_view();
            auto feed_res = feed_next_video_frame_pair(
                rgb_reader, hint_reader, options.input_video_path, options.hint_video_path,
                transport_rgb, transport_hint);
            if (!feed_res) {
                std::filesystem::remove(transport_path, cleanup_error);
                std::cout << failure_json(feed_res.error().message).dump(4) << std::endl;
                return 1;
            }
        }

        OfxRuntimeRenderFrameRequest render_request;
        render_request.session_id = session_id;
        render_request.shared_frame_path = transport_path;
        render_request.width = options.frame_width;
        render_request.height = options.frame_height;
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

        nlohmann::json frame_entry;
        frame_entry["iteration"] = iteration;
        frame_entry["roundtrip_ms"] = render_latency_ms;
        nlohmann::json frame_stages = nlohmann::json::object();
        for (const auto& timing : render_res->timings) {
            frame_stages[timing.name] = timing.total_ms;
        }
        frame_entry["stages"] = std::move(frame_stages);
        per_frame_timings.push_back(std::move(frame_entry));

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
    const auto stage_timings = profiler.snapshot();

    nlohmann::json results;
    results["success"] = true;
    results["mode"] = video_mode ? "ofx_broker_video" : "ofx_broker_synthetic";
    if (video_mode) {
        results["input_video"] = options.input_video_path.string();
        results["hint_video"] = options.hint_video_path.string();
    }
    results["model"] = options.model_path.filename().string();
    results["artifact"] = options.model_path.filename().string();
    results["artifact_path"] = options.model_path.string();
    results["resolution"] = options.resolution;
    results["frame_width"] = options.frame_width;
    results["frame_height"] = options.frame_height;
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
    results["io_binding"]["active"] = core::should_enable_io_binding(
        options.model_path, effective_device.backend, options.io_binding_mode);
    results["io_binding"]["observed"] = has_stage(stage_timings, "ort_io_binding_bind_inputs");
#if defined(CORRIDORKEY_HAS_CUDA) && CORRIDORKEY_HAS_CUDA
    results["io_binding"]["memory_mode"] = "pinned";
    results["io_binding"]["resize_mode"] = "gpu";
#else
    results["io_binding"]["memory_mode"] = "pageable";
    results["io_binding"]["resize_mode"] = "cpu";
#endif
    results["warmup_runs"] = 0;
    results["steady_state_runs"] = options.iterations;
    results["benchmark_runs"] = options.iterations;
    results["avg_latency_ms"] = average_latency_ms;
    results["fps"] = total_render_ms > 0.0
                         ? (1000.0 * static_cast<double>(options.iterations)) / total_render_ms
                         : 0.0;
    results["stage_timings"] = stage_timings_to_json(stage_timings);
    results["phase_timings"] = summarize_stage_groups(stage_timings);
    results["per_frame_timings"] = std::move(per_frame_timings);
    if (fallback.has_value()) {
        results["fallback"] = to_json(*fallback);
    }

    std::cout << results.dump(4) << std::endl;
    return 0;
}
