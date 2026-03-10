#include <cpr/cpr.h>

#include <corridorkey/engine.hpp>
#include <corridorkey/version.hpp>
#include <cstdio>
#include <cxxopts.hpp>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>

#include "../app/hardware_profile.hpp"
#include "../app/job_orchestrator.hpp"

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

    return json;
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
        cxxopts::value<std::string>())("i,input", "Input video or directory of frames",
                                       cxxopts::value<std::string>())(
        "a,alpha-hint", "Alpha hint video or directory (optional)", cxxopts::value<std::string>())(
        "o,output", "Output directory or file", cxxopts::value<std::string>())(
        "m,model", "Path to ONNX model file", cxxopts::value<std::string>())(
        "r,resolution", "Resolution (0=auto, 512, 768, 1024)",
        cxxopts::value<int>()->default_value("0"))(
        "d,device", "Device (auto, cpu, coreml, cuda, dml)",
        cxxopts::value<std::string>()->default_value("auto"))(
        "variant", "Model variant (int8, fp16, fp32)", cxxopts::value<std::string>())(
        "batch-size", "Number of frames to process in a single GPU call",
        cxxopts::value<int>()->default_value("1"))("despill", "Green spill removal (0.0-1.0)",
                                                   cxxopts::value<float>()->default_value("1.0"))(
        "no-despeckle", "Disable cleanup")("tiled", "Enable tiling for high-res (4K+)")(
        "json", "Output results in JSON format")("v,version", "Print version")(
        "h,help", "Print detailed help");

    options.parse_positional({"command"});

    if (argc <= 1) {
        std::cout << "==========================================\n"
                  << "      CorridorKey Runtime v" << CORRIDORKEY_VERSION_STRING << "\n"
                  << "==========================================\n\n"
                  << "Welcome! To use this tool, follow these steps:\n\n"
                  << "1. Download a model (int8 is recommended):\n"
                  << "   corridorkey download --variant int8\n\n"
                  << "2. Process a video (Auto-detecting resolution/device):\n"
                  << "   corridorkey process -i input.mp4 -o output.mp4 -m "
                     "models/corridorkey_int8_512.onnx\n\n"
                  << "3. For 4K videos or high-detail, use tiling:\n"
                  << "   corridorkey process -i input.mp4 -o output.mp4 -m "
                     "models/corridorkey_int8_768.onnx --tiled\n\n"
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

        if (cmd == "info") {
            if (use_json) {
                std::cout << JobOrchestrator::get_system_info().dump(4) << std::endl;
            } else {
                print_info();
            }
            return 0;
        }

        if (cmd == "doctor") {
            std::filesystem::path models_dir = "models";
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
            if (!result.count("model")) {
                std::cout << "Error: 'benchmark' requires a --model path." << std::endl;
                return 1;
            }

            auto devices = list_devices();
            DeviceInfo selected_device = devices[0];

            auto report =
                JobOrchestrator::run_benchmark(result["model"].as<std::string>(), selected_device);
            if (use_json) {
                std::cout << report.dump(4) << std::endl;
            } else {
                std::cout << "--- Benchmark Results ---\n"
                          << "Device: " << report["device"].get<std::string>() << "\n"
                          << "Backend: " << report["backend"].get<std::string>() << "\n"
                          << "Resolution: " << report["resolution"].get<int>() << "x"
                          << report["resolution"].get<int>() << "\n"
                          << "Avg Latency: " << report["avg_latency_ms"].get<float>() << " ms\n"
                          << "Estimated FPS: " << report["fps"].get<float>() << "\n";
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
            if (!result.count("input") || !result.count("output") || !result.count("model")) {
                std::cout << "Error: 'process' command requires --input, --output, and --model."
                          << std::endl;
                return 1;
            }

            JobRequest req;
            req.input_path = result["input"].as<std::string>();
            req.hint_path =
                result.count("alpha-hint") ? result["alpha-hint"].as<std::string>() : "";
            req.output_path = result["output"].as<std::string>();
            req.model_path = result["model"].as<std::string>();

            req.params.target_resolution = result["resolution"].as<int>();
            req.params.despill_strength = result["despill"].as<float>();
            req.params.auto_despeckle = !result.count("no-despeckle");
            req.params.batch_size = result["batch-size"].as<int>();
            req.params.enable_tiling = result.count("tiled");

            auto devices = list_devices();
            std::string device_str = result["device"].as<std::string>();
            req.device = devices[0];  // Default to auto/first

            if (device_str != "auto") {
                try {
                    int idx = std::stoi(device_str);
                    if (idx >= 0 && idx < (int)devices.size()) {
                        req.device = devices[idx];
                    }
                } catch (...) {
                    std::transform(device_str.begin(), device_str.end(), device_str.begin(),
                                   ::tolower);
                    for (const auto& d : devices) {
                        std::string b_name;
                        switch (d.backend) {
                            case Backend::CPU:
                                b_name = "cpu";
                                break;
                            case Backend::CoreML:
                                b_name = "coreml";
                                break;
                            case Backend::CUDA:
                                b_name = "cuda";
                                break;
                            case Backend::TensorRT:
                                b_name = "tensorrt";
                                break;
                            case Backend::DirectML:
                                b_name = "dml";
                                break;
                            default:
                                b_name = "";
                                break;
                        }
                        if (b_name == device_str) {
                            req.device = d;
                            break;
                        }
                    }
                }
            }

            if (!use_json) {
                std::cout << "Processing Engine Setup:\n"
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
