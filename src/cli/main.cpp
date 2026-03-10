#include <corridorkey/engine.hpp>
#include <corridorkey/version.hpp>
#include <cxxopts.hpp>
#include <iostream>
#include <filesystem>
#include <iomanip>
#include <fstream>
#include <cpr/cpr.h>
#include "../app/job_orchestrator.hpp"
#include "../app/hardware_profile.hpp"

using namespace corridorkey;
using namespace corridorkey::app;

void print_info() {
    auto devices = list_devices();
    std::cout << "CorridorKey Runtime v" << CORRIDORKEY_VERSION_STRING << "\n";
    std::cout << "------------------------------------------\n";
    std::cout << "Detected Hardware Devices:\n";
    for (const auto& d : devices) {
        std::string backend_name;
        switch (d.backend) {
            case Backend::CPU: backend_name = "CPU (Universal)"; break;
            case Backend::CoreML: backend_name = "CoreML (Apple Silicon NPU/GPU)"; break;
            case Backend::TensorRT: backend_name = "TensorRT (NVIDIA RTX)"; break;
            case Backend::CUDA: backend_name = "CUDA (NVIDIA)"; break;
            case Backend::DirectML: backend_name = "DirectML (Windows GPU)"; break;
            default: backend_name = "Unknown"; break;
        }
        std::cout << " - " << std::left << std::setw(30) << d.name 
                  << " [" << backend_name << "] " 
                  << (d.available_memory_mb > 0 ? (std::to_string(d.available_memory_mb) + " MB") : "") << "\n";
    }
}

int main(int argc, char* argv[]) {
    cxxopts::Options options("corridorkey", "CorridorKey Runtime - High-performance neural green screen keyer");

    options.add_options()
        ("command", "Sub-command to run (info, process, download, doctor, benchmark)", cxxopts::value<std::string>())
        ("i,input", "Input video or directory of frames", cxxopts::value<std::string>())
        ("a,alpha-hint", "Alpha hint video or directory (optional)", cxxopts::value<std::string>())
        ("o,output", "Output directory or file", cxxopts::value<std::string>())
        ("m,model", "Path to ONNX model file", cxxopts::value<std::string>())
        ("r,resolution", "Resolution (0=auto, 512, 768, 1024)", cxxopts::value<int>()->default_value("0"))
        ("d,device", "Device (auto, cpu, coreml, cuda, dml)", cxxopts::value<std::string>()->default_value("auto"))
        ("variant", "Model variant (int8, fp16, fp32)", cxxopts::value<std::string>())
        ("batch-size", "Number of frames to process in a single GPU call", cxxopts::value<int>()->default_value("1"))
        ("despill", "Green spill removal (0.0-1.0)", cxxopts::value<float>()->default_value("1.0"))
        ("no-despeckle", "Disable cleanup")
        ("tiled", "Enable tiling for high-res (4K+)")
        ("json", "Output results in JSON format")
        ("v,version", "Print version")
        ("h,help", "Print detailed help");

    options.parse_positional({"command"});

    if (argc <= 1) {
        std::cout << "==========================================\n"
                  << "      CorridorKey Runtime v" << CORRIDORKEY_VERSION_STRING << "\n"
                  << "==========================================\n\n"
                  << "Welcome! To use this tool, follow these steps:\n\n"
                  << "1. Download a model (int8 is recommended):\n"
                  << "   corridorkey download --variant int8\n\n"
                  << "2. Process a video (Auto-detecting resolution/device):\n"
                  << "   corridorkey process -i input.mp4 -o output.mp4 -m models/corridorkey_int8_512.onnx\n\n"
                  << "3. For 4K videos or high-detail, use tiling:\n"
                  << "   corridorkey process -i input.mp4 -o output.mp4 -m models/corridorkey_int8_768.onnx --tiled\n\n"
                  << "Run 'corridorkey --help' for all options.\n" << std::endl;
        return 0;
    }

    try {
        auto result = options.parse(argc, argv);
        bool use_json = result.count("json");

        if (result.count("help")) {
            std::cout << options.help() << std::endl;
            return 0;
        }

        if (result.count("version")) {
            if (use_json) {
                std::cout << nlohmann::json({{"version", CORRIDORKEY_VERSION_STRING}}).dump() << std::endl;
            } else {
                std::cout << "CorridorKey Runtime v" << CORRIDORKEY_VERSION_STRING << std::endl;
            }
            return 0;
        }

        if (!result.count("command")) {
            std::cout << "Error: No command specified. Use 'info', 'download', 'process', 'doctor' or 'benchmark'." << std::endl;
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
                        std::cout << " [OK] Model: " << m["variant"].get<std::string>() << "_" << m["resolution"].get<int>() << "\n";
                    }
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
            
            auto report = JobOrchestrator::run_benchmark(result["model"].as<std::string>(), selected_device);
            if (use_json) {
                std::cout << report.dump(4) << std::endl;
            } else {
                std::cout << "--- Benchmark Results ---\n"
                          << "Device: " << report["device"].get<std::string>() << "\n"
                          << "Resolution: " << report["resolution"].get<int>() << "x" << report["resolution"].get<int>() << "\n"
                          << "Avg Latency: " << report["avg_latency_ms"].get<float>() << " ms\n"
                          << "Estimated FPS: " << report["fps"].get<float>() << "\n";
            }
            return 0;
        }

        if (cmd == "download") {
            std::string variant = result.count("variant") ? result["variant"].as<std::string>() : "int8";
            std::vector<std::string> variants_to_download;
            
            if (variant == "all") {
                variants_to_download = {"int8", "fp16", "fp32"};
            } else if (variant == "int8" || variant == "fp16" || variant == "fp32") {
                variants_to_download.push_back(variant);
            } else {
                std::cerr << "Error: Invalid variant. Allowed values are 'int8', 'fp16', 'fp32', or 'all'." << std::endl;
                return 1;
            }

            std::filesystem::create_directory("models");

            for (const auto& v : variants_to_download) {
                std::string filename = "corridorkey_" + v + ".onnx";
                std::filesystem::path output_path = std::filesystem::path("models") / filename;
                std::string url = "https://huggingface.co/corridorkey/models/resolve/main/" + filename;

                std::cout << "Downloading " << filename << "..." << std::endl;
                std::ofstream of(output_path, std::ios::binary);
                
                cpr::Response r = cpr::Download(of, cpr::Url{url}, cpr::ProgressCallback(
                    [](size_t downloadTotal, size_t downloadNow, size_t, size_t, intptr_t) -> bool {
                        if (downloadTotal > 0) {
                            float p = static_cast<float>(downloadNow) / downloadTotal;
                            int bar_width = 50;
                            std::cout << "\r[" << std::string(bar_width * p, '=') << std::string(bar_width * (1-p), ' ') << "] " 
                                      << int(p * 100.0) << "% " << std::flush;
                        }
                        return true;
                    }));
                
                std::cout << std::endl;
                of.close();
                
                if (r.status_code == 200) {
                    std::cout << "Successfully downloaded " << filename << " to models/" << std::endl;
                } else {
                    std::cerr << "Failed to download " << filename << ". HTTP Status: " << r.status_code << std::endl;
                    if (r.status_code == 401 || r.status_code == 404) {
                        std::cerr << "Note: The HuggingFace repository may be private or not yet created." << std::endl;
                    }
                    std::filesystem::remove(output_path);
                }
            }
            return 0;
        }
        
        if (cmd == "process") {
            if (!result.count("input") || !result.count("output") || !result.count("model")) {
                std::cout << "Error: 'process' command requires --input, --output, and --model." << std::endl;
                return 1;
            }

            JobRequest req;
            req.input_path = result["input"].as<std::string>();
            req.hint_path = result.count("alpha-hint") ? result["alpha-hint"].as<std::string>() : "";
            req.output_path = result["output"].as<std::string>();
            req.model_path = result["model"].as<std::string>();

            req.params.target_resolution = result["resolution"].as<int>();
            req.params.despill_strength = result["despill"].as<float>();
            req.params.auto_despeckle = !result.count("no-despeckle");
            req.params.batch_size = result["batch-size"].as<int>();
            req.params.enable_tiling = result.count("tiled");

            auto devices = list_devices();
            std::string device_str = result["device"].as<std::string>();
            req.device = devices[0]; // Default to auto/first

            if (device_str != "auto") {
                try {
                    int idx = std::stoi(device_str);
                    if (idx >= 0 && idx < (int)devices.size()) {
                        req.device = devices[idx];
                    }
                } catch (...) {
                    std::transform(device_str.begin(), device_str.end(), device_str.begin(), ::tolower);
                    for (const auto& d : devices) {
                        std::string b_name;
                        switch (d.backend) {
                            case Backend::CPU: b_name = "cpu"; break;
                            case Backend::CoreML: b_name = "coreml"; break;
                            case Backend::CUDA: b_name = "cuda"; break;
                            case Backend::TensorRT: b_name = "tensorrt"; break;
                            case Backend::DirectML: b_name = "dml"; break;
                            default: b_name = ""; break;
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
                          << " - Device: " << req.device.name << " [" << device_str << "]\n";
            }

            auto progress = [use_json](float p, const std::string& status) -> bool {
                if (use_json) {
                    nlohmann::json event;
                    event["type"] = "progress";
                    event["value"] = p;
                    event["status"] = status;
                    std::cout << event.dump() << std::endl;
                } else {
                    int bar_width = 50;
                    std::cout << "\r[" << std::string(bar_width * p, '=') << std::string(bar_width * (1-p), ' ') << "] " 
                              << int(p * 100.0) << "% " << status << std::flush;
                }
                return true;
            };

            auto process_res = JobOrchestrator::run(req, progress);
            if (!use_json) std::cout << std::endl;

            if (!process_res) {
                if (use_json) {
                    nlohmann::json err;
                    err["type"] = "error";
                    err["message"] = process_res.error().message;
                    err["code"] = static_cast<int>(process_res.error().code);
                    std::cout << err.dump() << std::endl;
                } else {
                    std::cerr << "Error: " << process_res.error().message << std::endl;
                }
                return 1;
            }

            if (use_json) {
                nlohmann::json success;
                success["type"] = "complete";
                success["message"] = "Processing finished successfully";
                std::cout << success.dump() << std::endl;
            } else {
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
