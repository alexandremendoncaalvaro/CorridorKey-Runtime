#include <corridorkey/engine.hpp>
#include <corridorkey/version.hpp>
#include <cxxopts.hpp>
#include <iostream>
#include <filesystem>
#include <iomanip>
#include <fstream>
#include <cpr/cpr.h>

using namespace corridorkey;

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
    cxxopts::Options options("corridorkey", "High-performance neural green screen keyer runtime");

    options.add_options()
        ("command", "Sub-command to run (info, process)", cxxopts::value<std::string>())
        ("i,input", "Input video or directory of frames", cxxopts::value<std::string>())
        ("a,alpha-hint", "Alpha hint video or directory", cxxopts::value<std::string>())
        ("o,output", "Output directory", cxxopts::value<std::string>())
        ("m,model", "Path to GreenFormer ONNX model", cxxopts::value<std::string>())
        ("r,resolution", "Target resolution (0=auto, 512, 1024, etc.)", cxxopts::value<int>()->default_value("0"))
        ("d,device", "Device index from 'info' command", cxxopts::value<int>()->default_value("0"))
        ("variant", "Model variant to download (fp32, fp16, int8, all)", cxxopts::value<std::string>())
        ("despill", "Green spill removal strength (0.0 - 10.0)", cxxopts::value<float>()->default_value("1.0"))
        ("no-despeckle", "Disable morphological cleanup")
        ("despeckle-size", "Cleanup threshold in pixels", cxxopts::value<int>()->default_value("400"))
        ("input-linear", "Input images are already in linear space")
        ("v,version", "Print version")
        ("h,help", "Print help");

    options.parse_positional({"command"});

    try {
        auto result = options.parse(argc, argv);

        if (result.count("help")) {
            std::cout << options.help() << std::endl;
            return 0;
        }

        if (result.count("version")) {
            std::cout << "CorridorKey Runtime v" << CORRIDORKEY_VERSION_STRING << std::endl;
            return 0;
        }

        if (!result.count("command")) {
            std::cout << "Error: No command specified. Use 'info' or 'process'." << std::endl;
            return 1;
        }

        std::string cmd = result["command"].as<std::string>();

        if (cmd == "info") {
            print_info();
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
                    [](size_t downloadTotal, size_t downloadNow, size_t /*uploadTotal*/, size_t /*uploadNow*/, intptr_t /*userdata*/) -> bool {
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
            if (!result.count("input") || !result.count("alpha-hint") || !result.count("output") || !result.count("model")) {
                std::cout << "Error: 'process' command requires --input, --alpha-hint, --output, and --model." << std::endl;
                return 1;
            }

            std::filesystem::path input_path = result["input"].as<std::string>();
            std::filesystem::path hint_path = result["alpha-hint"].as<std::string>();
            std::filesystem::path output_path = result["output"].as<std::string>();
            std::filesystem::path model_path = result["model"].as<std::string>();

            InferenceParams params;
            params.target_resolution = result["resolution"].as<int>();
            params.despill_strength = result["despill"].as<float>();
            params.auto_despeckle = !result.count("no-despeckle");
            params.despeckle_size = result["despeckle-size"].as<int>();
            params.input_is_linear = result.count("input-linear");

            auto devices = list_devices();
            int device_idx = result["device"].as<int>();
            auto engine_res = Engine::create(model_path, devices[device_idx]);
            if (!engine_res) {
                std::cerr << "Engine Error: " << engine_res.error().message << std::endl;
                return 1;
            }
            auto engine = std::move(*engine_res);

            std::cout << "Processing Engine Setup:\n"
                      << " - Device: " << devices[device_idx].name << "\n"
                      << " - Target Resolution: " << (params.target_resolution > 0 ? params.target_resolution : engine->recommended_resolution()) << "x" << (params.target_resolution > 0 ? params.target_resolution : engine->recommended_resolution()) << "\n";

            auto is_video_file = [](const std::filesystem::path& p) {
                std::string ext = p.extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                return ext == ".mp4" || ext == ".mov" || ext == ".avi" || ext == ".mkv";
            };

            auto progress = [](float p, const std::string& status) -> bool {
                int bar_width = 50;
                std::cout << "\r[" << std::string(bar_width * p, '=') << std::string(bar_width * (1-p), ' ') << "] " 
                          << int(p * 100.0) << "% " << status << std::flush;
                return true;
            };

            if (std::filesystem::is_regular_file(input_path) && is_video_file(input_path)) {
                std::cout << "Processing video..." << std::endl;
                auto process_res = engine->process_video(input_path, hint_path, output_path, params, progress);
                std::cout << std::endl;

                if (!process_res) {
                    std::cerr << "Error: " << process_res.error().message << std::endl;
                    return 1;
                }
            } else {
                std::vector<std::filesystem::path> inputs;
                std::vector<std::filesystem::path> hints;

                if (std::filesystem::is_directory(input_path)) {
                    for (const auto& entry : std::filesystem::directory_iterator(input_path)) {
                        if (entry.is_regular_file()) {
                            inputs.push_back(entry.path());
                            // Assume hints have same filename in hint directory
                            hints.push_back(hint_path / entry.path().filename());
                        }
                    }
                    std::sort(inputs.begin(), inputs.end());
                    std::sort(hints.begin(), hints.end());
                } else {
                    inputs.push_back(input_path);
                    hints.push_back(hint_path);
                }

                std::cout << "Processing " << inputs.size() << " frames..." << std::endl;
                
                auto process_res = engine->process_sequence(inputs, hints, output_path, params, progress);
                std::cout << std::endl;

                if (!process_res) {
                    std::cerr << "Error: " << process_res.error().message << std::endl;
                    return 1;
                }
            }

            std::cout << "Done!" << std::endl;
            return 0;
        }

        std::cout << "Unknown command: " << cmd << std::endl;
        return 1;

    } catch (const std::exception& e) {
        std::cerr << "Error parsing arguments: " << e.what() << std::endl;
        return 1;
    }
}
