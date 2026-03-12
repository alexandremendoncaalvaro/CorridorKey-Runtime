#include <cpr/cpr.h>

#include <cctype>
#include <corridorkey/engine.hpp>
#include <corridorkey/version.hpp>
#include <cstdio>
#include <cxxopts.hpp>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <string_view>
#include <vector>

#include "../app/hardware_profile.hpp"
#include "../app/job_orchestrator.hpp"
#include "../app/runtime_contracts.hpp"
#include "device_selection.hpp"

using namespace corridorkey;
using namespace corridorkey::app;

namespace {

std::string backend_to_string_local(Backend backend) {
    switch (backend) {
        case Backend::CPU:
            return "cpu";
        case Backend::CoreML:
            return "coreml";
        case Backend::CUDA:
            return "cuda";
        case Backend::TensorRT:
            return "tensorrt";
        case Backend::DirectML:
            return "dml";
        case Backend::MLX:
            return "mlx";
        default:
            return "auto";
    }
}

nlohmann::json event_to_json(const JobEvent& event) {
    nlohmann::json json;

    switch (event.type) {
        case JobEventType::JobStarted:
            json["type"] = "job_started";
            break;
        case JobEventType::BackendSelected:
            json["type"] = "backend_selected";
            break;
        case JobEventType::Progress:
            json["type"] = "progress";
            break;
        case JobEventType::Warning:
            json["type"] = "warning";
            break;
        case JobEventType::ArtifactWritten:
            json["type"] = "artifact_written";
            break;
        case JobEventType::Completed:
            json["type"] = "completed";
            break;
        case JobEventType::Failed:
            json["type"] = "failed";
            break;
        case JobEventType::Cancelled:
            json["type"] = "cancelled";
            break;
    }

    json["phase"] = event.phase;
    json["progress"] = event.progress;
    if (event.backend != Backend::Auto) {
        json["backend"] = backend_to_string_local(event.backend);
    }
    if (!event.message.empty()) {
        json["message"] = event.message;
    }
    if (!event.artifact_path.empty()) {
        json["artifact_path"] = event.artifact_path;
    }
    if (event.error.has_value()) {
        json["error"]["code"] = static_cast<int>(event.error->code);
        json["error"]["message"] = event.error->message;
    }
    if (event.fallback.has_value()) {
        json["fallback"]["requested_backend"] =
            backend_to_string_local(event.fallback->requested_backend);
        json["fallback"]["selected_backend"] =
            backend_to_string_local(event.fallback->selected_backend);
        json["fallback"]["reason"] = event.fallback->reason;
    }
    if (!event.timings.empty()) {
        json["timings"] = nlohmann::json::array();
        for (const auto& timing : event.timings) {
            nlohmann::json timing_json;
            timing_json["name"] = timing.name;
            timing_json["total_ms"] = timing.total_ms;
            timing_json["sample_count"] = timing.sample_count;
            timing_json["work_units"] = timing.work_units;
            timing_json["avg_ms"] =
                timing.sample_count > 0 ? timing.total_ms / timing.sample_count : 0.0;
            if (timing.work_units > 0) {
                timing_json["ms_per_unit"] = timing.total_ms / timing.work_units;
            }
            json["timings"].push_back(std::move(timing_json));
        }
    }

    return json;
}

bool option_present(int argc, char* argv[], std::initializer_list<std::string_view> option_names) {
    for (int index = 1; index < argc; ++index) {
        std::string_view token(argv[index]);
        for (std::string_view name : option_names) {
            if (token == name) {
                return true;
            }
            if (token.size() > name.size() && token.substr(0, name.size()) == name &&
                token[name.size()] == '=') {
                return true;
            }
        }
    }

    return false;
}

std::vector<std::string> positional_args(const cxxopts::ParseResult& result) {
    if (!result.count("args")) {
        return {};
    }
    return result["args"].as<std::vector<std::string>>();
}

std::optional<std::string> selected_preset_selector(const cxxopts::ParseResult& result) {
    if (result.count("quality")) {
        return result["quality"].as<std::string>();
    }
    if (result.count("preset")) {
        return result["preset"].as<std::string>();
    }
    return std::nullopt;
}

InferenceParams build_inference_params(const cxxopts::ParseResult& result,
                                       const std::optional<InferenceParams>& base_params, int argc,
                                       char* argv[]) {
    InferenceParams params = base_params.value_or(InferenceParams{});

    if (!base_params.has_value() || option_present(argc, argv, {"--resolution", "-r"})) {
        params.target_resolution = result["resolution"].as<int>();
    }
    if (!base_params.has_value() || option_present(argc, argv, {"--despill"})) {
        params.despill_strength = result["despill"].as<float>();
    }
    if (!base_params.has_value() || option_present(argc, argv, {"--batch-size"})) {
        params.batch_size = result["batch-size"].as<int>();
    }
    if (option_present(argc, argv, {"--despeckle"})) {
        params.auto_despeckle = true;
    }
    if (option_present(argc, argv, {"--tiled"})) {
        params.enable_tiling = true;
    }

    return params;
}

std::filesystem::path default_models_dir() {
    return "models";
}

bool is_video_path(const std::filesystem::path& path) {
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);
    return extension == ".mp4" || extension == ".mov" || extension == ".avi" || extension == ".mkv";
}

std::filesystem::path default_output_path_for_input(const std::filesystem::path& input_path) {
    std::filesystem::path outputs_root = "outputs";

    if (std::filesystem::is_directory(input_path)) {
        auto name = input_path.filename().string();
        if (name.empty()) {
            name = "sequence";
        }
        return outputs_root / (name + "_out");
    }

    if (is_video_path(input_path)) {
        return outputs_root /
               (input_path.stem().string() + "_out" + input_path.extension().string());
    }

    return outputs_root / input_path.stem();
}

struct ResolvedExecution {
    std::filesystem::path models_dir = {};
    std::filesystem::path model_path = {};
    std::optional<PresetDefinition> preset = std::nullopt;
    InferenceParams params = {};
    bool default_model_selected = false;
    bool default_output_selected = false;
};

Result<ResolvedExecution> resolve_execution_defaults(const cxxopts::ParseResult& result, int argc,
                                                     char* argv[], const DeviceInfo& device,
                                                     bool requires_output) {
    ResolvedExecution resolved;
    resolved.models_dir = default_models_dir();
    if (result.count("model")) {
        resolved.model_path = result["model"].as<std::string>();
        auto parent = resolved.model_path.parent_path();
        if (!parent.empty()) {
            resolved.models_dir = parent;
        }
    }

    auto capabilities = runtime_capabilities();
    if (auto preset_selector = selected_preset_selector(result); preset_selector.has_value()) {
        resolved.preset = find_preset_by_selector(*preset_selector);
        if (!resolved.preset.has_value()) {
            return Unexpected<Error>{
                Error{ErrorCode::InvalidParameters, "Unknown preset: " + *preset_selector}};
        }
    } else if (!result.count("model")) {
        resolved.preset = default_preset_for_capabilities(capabilities);
    }

    if (result.count("model")) {
        resolved.params = build_inference_params(result, std::nullopt, argc, argv);
    } else {
        resolved.params = build_inference_params(
            result,
            resolved.preset.has_value() ? std::optional<InferenceParams>{resolved.preset->params}
                                        : std::nullopt,
            argc, argv);

        auto selected_model =
            default_model_for_request(capabilities, device.backend, resolved.preset);
        if (!selected_model.has_value()) {
            return Unexpected<Error>{Error{ErrorCode::ModelLoadFailed,
                                           "Could not resolve a default model for this device."}};
        }

        resolved.model_path = resolved.models_dir / selected_model->filename;
        if (!std::filesystem::exists(resolved.model_path)) {
            auto cpu_fallback = resolved.models_dir / "corridorkey_int8_512.onnx";
            if (resolved.model_path.filename() != "corridorkey_int8_512.onnx" &&
                std::filesystem::exists(cpu_fallback)) {
                resolved.model_path = cpu_fallback;
            } else {
                return Unexpected<Error>{
                    Error{ErrorCode::ModelLoadFailed,
                          "Default model pack not found: " + resolved.model_path.string()}};
            }
        }
        resolved.default_model_selected = true;
    }

    if (requires_output && !result.count("output")) {
        resolved.default_output_selected = true;
    }

    return resolved;
}

void print_stage_timings(const nlohmann::json& timings) {
    if (!timings.is_array() || timings.empty()) {
        return;
    }

    std::cout << "Stage timings:\n";
    for (const auto& timing : timings) {
        std::cout << " - " << timing["name"].get<std::string>() << ": "
                  << timing["total_ms"].get<double>() << " ms";
        if (timing.contains("avg_ms")) {
            std::cout << " (avg " << timing["avg_ms"].get<double>() << " ms";
            if (timing.contains("sample_count")) {
                std::cout << " over " << timing["sample_count"].get<std::uint64_t>() << " samples";
            }
            std::cout << ")";
        }
        if (timing.contains("ms_per_unit")) {
            std::cout << ", " << timing["ms_per_unit"].get<double>() << " ms/unit";
        }
        std::cout << "\n";
    }
}

}  // namespace

void print_info() {
    auto info = JobOrchestrator::get_system_info();
    std::cout << "CorridorKey Runtime v" << CORRIDORKEY_VERSION_STRING << "\n";
    std::cout << "------------------------------------------\n";
    std::cout << "Detected Hardware Devices:\n";
    for (const auto& d : info["devices"]) {
        std::cout << " - " << std::left << std::setw(30) << d["name"].get<std::string>() << " ["
                  << d["backend"].get<std::string>() << "] "
                  << (d["memory_mb"].get<int64_t>() > 0
                          ? (std::to_string(d["memory_mb"].get<int64_t>()) + " MB")
                          : "")
                  << "\n";
    }

    std::cout << "Capabilities:\n"
              << " - CoreML: "
              << (info["capabilities"]["coreml_available"].get<bool>() ? "yes" : "no") << "\n"
              << " - MLX probe: "
              << (info["capabilities"]["mlx_probe_available"].get<bool>() ? "yes" : "no") << "\n"
              << " - CPU fallback: "
              << (info["capabilities"]["cpu_fallback_available"].get<bool>() ? "yes" : "no") << "\n"
              << " - VideoToolbox: "
              << (info["capabilities"]["videotoolbox_available"].get<bool>() ? "yes" : "no") << "\n"
              << " - Default video encoder: "
              << info["capabilities"]["default_video_encoder"].get<std::string>() << "\n";
}

int main(int argc, char* argv[]) {
    cxxopts::Options options("corridorkey",
                             "CorridorKey Runtime - High-performance neural green screen keyer");

    options.add_options()(
        "command",
        "Sub-command to run (info, process, download, doctor, benchmark, models, presets)",
        cxxopts::value<std::string>())("args", "Positional arguments",
                                       cxxopts::value<std::vector<std::string>>())(
        "i,input", "Input video or directory of frames", cxxopts::value<std::string>())(
        "a,alpha-hint", "Alpha hint video or directory (optional)", cxxopts::value<std::string>())(
        "o,output", "Output directory or file (optional for process/benchmark)",
        cxxopts::value<std::string>())("m,model",
                                       "Path to model pack or exported artifact (advanced)",
                                       cxxopts::value<std::string>())(
        "preset", "Preset (preview, balanced, max)", cxxopts::value<std::string>())(
        "quality", "Alias for --preset", cxxopts::value<std::string>())(
        "r,resolution", "Resolution (0=auto, 512, 768, 1024)",
        cxxopts::value<int>()->default_value("0"))(
        "d,device", "Device (auto, cpu, mlx, coreml, cuda, dml)",
        cxxopts::value<std::string>()->default_value("auto"))(
        "variant", "ONNX model variant (int8, fp16, fp32)", cxxopts::value<std::string>())(
        "batch-size", "Number of frames to process in a single GPU call",
        cxxopts::value<int>()->default_value("1"))("despill", "Green spill removal (0.0-1.0)",
                                                   cxxopts::value<float>()->default_value("1.0"))(
        "despeckle", "Enable morphological cleanup")("tiled", "Enable tiling for high-res (4K+)")(
        "json", "Output results in JSON format")("v,version", "Print version")(
        "h,help", "Print detailed help");

    options.parse_positional({"command", "args"});
    options.positional_help("command [input] [output]");

    if (argc <= 1) {
        std::cout << "==========================================\n"
                  << "      CorridorKey Runtime v" << CORRIDORKEY_VERSION_STRING << "\n"
                  << "==========================================\n\n"
                  << "Quick start:\n\n"
                  << "1. Check the runtime:\n"
                  << "   corridorkey doctor\n\n"
                  << "2. Process a video with the default Apple Silicon path:\n"
                  << "   corridorkey process input.mp4 output.mp4\n\n"
                  << "3. Use a simpler or stronger preset when needed:\n"
                  << "   corridorkey process input.mp4 output.mp4 --preset preview\n"
                  << "   corridorkey process input.mp4 output.mp4 --preset max\n\n"
                  << "4. Inspect validated models and presets:\n"
                  << "   corridorkey models\n"
                  << "   corridorkey presets\n\n"
                  << "Run 'corridorkey --help' for all options.\n"
                  << std::endl;
        return 0;
    }

    try {
        auto result = options.parse(argc, argv);
        bool use_json = result.count("json");

        if (use_json) {
#if defined(_WIN32)
            std::freopen("NUL", "a", stderr);
#else
            std::freopen("/dev/null", "a", stderr);
#endif
        }

        if (result.count("help")) {
            std::cout << options.help() << std::endl;
            return 0;
        }

        if (result.count("version")) {
            if (use_json) {
                std::cout << nlohmann::json({{"version", CORRIDORKEY_VERSION_STRING}}).dump()
                          << std::endl;
            } else {
                std::cout << "CorridorKey Runtime v" << CORRIDORKEY_VERSION_STRING << std::endl;
            }
            return 0;
        }

        if (!result.count("command")) {
            std::cout
                << "Error: No command specified. Use 'info', 'download', 'process', 'doctor', "
                   "'benchmark', 'models', or 'presets'."
                << std::endl;
            return 1;
        }

        std::string cmd = result["command"].as<std::string>();
        auto args = positional_args(result);

        if (cmd == "info") {
            if (use_json) {
                std::cout << JobOrchestrator::get_system_info().dump(4) << std::endl;
            } else {
                print_info();
            }
            return 0;
        }

        if (cmd == "doctor") {
            std::filesystem::path models_dir = default_models_dir();
            if (result.count("model")) {
                models_dir = std::filesystem::path(result["model"].as<std::string>()).parent_path();
            }
            auto report = JobOrchestrator::run_doctor(models_dir);
            if (use_json) {
                std::cout << report.dump(4) << std::endl;
            } else {
                std::cout << "--- CorridorKey Doctor Report ---\n"
                          << "Version: " << report["system"]["version"].get<std::string>() << "\n"
                          << "Detected Devices: " << report["system"]["devices"].size() << "\n";
                for (const auto& m : report["models"]) {
                    if (m["found"].get<bool>()) {
                        std::cout << " [OK] Model: " << m["variant"].get<std::string>() << "_"
                                  << m["resolution"].get<int>() << "\n";
                    }
                }
            }
            return 0;
        }

        if (cmd == "models") {
            auto models = JobOrchestrator::list_models();
            if (use_json) {
                std::cout << models.dump(4) << std::endl;
            } else {
                std::cout << "--- Model Catalog ---\n";
                for (const auto& model : models) {
                    std::cout << " - " << model["filename"].get<std::string>();
                    if (model["validated_for_macos"].get<bool>()) {
                        std::cout << " [validated-macos]";
                    }
                    if (model["packaged_for_macos"].get<bool>()) {
                        std::cout << " [packaged]";
                    }
                    std::cout << "\n   " << model["description"].get<std::string>() << "\n";
                }
            }
            return 0;
        }

        if (cmd == "presets") {
            auto presets = JobOrchestrator::list_presets();
            if (use_json) {
                std::cout << presets.dump(4) << std::endl;
            } else {
                std::cout << "--- Presets ---\n";
                for (const auto& preset : presets) {
                    std::cout << " - " << preset["name"].get<std::string>();
                    if (preset["default_for_macos"].get<bool>()) {
                        std::cout << " [default-macos]";
                    }
                    std::cout << "\n   " << preset["description"].get<std::string>() << "\n";
                }
            }
            return 0;
        }

        if (cmd == "benchmark") {
            if (args.size() > 1) {
                std::cout << "Error: 'benchmark' accepts at most one positional input path."
                          << std::endl;
                return 1;
            }

            auto devices = list_devices();
            std::string device_str = result["device"].as<std::string>();
            DeviceInfo device = cli::select_device(devices, device_str);

            std::filesystem::path input_path =
                result.count("input") ? std::filesystem::path(result["input"].as<std::string>())
                                      : (args.empty() ? std::filesystem::path{}
                                                      : std::filesystem::path(args.front()));
            auto resolved = resolve_execution_defaults(result, argc, argv, device, false);
            if (!resolved) {
                std::cerr << "Error: " << resolved.error().message << std::endl;
                return 1;
            }

            JobRequest benchmark_request;
            benchmark_request.model_path = resolved->model_path;
            benchmark_request.device = device;
            benchmark_request.params = resolved->params;
            if (!input_path.empty()) {
                benchmark_request.input_path = input_path;
            }
            if (result.count("alpha-hint")) {
                benchmark_request.hint_path = result["alpha-hint"].as<std::string>();
            }
            if (result.count("output")) {
                benchmark_request.output_path = result["output"].as<std::string>();
            }

            auto report = JobOrchestrator::run_benchmark(benchmark_request);
            if (report.contains("error")) {
                if (use_json) {
                    std::cout << report.dump(4) << std::endl;
                } else {
                    std::cerr << "Benchmark error: " << report["error"].get<std::string>()
                              << std::endl;
                }
                return 1;
            }
            if (use_json) {
                std::cout << report.dump(4) << std::endl;
            } else {
                std::cout << "--- Benchmark Results ---\n"
                          << "Model: " << benchmark_request.model_path.filename().string() << "\n";
                if (resolved->preset.has_value()) {
                    std::cout << "Preset: " << resolved->preset->name << "\n";
                }
                std::cout << "Requested device: " << report["requested_device"].get<std::string>()
                          << "\n"
                          << "Backend: " << report["backend"].get<std::string>() << "\n";
                std::cout << "Mode: " << report["mode"].get<std::string>() << "\n"
                          << "Device: "
                          << (report.contains("device")
                                  ? report["device"].get<std::string>()
                                  : report["requested_device"].get<std::string>())
                          << "\n";
                if (report["mode"] == "synthetic") {
                    std::cout << "Resolution: " << report["resolution"].get<int>() << "x"
                              << report["resolution"].get<int>() << "\n"
                              << "Avg Latency: " << report["avg_latency_ms"].get<double>()
                              << " ms\n"
                              << "Estimated FPS: " << report["fps"].get<double>() << "\n";
                } else {
                    std::cout << "Input: " << report["input"].get<std::string>() << "\n"
                              << "Total Duration: " << report["total_duration_ms"].get<double>()
                              << " ms\n";
                    if (report.contains("processed_units")) {
                        std::cout << "Processed Units: "
                                  << report["processed_units"].get<std::uint64_t>() << "\n"
                                  << "Throughput: "
                                  << report["throughput_units_per_second"].get<double>()
                                  << " units/s\n";
                    }
                    if (report.contains("output_path")) {
                        std::cout << "Output: " << report["output_path"].get<std::string>() << "\n";
                    }
                }
                print_stage_timings(report["stage_timings"]);
            }
            return 0;
        }

        if (cmd == "download") {
            std::string variant =
                result.count("variant") ? result["variant"].as<std::string>() : "int8";
            std::vector<std::string> variants_to_download;

            if (variant == "all") {
                variants_to_download = {"int8", "fp16", "fp32"};
            } else if (variant == "int8" || variant == "fp16" || variant == "fp32") {
                variants_to_download.push_back(variant);
            } else {
                std::cerr << "Error: Invalid variant. Allowed values are 'int8', 'fp16', 'fp32', "
                             "or 'all'."
                          << std::endl;
                return 1;
            }

            std::filesystem::create_directory("models");

            const std::vector<int> available_resolutions = {512, 768, 1024};

            for (const auto& v : variants_to_download) {
                for (const int resolution : available_resolutions) {
                    std::string filename =
                        "corridorkey_" + v + "_" + std::to_string(resolution) + ".onnx";
                    std::filesystem::path output_path = std::filesystem::path("models") / filename;
                    std::string url =
                        "https://huggingface.co/corridorkey/models/resolve/main/" + filename;

                    std::cout << "Downloading " << filename << "..." << std::endl;
                    std::ofstream of(output_path, std::ios::binary);

                    cpr::Response r = cpr::Download(
                        of, cpr::Url{url},
                        cpr::ProgressCallback([](size_t downloadTotal, size_t downloadNow, size_t,
                                                 size_t, intptr_t) -> bool {
                            if (downloadTotal > 0) {
                                float p = static_cast<float>(downloadNow) / downloadTotal;
                                int bar_width = 50;
                                std::cout << "\r[" << std::string(bar_width * p, '=')
                                          << std::string(bar_width * (1 - p), ' ') << "] "
                                          << int(p * 100.0) << "% " << std::flush;
                            }
                            return true;
                        }));

                    std::cout << std::endl;
                    of.close();

                    if (r.status_code == 200) {
                        std::cout << "Successfully downloaded " << filename << " to models/"
                                  << std::endl;
                    } else {
                        std::cerr << "Failed to download " << filename
                                  << ". HTTP Status: " << r.status_code << std::endl;
                        if (r.status_code == 401 || r.status_code == 404) {
                            std::cerr << "Note: The HuggingFace repository may be private or not "
                                         "yet created."
                                      << std::endl;
                        }
                        std::filesystem::remove(output_path);
                    }
                }
            }
            return 0;
        }

        if (cmd == "process") {
            if (args.size() > 2) {
                std::cout << "Error: 'process' accepts at most two positional paths: input and "
                             "output."
                          << std::endl;
                return 1;
            }

            std::filesystem::path input_path =
                result.count("input") ? std::filesystem::path(result["input"].as<std::string>())
                                      : (args.empty() ? std::filesystem::path{}
                                                      : std::filesystem::path(args.front()));
            std::filesystem::path output_path =
                result.count("output")
                    ? std::filesystem::path(result["output"].as<std::string>())
                    : (args.size() >= 2 ? std::filesystem::path(args[1]) : std::filesystem::path{});

            if (input_path.empty()) {
                std::cout << "Error: 'process' requires an input path." << std::endl;
                return 1;
            }

            auto devices = list_devices();
            std::string device_str = result["device"].as<std::string>();
            DeviceInfo device = cli::select_device(devices, device_str);
            auto resolved = resolve_execution_defaults(result, argc, argv, device, true);
            if (!resolved) {
                std::cerr << "Error: " << resolved.error().message << std::endl;
                return 1;
            }
            if (output_path.empty()) {
                output_path = default_output_path_for_input(input_path);
                resolved->default_output_selected = true;
            }

            JobRequest req;
            req.input_path = input_path;
            req.hint_path =
                result.count("alpha-hint") ? result["alpha-hint"].as<std::string>() : "";
            req.output_path = output_path;
            req.model_path = resolved->model_path;
            req.params = resolved->params;
            req.device = device;

            if (!use_json) {
                std::cout << "Processing setup:\n"
                          << " - Input: " << req.input_path.string() << "\n"
                          << " - Output: " << req.output_path.string();
                if (resolved->default_output_selected) {
                    std::cout << " [auto]";
                }
                std::cout << "\n";
                if (resolved->preset.has_value()) {
                    std::cout << " - Preset: " << resolved->preset->name << "\n";
                }
                std::cout << " - Model: " << req.model_path.filename().string();
                if (resolved->default_model_selected) {
                    std::cout << " [auto]";
                }
                std::cout << "\n"
                          << " - Requested device: " << req.device.name << " [" << device_str
                          << "]\n";
            }

            auto progress = [](float p, const std::string& status) -> bool {
                {
                    int bar_width = 50;
                    std::cout << "\r[" << std::string(bar_width * p, '=')
                              << std::string(bar_width * (1 - p), ' ') << "] " << int(p * 100.0)
                              << "% " << status << std::flush;
                }
                return true;
            };

            JobEventCallback events = nullptr;
            if (use_json) {
                events = [](const JobEvent& event) -> bool {
                    std::cout << event_to_json(event).dump() << std::endl;
                    return true;
                };
            } else {
                events = [](const JobEvent& event) -> bool {
                    if (event.type == JobEventType::BackendSelected) {
                        std::cout << "Selected backend: " << backend_to_string_local(event.backend);
                        if (!event.message.empty()) {
                            std::cout << " (" << event.message << ")";
                        }
                        std::cout << std::endl;
                    }
                    if (event.type == JobEventType::Warning) {
                        std::cerr << "Warning: " << event.message << std::endl;
                        if (event.fallback.has_value()) {
                            std::cerr << "Fallback reason: " << event.fallback->reason << std::endl;
                        }
                    }
                    return true;
                };
            }

            auto process_res = JobOrchestrator::run(req, use_json ? nullptr : progress, events);
            if (!use_json) std::cout << std::endl;

            if (!process_res) {
                if (!use_json) {
                    std::cerr << "Error: " << process_res.error().message << std::endl;
                }
                return 1;
            }

            if (!use_json) {
                std::cout << "Done!" << std::endl;
            }
            return 0;
        }

        std::cout << "Unknown command: " << cmd << std::endl;
        return 1;

    } catch (const std::exception& e) {
        std::cerr << "Error parsing arguments: " << e.what() << std::endl;
        return 1;
    }
}
